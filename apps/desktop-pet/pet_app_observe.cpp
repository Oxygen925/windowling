// PetApp 의 화면 관찰·자율 발화·TTS 부분 (P4, P4 계획 §2).
// 이미지·모델 응답 원문·창 제목·실행 파일 이름은 로그에 남기지 않는다 (사유 코드·크기·시간만).
#include "pet_app.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>

#include <shellapi.h>

#include <nlohmann/json.hpp>

#include "diagnostics/log.h"
#include "platform/windows/capture/screen_capture.h"
#include "platform/windows/observe_signals.h"
#include "platform/windows/strings.h"
#include "ui/text.h"
#include "ai/json_util.h"
#include "dp_version.h"

namespace dp {

namespace {

// 동의 문구·전송 범위가 바뀌면 올린다 → 저장된 동의가 무효가 되어 다시 묻는다 (SIWC Terms: 백그라운드 사용 전 명시적 동의)
// 2026-10-19: 테두리 문장을 트레이 표시 문장으로 바꿨지만 전송 범위는 같아 버전은 유지 (사용자 결정)
constexpr int kObserveConsentVersion = 1;
constexpr double kNoTime = 1e300;

bool Contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

int LocalDay() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wYear * 400 + st.wMonth * 32 + st.wDay;
}

double SecondsToLocalMidnight() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return 86400.0 - (st.wHour * 3600.0 + st.wMinute * 60.0 + st.wSecond);
}

std::string LocalTimeHHMM() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return std::format("{:02}:{:02}", st.wHour, st.wMinute);
}

double UnixNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<double>(t) / 1e7 - 11644473600.0;
}

nlohmann::json ScopeToJson(const ai::ObserveScope& s) {
    return {{"max_per_hour", s.maxPerHour}, {"max_per_day", s.maxPerDay}, {"interval_min", s.intervalMinMin},
            {"interval_max", s.intervalMaxMin}, {"require_activity_min", s.requireActivityMin},
            {"image_max_px", s.imageMaxPx}, {"send_app_name", s.sendAppName}, {"exclude_exe", s.excludeExe}};
}

ai::ObserveScope ScopeFromJson(const nlohmann::json& j) {
    const core::ObserveConfig d;  // 없는 값은 기본값 (예전 상태 파일)
    ai::ObserveScope s;
    s.maxPerHour = static_cast<int>(ai::JInt(j, "max_per_hour", d.maxPerHour));
    s.maxPerDay = static_cast<int>(ai::JInt(j, "max_per_day", d.maxPerDay));
    s.intervalMinMin = static_cast<int>(ai::JInt(j, "interval_min", d.intervalMinMin));
    s.intervalMaxMin = static_cast<int>(ai::JInt(j, "interval_max", d.intervalMaxMin));
    s.requireActivityMin = static_cast<int>(ai::JInt(j, "require_activity_min", d.requireActivityMin));
    s.imageMaxPx = static_cast<int>(ai::JInt(j, "image_max_px", d.imageMaxPx));
    s.sendAppName = ai::JBool(j, "send_app_name", d.sendAppName);
    s.excludeExe = d.excludeExe;
    if (const auto* list = ai::JField(j, "exclude_exe"); list && list->is_array()) {
        s.excludeExe.clear();
        for (const auto& v : *list)
            if (v.is_string()) s.excludeExe.push_back(v.get<std::string>());
    }
    return s;
}

std::string UtcNowIso() {
    SYSTEMTIME st;
    GetSystemTime(&st);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

}  // namespace

std::vector<std::string> PetApp::ObserveBehaviors(const PetActor& pet) const {
    // 모델이 요청할 수 있는 행동: 설정 [observe] allowed_behaviors 중 이 캐릭터에 클립이 있는 것 (기준선 §7.3 허용 목록)
    std::vector<std::string> out;
    for (const auto& name : config_.observe.allowedBehaviors)
        if (pet.asset->pkg.sprite.clips.contains(name) || pet.asset->pkg.model.animations.contains(name)) out.push_back(name);
    return out;
}

