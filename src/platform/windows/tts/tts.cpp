#include "platform/windows/tts/tts.h"

#include <xaudio2.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.SpeechSynthesis.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "ai/responses.h"
#include "core/config.h"
#include "diagnostics/log.h"
#include "platform/windows/ai/http_client.h"
#include "platform/windows/strings.h"

namespace dp {

namespace ss = winrt::Windows::Media::SpeechSynthesis;

namespace {

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool LooksLikeWav(const std::vector<uint8_t>& b) {
    return b.size() >= 44 && !memcmp(b.data(), "RIFF", 4) && !memcmp(b.data() + 8, "WAVE", 4);
}

// ---- Windows 음성 ----
class WinRtTts final : public ITtsProvider {
public:
    const char* Name() const override { return "winrt"; }

    std::vector<std::string> Voices() override {
        std::vector<std::string> out;
        for (const auto& v : ss::SpeechSynthesizer::AllVoices()) out.push_back(ToUtf8(std::wstring(v.DisplayName())));
        return out;
    }

    bool Synthesize(const std::string& utf8, const TtsVoiceSpec& spec, TtsAudio& out, bool& matched,
                    std::string& error) override {
        try {
            if (!synth_) synth_ = ss::SpeechSynthesizer();
            matched = spec.name.empty();
            if (!spec.name.empty()) {
                const std::wstring want = Lower(ToWide(spec.name));
                for (const auto& v : ss::SpeechSynthesizer::AllVoices()) {
                    if (Lower(std::wstring(v.DisplayName())).find(want) != std::wstring::npos) {
                        synth_.Voice(v);
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched) synth_.Voice(ss::SpeechSynthesizer::DefaultVoice());
            synth_.Options().SpeakingRate(std::clamp(spec.rate, 0.5, 2.0));
            synth_.Options().IncludeWordBoundaryMetadata(true);
            auto stream = synth_.SynthesizeTextToStreamAsync(winrt::to_hstring(utf8)).get();
            out.wordStartsMs.clear();
            for (const auto& track : stream.TimedMetadataTracks())
                for (const auto& cue : track.Cues())
                    out.wordStartsMs.push_back(static_cast<uint32_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(cue.StartTime()).count()));
            const auto size = static_cast<uint32_t>(stream.Size());
            winrt::Windows::Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
            reader.LoadAsync(size).get();
            out.wav.resize(size);
            reader.ReadBytes(out.wav);
            return !out.wav.empty();
        } catch (const winrt::hresult_error& e) {
            error = std::format("winrt_0x{:08X}", static_cast<uint32_t>(e.code().value));
        } catch (...) {
            error = "exception";
        }
        return false;
    }

private:
    ss::SpeechSynthesizer synth_{nullptr};
};

// ---- OpenAI 호환 로컬 서버: POST /v1/audio/speech → WAV ----
class HttpTts final : public ITtsProvider {
public:
    explicit HttpTts(HttpTtsOptions o) : o_(std::move(o)) {}
    const char* Name() const override { return "http"; }

    bool Synthesize(const std::string& utf8, const TtsVoiceSpec& spec, TtsAudio& out, bool& matched,
                    std::string& error) override {
        matched = true;
        if (!core::IsLoopbackUrl(o_.url)) {  // PC 밖으로 보내지 않는다
            error = "url_not_local";
            return false;
        }
        nlohmann::json body = nlohmann::json::object();
        if (!o_.extraJson.empty()) {  // 엔진별 추가 필드 (예: {"lang":"ko"})
            body = nlohmann::json::parse(o_.extraJson, nullptr, false);
            if (!body.is_object()) {
                error = "extra_json_invalid";
                return false;
            }
        }
        if (!o_.model.empty()) body["model"] = o_.model;
        body["input"] = utf8;
        if (!spec.aiVoice.empty()) body["voice"] = spec.aiVoice;
        body["response_format"] = "wav";  // 엔진에 따라 기본값이 mp3 라서 항상 명시
        if (spec.rate != 1.0) body["speed"] = spec.rate;
        HttpRequest req;
        req.method = "POST";
        req.url = o_.url;
        req.headers = {{"Content-Type", "application/json"}, {"Accept", "audio/wav"}};
        req.body = body.dump();
        req.timeoutMs = o_.timeoutSec * 1000;
        http_.ResetCancel();
        HttpResponse res = http_.Send(req);
        SecureZeroMemory(req.body.data(), req.body.size());
        if (res.cancelled) {
            error = "cancelled";
            return false;
        }
        if (!res.Ok()) {
            error = res.status ? std::format("http_{}", res.status) : std::format("network_{}", res.winError);
            return false;
        }
        out.wav.assign(res.body.begin(), res.body.end());
        if (!LooksLikeWav(out.wav)) {
            error = "not_wav";
            return false;
        }
        return true;
    }
    void Cancel() override { http_.Cancel(); }

private:
    HttpTtsOptions o_;
    HttpClient http_;
};

// ---- 실행 파일: 인자 템플릿 → WAV 파일 ----
class CommandTts final : public ITtsProvider {
public:
    explicit CommandTts(CommandTtsOptions o) : o_(std::move(o)), cancel_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~CommandTts() override { CloseHandle(cancel_); }
    const char* Name() const override { return "command"; }
    void Cancel() override { SetEvent(cancel_); }

