#pragma once
// 로컬 TTS (P4 계획 §2.6, 기준선 §7.4). 기본 꺼짐이고 켤 때 처음 만든다.
// - 엔진: ITtsProvider
//   - winrt:   Windows 음성 (WinRT SpeechSynthesizer, OneCore 음성)
//   - http:    OpenAI 호환 로컬 서버 POST /v1/audio/speech (Supertonic serve·Kokoro-FastAPI 등, 127.0.0.1 만)
//   - command: WAV 를 쓰는 실행 파일 (sherpa-onnx·supertonic CLI 등). 엔진·모델은 동봉하지 않는다
//   - VOICEVOX 호환 방식은 보류 (2026-10-19 결정)
// - 실패하면 그 발화는 읽지 않는다 (다른 엔진으로 대체하지 않음, 2026-10-19 결정)
// - 재생: XAudio2, 출력 큐 1개 → 동시 재생 없음. 대기열은 최대 2개 (넘치면 오래된 것을 버린다).
// 발화 원문은 로그에 남기지 않는다 (글자 수·소요 시간만).
#include <windows.h>

#include <mmreg.h>  // WAVEFORMATEX (WIN32_LEAN_AND_MEAN 에서 빠짐)

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dp {

struct TtsVoiceSpec {
    std::string name;     // Windows 음성 표시 이름 일부 (대소문자 무시). 비면 시스템 기본
    std::string aiVoice;  // 로컬 AI 엔진의 voice id (http "voice", command {voice}). 비면 보내지 않음
    double rate = 1.0;
};

struct TtsAudio {
    std::vector<uint8_t> wav;            // RIFF/WAVE (PCM)
    std::vector<uint32_t> wordStartsMs;  // 단어 시작 시각. 립싱크용 자리 (기준선 §7.4)
};

class ITtsProvider {
public:
    virtual ~ITtsProvider() = default;
    virtual const char* Name() const = 0;
    virtual std::vector<std::string> Voices() { return {}; }
    // matched: voice 이름이 맞는 음성을 찾았나 (winrt 만 의미 있음)
    virtual bool Synthesize(const std::string& utf8, const TtsVoiceSpec& spec, TtsAudio& out, bool& matched,
                            std::string& error) = 0;
    virtual void Cancel() {}  // 다른 스레드에서: 진행 중 합성 중단
};

struct HttpTtsOptions {
    std::string url = "http://127.0.0.1:7788/v1/audio/speech";
    std::string model, extraJson;
    int timeoutSec = 60;
};

struct CommandTtsOptions {
    std::wstring path;
    std::wstring args;  // {text} {text_file} {out} {voice} {speed}
    bool stdinText = false;
    int timeoutSec = 60;
};

std::unique_ptr<ITtsProvider> CreateWinRtTts();                       // MTA 스레드에서 만들고 쓴다
std::unique_ptr<ITtsProvider> CreateHttpTts(HttpTtsOptions options);
std::unique_ptr<ITtsProvider> CreateCommandTts(CommandTtsOptions options);
// CommandLineToArgvW 규칙으로 인자 1개를 따옴표 처리 (테스트용 공개)
std::wstring QuoteArg(const std::wstring& arg);
// 명령 인수 템플릿을 한 번에 치환한다 (치환한 값 안의 {…} 는 다시 보지 않음). 실패하면 빈 문자열 + error
// - 실행 파일: 값마다 QuoteArg. 발화·음성이 - 나 / 로 시작하면 앞에 공백을 붙여 옵션으로 읽히지 않게 한다
// - 배치: {text}·{voice} 거부, 나머지 값은 cmd.exe 특수 문자(" % ! ^ & | < > 제어 문자)가 없을 때만 따옴표로
struct CommandArgValues {
    std::wstring text, textFile, out, voice, speed;
};
std::wstring ExpandCommandArgs(const std::wstring& tmpl, const CommandArgValues& values, bool batch, std::string& error);
// RIFF/WAVE 에서 PCM 형식·데이터 위치 (테스트용 공개)
bool ParseWav(const std::vector<uint8_t>& wav, WAVEFORMATEX& fmt, const uint8_t*& data, uint32_t& size);

struct TtsSettings {
    std::string engine = "winrt";
    double volume = 0.8;
    HttpTtsOptions http;
    CommandTtsOptions command;
};

class TtsService {
public:
    TtsService();
    ~TtsService();
    void Start(TtsSettings settings);  // 스레드 시작 (엔진·오디오는 첫 발화 때 만든다)
    void Shutdown();
    bool Running() const { return thread_.joinable(); }
    void Speak(std::string utf8, TtsVoiceSpec voice);
    void StopAll();  // 재생·합성 중지 + 대기열 비우기 (전체화면·잠금·펫 제거·끄기)

private:
    struct Item {
        std::string text;
        TtsVoiceSpec voice;
    };
    void Run();
    bool Play(const TtsAudio& audio);

    TtsSettings settings_;
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    bool quit_ = false;
    HANDLE stopEvent_ = nullptr;  // 재생 중단 (수동 리셋)
    struct Audio;
    std::unique_ptr<Audio> audio_;
    ITtsProvider* active_ = nullptr;  // 합성 중인 엔진 (mu_ 보호, StopAll 이 Cancel)
};

}  // namespace dp