ai::ObserveScope PetApp::ScopeOfConfig() const {
    const auto& o = config_.observe;
    return {o.maxPerHour, o.maxPerDay, o.intervalMinMin, o.intervalMaxMin, o.requireActivityMin, o.imageMaxPx, o.sendAppName,
            o.excludeExe};
}

// 동의 뒤 설정(설정 창·설정 파일)으로 범위가 넓어졌으면 끈다. 다시 켜면 동의 창이 새 값으로 다시 묻는다
bool PetApp::TurnOffIfScopeWidened(const char* when) {
    if (!observeOn_) return false;
    const auto wider = ai::WiderScope(consentScope_, ScopeOfConfig());
    if (wider.empty()) return false;
    std::string keys;
    for (const auto& k : wider) keys += (keys.empty() ? "" : ",") + k;
    observeOn_ = false;
    ObserveInterrupted("scope_widened");
    SaveObserveState();
    log::Warn("[observe] 동의 뒤 범위가 넓어짐 ({}) when={} — 관찰 끔, 다시 켜면 동의를 다시 묻는다", keys, when);
    UpdateObserveIndicators();
    return true;
}

void PetApp::InitObserve(double now) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    observeRand_ ^= static_cast<uint64_t>(t.QuadPart) * 0x2545F4914F6CDD1Dull;
    const auto path = configPath_.parent_path() / L"observe_state.json";
    std::ifstream in(path, std::ios::binary);
    if (in) {
        std::stringstream ss;
        ss << in.rdbuf();
        const auto j = nlohmann::json::parse(ss.str(), nullptr, false);
        if (j.is_object()) {
            observeConsent_ = static_cast<int>(ai::JInt(j, "consent_version", 0));
            observeConsentAt_ = ai::JStr(j, "consented_at", "");
            const bool enabled = ai::JBool(j, "enabled", false);
            if (j.contains("sent_unix") && j["sent_unix"].is_array()) {  // 최근 24시간 전송 시각 → 단조 시계로
                std::vector<double> times;
                const double unixNow = UnixNow();
                for (const auto& v : j["sent_unix"])
                    if (v.is_number()) times.push_back(now - (unixNow - v.get<double>()));
                observeCounter_.Restore(std::move(times), now);
            }
            // 일시정지는 다시 시작해도 유지 (-1 = 다시 켤 때까지, 그 밖에는 끝나는 시각)
            const int64_t pausedUntil = ai::JInt(j, "paused_until_unix", 0);
            if (pausedUntil < 0) observePausedUntil_ = -1;
            else if (pausedUntil > 0 && static_cast<double>(pausedUntil) > UnixNow())
                observePausedUntil_ = now + (static_cast<double>(pausedUntil) - UnixNow());
            // 동의한 범위. 예전 상태 파일(범위 없음)은 기본값에 동의한 것으로 본다
            consentScope_ = ScopeFromJson(j.contains("scope") ? j["scope"] : nlohmann::json::object());
            observeOn_ = enabled && observeConsent_ == kObserveConsentVersion;
            if (enabled && !observeOn_)
                log::Warn("[observe] 동의 버전이 바뀌어({} → {}) 꺼진 상태로 시작 — 트레이에서 다시 동의해야 함", observeConsent_,
                          kObserveConsentVersion);
        }
    }
    TurnOffIfScopeWidened("start");
    log::Info("[observe] state on={} consent_version={} paused={} capture_supported={}", observeOn_, observeConsent_,
              observePausedUntil_ != 0, observeOn_ ? CaptureSupported() : false);
    UpdateObserveIndicators();
    ScheduleObserve(now);
}