    bool Synthesize(const std::string& utf8, const TtsVoiceSpec& spec, TtsAudio& out, bool& matched,
                    std::string& error) override {
        matched = true;
        ResetEvent(cancel_);
        if (o_.path.empty() || o_.args.find(L"{out}") == std::wstring::npos) {
            error = "command_not_configured";
            return false;
        }
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        const std::filesystem::path dir = std::filesystem::path(tmp) / L"DesktopPet" / L"tts";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::wstring stem = std::format(L"tts_{}_{}", GetCurrentProcessId(), ++seq_);
        const std::filesystem::path outPath = dir / (stem + L".wav"), textPath = dir / (stem + L".txt");
        const bool useTextFile = o_.args.find(L"{text_file}") != std::wstring::npos;

        const core::CommandKind kind = core::CommandKindOf(ToUtf8(o_.path));
        if (kind == core::CommandKind::Unsupported || o_.path.find(L'"') != std::wstring::npos) {
            error = "command_unsupported_type";
            return false;
        }
        const bool batch = kind == core::CommandKind::Batch;
        const std::wstring args = ExpandCommandArgs(
            o_.args, {ToWide(utf8), textPath.wstring(), outPath.wstring(), ToWide(spec.aiVoice), std::format(L"{:.2f}", spec.rate)},
            batch, error);
        if (!error.empty()) return false;
        if (useTextFile) std::ofstream(textPath, std::ios::binary).write(utf8.data(), utf8.size());
        std::wstring cmd = L"\"" + o_.path + L"\" " + args;

        auto cleanup = [&] {
            std::filesystem::remove(outPath, ec);
            if (useTextFile) std::filesystem::remove(textPath, ec);
        };
        // 상속 핸들은 표준 입출력만 (OAuth 콜백 소켓 등이 새지 않게). 출력은 NUL (GUI 앱이라 표준 출력이 없다)
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        HANDLE stdinRead = nullptr, stdinWrite = nullptr;
        if (o_.stdinText && CreatePipe(&stdinRead, &stdinWrite, &sa, 0)) SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
        std::vector<HANDLE> inherit;
        if (nul != INVALID_HANDLE_VALUE) inherit.push_back(nul);
        if (stdinRead) inherit.push_back(stdinRead);
        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
        std::vector<uint8_t> attrBuf(attrSize);
        auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
        const bool attrOk = !inherit.empty() && InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                            UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(),
                                                      inherit.size() * sizeof(HANDLE), nullptr, nullptr);
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        if (attrOk) {
            si.lpAttributeList = attrs;
            si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            si.StartupInfo.hStdInput = stdinRead ? stdinRead : nul;
            si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = nul;
        }
        // Job: 시간 초과·취소 때 배치가 띄운 손자 프로세스까지 끝낸다
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
            li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
        }
        PROCESS_INFORMATION pi{};
        // 실행 파일은 경로를 lpApplicationName 으로 (명령줄 첫 토큰 해석에 기대지 않음). 배치는 시스템이 cmd.exe 로 연다
        const BOOL started = attrOk && CreateProcessW(batch ? nullptr : o_.path.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                                      CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                                                      nullptr, nullptr, &si.StartupInfo, &pi);
        const DWORD startErr = GetLastError();
        SecureZeroMemory(cmd.data(), cmd.size() * sizeof(wchar_t));
        if (attrOk) DeleteProcThreadAttributeList(attrs);
        if (stdinRead) CloseHandle(stdinRead);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        if (!started) {
            if (stdinWrite) CloseHandle(stdinWrite);
            if (job) CloseHandle(job);
            error = std::format("start_failed_{}", attrOk ? startErr : 0);
            cleanup();
            return false;
        }
        if (job) AssignProcessToJobObject(job, pi.hProcess);
        ResumeThread(pi.hThread);
        // 표준 입력은 따로 쓴다: 엔진이 읽지 않아 파이프가 차도 시간 제한·취소가 동작하게 (끝내면 쓰기가 풀린다)
        std::thread writer;
        if (stdinWrite) {
            writer = std::thread([h = stdinWrite, line = utf8 + "\n"] {
                DWORD written = 0;
                WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
                CloseHandle(h);
            });
        }
        HANDLE waits[2] = {pi.hProcess, cancel_};
        const DWORD w = WaitForMultipleObjects(2, waits, FALSE, static_cast<DWORD>(o_.timeoutSec) * 1000);
        DWORD code = 0;
        if (w != WAIT_OBJECT_0) {
            if (job) TerminateJobObject(job, 1);
            else TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            error = w == WAIT_OBJECT_0 + 1 ? "cancelled" : "timeout";
        } else {
            GetExitCodeProcess(pi.hProcess, &code);
            if (code != 0) error = std::format("exit_{}", code);
        }
        if (writer.joinable()) {
            if (job) TerminateJobObject(job, 1);  // 아직 쓰는 중이면 읽을 쪽을 끝내 쓰기를 푼다
            writer.join();
        }
        if (job) CloseHandle(job);  // KILL_ON_JOB_CLOSE: 남은 손자 프로세스도 끝난다
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (error.empty()) {
            std::ifstream in(outPath, std::ios::binary);
            out.wav.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (!LooksLikeWav(out.wav)) error = out.wav.empty() ? "no_output" : "not_wav";
        }
        cleanup();
        return error.empty();
    }

private:
    CommandTtsOptions o_;
    HANDLE cancel_;
    unsigned seq_ = 0;
};

struct EndCallback final : IXAudio2VoiceCallback {
    HANDLE ended = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~EndCallback() { CloseHandle(ended); }
    void STDMETHODCALLTYPE OnStreamEnd() override { SetEvent(ended); }
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override { SetEvent(ended); }
};

}  // namespace