void PetApp::SaveObserveState() {
    const auto path = configPath_.parent_path() / L"observe_state.json";
    nlohmann::json sent = nlohmann::json::array();
    const double now = Now(), unixNow = UnixNow();
    for (double t : observeCounter_.Times()) sent.push_back(std::floor(unixNow - (now - t)));
    const int64_t pausedUntil = observePausedUntil_ < 0 ? -1
                                : observePausedUntil_ > now ? static_cast<int64_t>(std::ceil(unixNow + (observePausedUntil_ - now)))
                                                            : 0;
    const nlohmann::json j = {{"enabled", observeOn_},
                              {"consent_version", observeConsent_},
                              {"consented_at", observeConsentAt_},
                              {"scope", ScopeToJson(consentScope_)},
                              {"paused_until_unix", pausedUntil},
                              {"sent_unix", sent},  // 시간당·하루 상한용 (재실행해도 유지)
                              {"note", "Desktop Pet 화면 관찰 동의 기록. 트레이 메뉴로 바꾼다."}};
    const auto tmp = path.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << j.dump(2);
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        log::Warn("[observe] 상태 저장 실패 err={}", GetLastError());
}

void PetApp::UpdateObserveIndicators() {
    const double now = Now();
    const bool paused = observePausedUntil_ < 0 || observePausedUntil_ > now;
    tray_.SetTip(((observeReq_ ? ui::T("tray.tip_observing") : ui::T("tray.tip")) + DP_CHANNEL_SUFFIX).c_str());
    chat_.SetObserveNote(!observeOn_ ? L"" : paused ? ui::T("chat.observe_paused") : ui::T("chat.observe_on"));
}

void PetApp::ScheduleObserve(double now) {
    const bool test = options_.observeIntervalSec > 0 && !options_.aiEndpoint.empty();
    const double minSec = test ? options_.observeIntervalSec : config_.observe.intervalMinMin * 60.0;
    const double maxSec = test ? options_.observeIntervalSec : config_.observe.intervalMaxMin * 60.0;
    observeSchedule_.Configure(minSec, maxSec);
    if (observeOn_) {
        observeRand_ ^= observeRand_ << 13;
        observeRand_ ^= observeRand_ >> 7;
        observeRand_ ^= observeRand_ << 17;
        observeSchedule_.ScheduleNext(now, static_cast<double>(observeRand_ >> 11) / 9007199254740992.0);
    } else {
        observeSchedule_.Clear();
    }
    double at = observeSchedule_.Armed() ? observeSchedule_.NextAt() : kNoTime;
    if (captureExcluded_) at = std::min(at, captureExcludeUntil_);
    if (at < kNoTime) deadlines_.Set(kDlObserveKey, at);
    else deadlines_.Clear(kDlObserveKey);
}

bool PetApp::SetCaptureExcluded(bool on) {
    if (on == captureExcluded_) return true;
    if (!on) excludeActive_->store(false);  // 되돌리기 전에 내린다 → 작업 스레드는 그 뒤 캡처를 버린다
    // 캡처 순간에만 펫·말풍선·대화창을 캡처에서 뺀다 (2026-10-14 결정). 평소 스크린샷·화면 공유에는 펫이 보인다
    const DWORD affinity = on ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    int failed = 0;
    auto apply = [&](HWND h) {
        if (h && !SetWindowDisplayAffinity(h, affinity)) ++failed;
    };
    for (auto& p : pets_) {
        apply(p->hwnd);
        if (p->bubble) apply(p->bubble->Hwnd());
    }
    apply(chat_.Hwnd());
    if (on && failed) {
        // 하나라도 제외하지 못했으면 캡처하지 않는다 (펫·말풍선·대화창이 이미지에 들어갈 수 있음)
        log::Warn("[observe] SetWindowDisplayAffinity 실패 {}개 err={} — 이번 관찰 건너뜀", failed, GetLastError());
        for (auto& p : pets_) {
            if (p->hwnd) SetWindowDisplayAffinity(p->hwnd, WDA_NONE);
            if (p->bubble && p->bubble->Hwnd()) SetWindowDisplayAffinity(p->bubble->Hwnd(), WDA_NONE);
        }
        if (chat_.Hwnd()) SetWindowDisplayAffinity(chat_.Hwnd(), WDA_NONE);
        return false;
    }
    captureExcluded_ = on;
    if (on) excludeActive_->store(true);  // 모두 적용한 뒤 올린다
    if (on) {
        captureExcludeUntil_ = Now() + 5.0;  // 캡처 완료 이벤트가 오지 않아도 5초 뒤 되돌린다
        deadlines_.Set(kDlObserveKey, std::min(captureExcludeUntil_, observeSchedule_.Armed() ? observeSchedule_.NextAt() : kNoTime));
    }
    if (failed) log::Warn("[observe] SetWindowDisplayAffinity 실패 {}개 on={} err={}", failed, on, GetLastError());
    return true;
}

void PetApp::ObserveInterrupted(const char* reason) {
    if (observeReq_) {
        log::Info("[observe] cancel request={} reason={}", observeReq_, reason);
        ai_.Cancel(observeReq_);
        observeReq_ = 0;
        observePetId_ = -1;
    }
    if (captureExcluded_) SetCaptureExcluded(false);
    if (std::string_view(reason) != "chat") tts_.StopAll();
    UpdateObserveIndicators();
}

void PetApp::ObserveTick(double now) {
    if (captureExcluded_ && now >= captureExcludeUntil_) {
        log::Warn("[observe] 캡처 완료 신호가 없어 캡처 제외를 되돌림");
        SetCaptureExcluded(false);
    }
    if (observePausedUntil_ > 0 && now >= observePausedUntil_) {
        observePausedUntil_ = 0;
        SaveObserveState();
        log::Info("[observe] 일시정지 끝");
        UpdateObserveIndicators();
    }
    if (!observeSchedule_.Due(now)) return ScheduleObserveDeadlineOnly(now);

    // ---- 시도 1회: Privacy Guard → 캡처·요청 ----
    const char* skip = nullptr;
    PetActor* pet = nullptr;
    std::vector<PetActor*> candidates;
    for (auto& p : pets_)
        if (p->hwnd && !p->hiddenFullscreen && !p->isBug) candidates.push_back(p.get());
    if (candidates.size() > 1)  // 같은 펫이 연달아 말하지 않게
        std::erase_if(candidates, [this](PetActor* p) { return p->id == lastObservePet_; });
    if (!candidates.empty()) {
        observeRand_ ^= observeRand_ << 13;
        observeRand_ ^= observeRand_ >> 7;
        observeRand_ ^= observeRand_ << 17;
        pet = candidates[observeRand_ % candidates.size()];
    }
    const HMONITOR mon = pet ? MonitorFromWindow(pet->hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;

    ai::GuardInput g;
    g.enabled = observeOn_;
    g.consented = observeConsent_ == kObserveConsentVersion;
    g.signedIn = aiEnabled_ && account_.state == AccountState::SignedIn;
    g.planUsage = account_.planUsage;
    g.imageSupported = !Contains(imageRejected_, modelSlug_);
    g.usageLimited = usageLimitedDay_ == LocalDay();
    g.paused = observePausedUntil_ < 0 || observePausedUntil_ > now;
    g.locked = Suspended();
    g.fullscreen = !pets_.empty() && !pet;  // 펫이 모두 전체화면 숨김
    g.notificationsBusy = NotificationsBusy();
    g.excludedAppVisible = mon && ExcludedAppVisibleOn(mon, config_.observe.excludeExe);
    g.idleSec = IdleSeconds();
    g.chatBusy = activeReq_ != 0 || signingIn_ || observeReq_ != 0 || (chat_.Hwnd() && GetForegroundWindow() == chat_.Hwnd());
    const ai::GuardLimits lim{config_.observe.maxPerHour, config_.observe.maxPerDay,
                              config_.observe.requireActivityMin * 60.0};
    skip = ai::EvaluateGuard(g, observeCounter_, lim, now);
    if (!skip && pets_.empty()) skip = "no_pet";
    if (!skip && modelSlug_.empty()) {
        skip = "no_model";
        RequestModelsIfNeeded();
    }
    if (skip) {
        lastObserveSkip_ = skip;
        log::Info("[observe] skip reason={}", skip);
        ScheduleObserve(now);
        return;
    }

    // 캡처 순간 펫 제외 → 작업 스레드에서 DWM 반영 대기 후 1프레임 (ObserveCaptured 에서 되돌림)
    const bool requireExclusion = !(options_.observeNoExclude && !options_.aiEndpoint.empty());
    if (!requireExclusion) {
        log::Warn("[observe] 테스트: 캡처 제외 끔 (대조 시험)");
    } else if (!SetCaptureExcluded(true)) {
        lastObserveSkip_ = "exclude_failed";
        log::Info("[observe] skip reason=exclude_failed");
        ScheduleObserve(now);
        return;
    }
    ObserveJob job;
    job.petId = pet->id;
    const CaptureOptions co{config_.observe.imageMaxPx, config_.observe.jpegQuality, 1000};
    const std::wstring dump = options_.aiEndpoint.empty() ? L"" : options_.observeDump;  // 테스트 전용
    // 작업 스레드에서 실행: 판정(UI 스레드)과 캡처 사이에 바뀐 것을 캡처 앞뒤로 다시 확인한다 —
    // 캡처 제외가 그대로 켜져 있는지(5초 안전망으로 되돌려졌으면 버림), 그 모니터에 제외 앱이 새로 보이는지
    job.capture = [mon, co, dump, requireExclusion, excluded = excludeActive_, apps = config_.observe.excludeExe](
                      std::vector<uint8_t>& jpeg, std::string& err) {
        auto recheck = [&]() -> const char* {
            if (requireExclusion && !excluded->load()) return "exclude_inactive";
            if (ExcludedAppVisibleOn(mon, apps)) return "excluded_app";
            return nullptr;
        };
        if (const char* why = recheck()) {
            err = why;
            log::Info("[observe] capture skipped code={}", why);
            return false;
        }
        CaptureResult r = CaptureMonitorJpeg(mon, co);
        if (!r.ok) {
            err = r.error;
            log::Warn("[observe] capture failed code={} ms={:.1f}", r.error, r.captureMs);
            return false;
        }
        if (const char* why = recheck()) {
            SecureZeroMemory(r.jpeg.data(), r.jpeg.size());
            err = why;
            log::Info("[observe] capture discarded code={}", why);
            return false;
        }
        log::Info("[observe] capture ok src={}x{} out={}x{} bytes={} capture_ms={:.1f} encode_ms={:.1f}", r.srcWidth,
                  r.srcHeight, r.width, r.height, r.jpeg.size(), r.captureMs, r.encodeMs);
        if (!dump.empty()) std::ofstream(dump, std::ios::binary).write(reinterpret_cast<const char*>(r.jpeg.data()), r.jpeg.size());
        jpeg = std::move(r.jpeg);
        return true;
    };
    const auto& pkg = pet->asset->pkg;
    job.behaviors = ObserveBehaviors(*pet);
    job.maxSpeechChars = static_cast<size_t>(config_.observe.maxSpeechChars);
    job.model = modelSlug_;
    job.reasoningEffort = config_.ai.reasoningEffort;
    job.instructions = ai::BuildObserveInstructions(pkg.name, pkg.persona, job.maxSpeechChars, job.behaviors);
    job.context.appName = config_.observe.sendAppName ? ForegroundExeOn(mon) : "";
    job.context.localTime = LocalTimeHHMM();
    job.context.locale = UserLocaleName();
    job.structured = !Contains(structuredRejected_, modelSlug_);
    job.timeouts = {config_.ai.firstTokenTimeoutSec, config_.ai.requestTimeoutSec};
    const bool structured = job.structured, withApp = !job.context.appName.empty();
    observeReq_ = ai_.Observe(std::move(job));
    observePetId_ = pet->id;
    lastObservePet_ = pet->id;
    observeCounter_.RecordSent(now);  // 상한은 전송 기준
    SaveObserveState();
    lastObserveSkip_.clear();
    log::Info("[observe] start pet={} request={} structured={} app_name={} sent_1h={} sent_24h={}", pet->id, observeReq_,
              structured, withApp, observeCounter_.SentWithin(now, 3600), observeCounter_.SentWithin(now, 86400));
    UpdateObserveIndicators();
    ScheduleObserve(now);
}

void PetApp::ScheduleObserveDeadlineOnly(double now) {
    (void)now;
    double at = observeSchedule_.Armed() ? observeSchedule_.NextAt() : kNoTime;
    if (captureExcluded_) at = std::min(at, captureExcludeUntil_);
    if (observePausedUntil_ > 0) at = std::min(at, observePausedUntil_);
    if (at < kNoTime) deadlines_.Set(kDlObserveKey, at);
    else deadlines_.Clear(kDlObserveKey);
}

void PetApp::OnObserveEvent(std::unique_ptr<AiEvent> e) {
    const double now = Now();
    if (e->kind == AiEvent::Kind::ObserveCaptured) {
        if (captureExcluded_ && (observeReq_ == 0 || e->requestId == observeReq_)) SetCaptureExcluded(false);
        ScheduleObserveDeadlineOnly(now);
        return;
    }
    if (e->requestId != observeReq_) return;  // 취소된 관찰
    observeReq_ = 0;
    observePetId_ = -1;
    account_ = e->account;
    const auto& r = e->stream;
    if (e->structuredRejected && !Contains(structuredRejected_, modelSlug_)) {
        structuredRejected_.push_back(modelSlug_);
        log::Warn("[observe] 이 모델이 구조화 출력(text.format)을 거부 — 안내문 JSON 으로 전환");
    }
    if (e->imageRejected && !Contains(imageRejected_, modelSlug_)) {
        imageRejected_.push_back(modelSlug_);
        log::Warn("[observe] 이 모델이 이미지 입력을 거부 — 이 모델에서는 관찰 중지");
        if (chat_.Hwnd()) chat_.SetStatus(ui::T("status.image_unsupported"));
    }
    if (r.recovery == ai::Recovery::UsageLimit) usageLimitedDay_ = LocalDay();

    PetActor* p = PetById(e->petId);
    std::string result = "failed";
    ai::ObserveReply reply;
    if (r.outcome == StreamOutcome::Completed && p) {
        reply = ai::ParseObserveReply(e->text, ObserveBehaviors(*p), static_cast<size_t>(config_.observe.maxSpeechChars));
        if (!reply.ok) result = "invalid_reply";
        else if (!reply.speak) result = "silent";
        else result = "spoke";
    } else if (r.outcome == StreamOutcome::Cancelled) {
        result = "cancelled";
    }
    SecureZeroMemory(e->text.data(), e->text.size());

    if (result == "spoke") {
        if (!p->bubble) p->bubble = std::make_unique<SpeechBubble>();
        p->bubble->SetText(ToWide(reply.text));
        p->bubble->hideAt = now + config_.ai.bubbleSeconds + 2;
        p->conv.SetMaxTurns(config_.ai.historyTurns);
        p->conv.AddAssistant(reply.text);  // 이미지는 남기지 않고 발화 문장만 (P4-R9)
        if (chatPetId_ == p->id && chat_.Hwnd()) RenderChatHistory();
        if (!reply.behavior.empty() && p->fsm) {
            const int dir = reply.behavior == "walk" ? ((observeRand_ >> 7) & 1 ? 1 : -1) : 0;
            p->fsm->Force({reply.behavior, 4.0, dir}, now);
        }
        SpeakTts(*p, reply.text);
    }
    log::Info("[observe] done pet={} request={} outcome={} result={} reason={} http={} code={} chars={} emotion={} "
              "behavior={} capture={} request_id={}",
              e->petId, e->requestId, ToString(r.outcome), result, reply.error, r.http, r.code, ai::Utf8Length(reply.text),
              reply.emotion, reply.behavior.empty() ? "none" : reply.behavior,
              e->captureError.empty() ? "ok" : e->captureError, r.requestId);
    SecureZeroMemory(reply.text.data(), reply.text.size());
    UpdateObserveIndicators();
    ScheduleObserveDeadlineOnly(now);
}

void PetApp::ToggleObserve() {
    if (observeOn_) {
        observeOn_ = false;
        SaveObserveState();
        ObserveInterrupted("disabled");
        ScheduleObserve(Now());
        log::Info("[observe] disabled");
        return;
    }
    if (!aiEnabled_) {
        MessageBoxW(appWnd_.get(), ui::T("observe.need_ai").c_str(), ui::T("observe.need_ai_title").c_str(),
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        return;
    }
    if (consent_.IsOpen()) {  // 이미 묻는 중
        consent_.BringToFront();
        return;
    }
    const auto& o = config_.observe;
    std::wstring excluded;
    for (const auto& x : o.excludeExe) excluded += (excluded.empty() ? L"" : L", ") + ToWide(x);
    const std::wstring body =
        ui::TF("observe.consent_body", o.imageMaxPx, o.sendAppName ? ui::T("observe.consent_app_name") : std::wstring(),
               o.intervalMinMin, o.intervalMaxMin, LimitText(o.maxPerHour), LimitText(o.maxPerDay),
               o.requireActivityMin > 0 ? ui::TF("observe.away_min", o.requireActivityMin) : ui::T("observe.away_off"),
               excluded.empty() ? ui::T("observe.consent_no_excluded") : excluded);
    pendingScope_ = ScopeOfConfig();  // 동의 창에 보인 값
    consent_.Ask(body, [this](bool agreed) { OnObserveConsent(agreed); });
}

void PetApp::OnObserveConsent(bool agreed) {
    if (!agreed) {
        log::Info("[observe] consent declined");
        return;
    }
    if (observeOn_) return;
    if (!aiEnabled_) {  // 묻는 동안 AI 를 껐다
        log::Info("[observe] consent ignored ai_off");
        return;
    }
    observeOn_ = true;
    observeConsent_ = kObserveConsentVersion;
    observeConsentAt_ = UtcNowIso();
    consentScope_ = pendingScope_;
    observePausedUntil_ = 0;
    SaveObserveState();
    log::Info("[observe] enabled consent_version={} signed_in={}", observeConsent_, account_.state == AccountState::SignedIn);
    UpdateObserveIndicators();
    ScheduleObserve(Now());
}

void PetApp::PauseObserve(double untilSec, const char* reason) {
    observePausedUntil_ = untilSec;
    SaveObserveState();  // 다시 시작해도 일시정지 유지
    ObserveInterrupted("paused");
    log::Info("[observe] pause {} for={}", reason, untilSec < 0 ? -1.0 : untilSec - Now());
    ScheduleObserveDeadlineOnly(Now());
}

// 관찰 상한 표시: "최대 N회" 또는 "무제한" (0)
std::wstring PetApp::LimitText(int limit) { return limit > 0 ? ui::TF("observe.limit_n", limit) : ui::T("observe.limit_none"); }

void PetApp::ShowObserveStatus() {
    const double now = Now();
    const bool paused = observePausedUntil_ < 0 || observePausedUntil_ > now;
    std::wstring next = L"-";
    if (observeOn_ && observeSchedule_.Armed())
        next = ui::TF("observe.next_in", static_cast<int>(std::max(0.0, observeSchedule_.NextAt() - now) / 60.0 + 0.5));
    const std::wstring text = ui::TF(
        "observe.status_body", observeOn_ ? ui::T("observe.on") : ui::T("observe.off"),
        paused ? ui::T("observe.paused_suffix") : std::wstring(),
        observeConsent_ ? ui::TF("observe.consent_version", observeConsent_, ToWide(observeConsentAt_)) : ui::T("observe.consent_none"),
        observeCounter_.SentWithin(now, 3600), LimitText(config_.observe.maxPerHour), observeCounter_.SentWithin(now, 86400),
        LimitText(config_.observe.maxPerDay), next,
        lastObserveSkip_.empty() ? std::wstring(L"-") : ToWide(lastObserveSkip_), ttsOn_ ? ui::T("observe.on") : ui::T("observe.off"));
    MessageBoxW(appWnd_.get(), text.c_str(), ui::T("observe.status_title").c_str(), MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

void PetApp::AppendObserveMenu(HMENU menu) {
    const double now = Now();
    const bool paused = observePausedUntil_ < 0 || observePausedUntil_ > now;
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING | (observeOn_ ? MF_CHECKED : 0), ID_OBSERVE_TOGGLE,
                (observeOn_ ? ui::T("observe_menu.disable") : ui::T("observe_menu.enable")).c_str());
    const UINT off = observeOn_ ? 0 : MF_GRAYED;
    AppendMenuW(sub, MF_STRING | off, ID_OBSERVE_PAUSE_1H, ui::T("observe_menu.pause_1h").c_str());
    AppendMenuW(sub, MF_STRING | off, ID_OBSERVE_PAUSE_TODAY, ui::T("observe_menu.pause_today").c_str());
    AppendMenuW(sub, MF_STRING | off, ID_OBSERVE_PAUSE_ON, ui::T("observe_menu.pause_until").c_str());
    if (paused) AppendMenuW(sub, MF_STRING, ID_OBSERVE_RESUME, ui::T("observe_menu.resume").c_str());
    AppendMenuW(sub, MF_STRING, ID_OBSERVE_STATUS, ui::T("observe_menu.status").c_str());
    AppendMenuW(sub, MF_STRING, ID_OBSERVE_SETTINGS, ui::T("observe_menu.settings").c_str());
    const std::wstring label = !observeOn_ ? ui::T("observe_menu.root_off") : paused ? ui::T("observe_menu.root_paused")
                                                                                   : ui::T("observe_menu.root_on");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), label.c_str());
    AppendMenuW(menu, MF_STRING | (ttsOn_ ? MF_CHECKED : 0), ID_TTS_TOGGLE, ui::T("menu.tts").c_str());
}

bool PetApp::OnObserveCommand(UINT id) {
    const double now = Now();
    switch (id) {
        case ID_OBSERVE_TOGGLE: ToggleObserve(); return true;
        case ID_OBSERVE_PAUSE_1H: PauseObserve(now + 3600, "1h"); return true;
        case ID_OBSERVE_PAUSE_TODAY: PauseObserve(now + SecondsToLocalMidnight(), "today"); return true;
        case ID_OBSERVE_PAUSE_ON: PauseObserve(-1, "until_resume"); return true;
        case ID_OBSERVE_RESUME:
            observePausedUntil_ = 0;
            SaveObserveState();
            log::Info("[observe] resume");
            UpdateObserveIndicators();
            ScheduleObserve(now);
            return true;
        case ID_OBSERVE_STATUS: ShowObserveStatus(); return true;
        case ID_OBSERVE_SETTINGS: OpenSettings(SettingsWindow::kObserve); return true;
        case ID_TTS_TOGGLE: EnableTts(!ttsOn_); return true;
    }
    return false;
}

void PetApp::EnableTts(bool on) {
    if (on == ttsOn_) return;
    if (on) {
        const auto& t = config_.tts;
        TtsSettings s;
        s.engine = t.engine;
        s.volume = t.volume;
        s.http = {t.httpUrl, t.httpModel, t.httpExtraJson, t.httpTimeoutSec};
        s.command = {ToWide(t.commandPath), ToWide(t.commandArgs), t.commandStdin, t.commandTimeoutSec};
        tts_.Start(std::move(s));  // 엔진은 첫 발화 때 만든다
    } else {
        tts_.Shutdown();
    }
    ttsOn_ = on;
    log::Info("[tts] {} engine={}", on ? "enabled" : "disabled", config_.tts.engine);
}

void PetApp::SpeakTts(const PetActor& pet, const std::string& utf8) {
    if (!ttsOn_ || Suspended()) return;
    // 캐릭터 manifest [voice] 가 있으면 우선, 없으면 전역 [tts] (B12 결정)
    const auto& v = pet.asset->pkg.voice;
    TtsVoiceSpec spec;
    spec.name = v.name.empty() ? config_.tts.voice : v.name;
    spec.aiVoice = v.aiVoice.empty() ? config_.tts.httpVoice : v.aiVoice;
    spec.rate = v.rate > 0 ? v.rate : config_.tts.rate;
    tts_.Speak(utf8, std::move(spec));
}

}  // namespace dp