std::unique_ptr<ITtsProvider> CreateWinRtTts() { return std::make_unique<WinRtTts>(); }
std::unique_ptr<ITtsProvider> CreateHttpTts(HttpTtsOptions o) { return std::make_unique<HttpTts>(std::move(o)); }
std::unique_ptr<ITtsProvider> CreateCommandTts(CommandTtsOptions o) { return std::make_unique<CommandTts>(std::move(o)); }

std::wstring ExpandCommandArgs(const std::wstring& tmpl, const CommandArgValues& v, bool batch, std::string& error) {
    error.clear();
    // 배치: cmd.exe 는 따옴표 안에서도 % ! 를 펼치고 " 로 따옴표가 끝난다 → 이런 문자가 든 값은 넘기지 않는다
    auto batchValue = [&](const std::wstring& value) -> std::wstring {
        for (wchar_t c : value)
            if (c < 0x20 || std::wcschr(L"\"%!^&|<>", c)) {
                error = "unsafe_command_value";
                return {};
            }
        if (!value.empty() && value.back() == L'\\') {
            error = "unsafe_command_value";
            return {};
        }
        return L"\"" + value + L"\"";
    };
    // 실행 파일: 발화·음성이 옵션처럼 보이면 앞에 공백 (따옴표로 묶여 한 인수로 간다)
    auto speech = [](const std::wstring& value) {
        return QuoteArg(!value.empty() && (value[0] == L'-' || value[0] == L'/') ? L" " + value : value);
    };
    const std::pair<std::wstring_view, int> tokens[] = {{L"{text_file}", 1}, {L"{text}", 0}, {L"{out}", 2},
                                                        {L"{voice}", 3}, {L"{speed}", 4}};
    std::wstring out;
    for (size_t i = 0; i < tmpl.size();) {
        int which = -1;
        size_t len = 0;
        if (tmpl[i] == L'{')
            for (const auto& [name, id] : tokens)
                if (tmpl.compare(i, name.size(), name) == 0) {
                    which = id;
                    len = name.size();
                    break;
                }
        if (which < 0) {
            out += tmpl[i++];
            continue;
        }
        i += len;
        if (batch && (which == 0 || which == 3)) {
            error = "batch_text_arg";
            return {};
        }
        const std::wstring* values[] = {&v.text, &v.textFile, &v.out, &v.voice, &v.speed};
        const std::wstring& value = *values[which];
        if (batch) out += batchValue(value);
        else out += which == 0 || which == 3 ? speech(value) : QuoteArg(value);
        if (!error.empty()) return {};
    }
    return out;
}

std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (size_t i = 0;; ++i) {
        size_t slashes = 0;
        while (i < arg.size() && arg[i] == L'\\') ++i, ++slashes;
        if (i == arg.size()) {
            out.append(slashes * 2, L'\\');  // 닫는 따옴표 앞 역슬래시는 두 배
            break;
        }
        if (arg[i] == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        out += arg[i];
    }
    return out + L"\"";
}

// RIFF/WAVE 에서 fmt·data 청크 찾기 (샘플레이트는 헤더 값을 쓴다: 엔진마다 다름)
bool ParseWav(const std::vector<uint8_t>& wav, WAVEFORMATEX& fmt, const uint8_t*& data, uint32_t& size) {
    auto u32 = [&](size_t at) { return wav[at] | (wav[at + 1] << 8) | (wav[at + 2] << 16) | (static_cast<uint32_t>(wav[at + 3]) << 24); };
    if (wav.size() < 12 || memcmp(wav.data(), "RIFF", 4) || memcmp(wav.data() + 8, "WAVE", 4)) return false;
    bool haveFmt = false;
    for (size_t at = 12; at + 8 <= wav.size();) {
        uint32_t len = u32(at + 4);
        const size_t body = at + 8;
        const bool isData = !memcmp(wav.data() + at, "data", 4);
        // 스트리밍 서버는 data 길이를 0xFFFFFFFF 등으로 줄 수 있다 → 남은 길이로 자른다
        if (body + len > wav.size()) {
            if (!isData) break;
            len = static_cast<uint32_t>(wav.size() - body);
        }
        if (!memcmp(wav.data() + at, "fmt ", 4) && len >= 16) {
            fmt = {};
            memcpy(&fmt, wav.data() + body, std::min<size_t>(len, sizeof(WAVEFORMATEX)));
            if (len < sizeof(WAVEFORMATEX)) fmt.cbSize = 0;
            // WAVE_FORMAT_EXTENSIBLE: 하위 형식 GUID 의 첫 2바이트가 PCM(1)·FLOAT(3) 이면 단순 형식으로 바꾼다
            if (fmt.wFormatTag == 0xFFFE && len >= 40) fmt.wFormatTag = static_cast<WORD>(wav[body + 24] | (wav[body + 25] << 8));
            if (fmt.wFormatTag == WAVE_FORMAT_PCM || fmt.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) fmt.cbSize = 0;
            haveFmt = true;
        } else if (isData && haveFmt) {
            data = wav.data() + body;
            size = len - len % std::max<uint32_t>(1, fmt.nBlockAlign);
            return (fmt.wFormatTag == WAVE_FORMAT_PCM || fmt.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) && size > 0;
        }
        at = body + len + (len & 1);
    }
    return false;
}

struct TtsService::Audio {
    winrt::com_ptr<IXAudio2> xa;
    IXAudio2MasteringVoice* master = nullptr;
    std::unique_ptr<ITtsProvider> provider;
    ~Audio() {
        if (master) master->DestroyVoice();
    }
};

TtsService::TtsService() = default;
TtsService::~TtsService() { Shutdown(); }

void TtsService::Start(TtsSettings settings) {
    if (thread_.joinable()) return;
    settings_ = std::move(settings);
    quit_ = false;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this] { Run(); });
}

void TtsService::Shutdown() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard lock(mu_);
        quit_ = true;
        queue_.clear();
        if (active_) active_->Cancel();
    }
    SetEvent(stopEvent_);
    cv_.notify_all();
    thread_.join();
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

void TtsService::Speak(std::string utf8, TtsVoiceSpec voice) {
    if (!thread_.joinable() || utf8.empty()) return;
    {
        std::lock_guard lock(mu_);
        while (queue_.size() >= 2) queue_.pop_front();  // 대기열 상한 2 (오래된 것 버림)
        queue_.push_back({std::move(utf8), std::move(voice)});
    }
    cv_.notify_one();
}

void TtsService::StopAll() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard lock(mu_);
        queue_.clear();
        if (active_) active_->Cancel();
    }
    SetEvent(stopEvent_);
}

bool TtsService::Play(const TtsAudio& audio) {
    WAVEFORMATEX fmt{};
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    if (!ParseWav(audio.wav, fmt, data, size)) return false;
    if (!audio_->xa) {
        if (FAILED(XAudio2Create(audio_->xa.put(), 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
            FAILED(audio_->xa->CreateMasteringVoice(&audio_->master))) {
            audio_->xa = nullptr;
            return false;
        }
    }
    EndCallback cb;
    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(audio_->xa->CreateSourceVoice(&voice, &fmt, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &cb))) return false;
    voice->SetVolume(static_cast<float>(std::clamp(settings_.volume, 0.0, 1.0)));
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = size;
    buf.pAudioData = data;
    buf.Flags = XAUDIO2_END_OF_STREAM;
    bool ok = SUCCEEDED(voice->SubmitSourceBuffer(&buf)) && SUCCEEDED(voice->Start());
    if (ok) {
        HANDLE waits[2] = {cb.ended, stopEvent_};
        ok = WaitForMultipleObjects(2, waits, FALSE, 120000) == WAIT_OBJECT_0;
    }
    voice->Stop();
    voice->FlushSourceBuffers();
    voice->DestroyVoice();  // 콜백이 끝난 뒤 반환된다
    return ok;
}

void TtsService::Run() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);  // WinRT .get() 은 STA 에서 쓸 수 없다
    SetThreadDescription(GetCurrentThread(), L"DesktopPet.TTS");
    audio_ = std::make_unique<Audio>();
    for (;;) {
        Item item;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, [this] { return quit_ || !queue_.empty(); });
            if (quit_) break;
            item = std::move(queue_.front());
            queue_.pop_front();
            ResetEvent(stopEvent_);
        }
        if (!audio_->provider) {
            if (settings_.engine == "http") audio_->provider = CreateHttpTts(settings_.http);
            else if (settings_.engine == "command") audio_->provider = CreateCommandTts(settings_.command);
            else audio_->provider = CreateWinRtTts();
            log::Info("[tts] engine={} voices={}", audio_->provider->Name(), audio_->provider->Voices().size());
        }
        {
            std::lock_guard lock(mu_);
            active_ = audio_->provider.get();
        }
        const ULONGLONG t0 = GetTickCount64();
        TtsAudio a;
        bool matched = false;
        std::string err;
        const bool synthesized = audio_->provider->Synthesize(item.text, item.voice, a, matched, err);
        {
            std::lock_guard lock(mu_);
            active_ = nullptr;
        }
        const size_t chars = ai::Utf8Length(item.text);
        SecureZeroMemory(item.text.data(), item.text.size());
        if (!synthesized) {  // 실패한 발화는 읽지 않는다 (다른 엔진으로 대체하지 않음)
            log::Warn("[tts] synth failed engine={} code={} chars={} ms={}", audio_->provider->Name(), err, chars,
                      GetTickCount64() - t0);
            continue;
        }
        if (!matched) log::Warn("[tts] 설정한 음성을 찾지 못해 기본 음성 사용 ([tts] voice 또는 캐릭터 [voice] name)");
        const ULONGLONG t1 = GetTickCount64();
        const bool played = Play(a);
        log::Info("[tts] spoke engine={} chars={} words={} bytes={} synth_ms={} play_ms={} completed={}",
                  audio_->provider->Name(), chars, a.wordStartsMs.size(), a.wav.size(), t1 - t0, GetTickCount64() - t1,
                  played);
    }
    audio_.reset();
    winrt::uninit_apartment();
}

}  // namespace dp
