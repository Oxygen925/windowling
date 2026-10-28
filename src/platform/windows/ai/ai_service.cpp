#include "platform/windows/ai/ai_service.h"

#include <dwmapi.h>
#include <objbase.h>

#include "diagnostics/log.h"

namespace dp {

AiService::~AiService() {
    Stop();
    std::lock_guard lock(postedMu_);  // UI 가 받지 못한 채 남은 이벤트
    for (AiEvent* e : posted_) delete e;
    posted_.clear();
}

bool AiService::Start(HWND notifyWnd, UINT notifyMsg, SiwcOptions options) {
    if (thread_.joinable()) return true;
    wnd_ = notifyWnd;
    msg_ = notifyMsg;
    client_ = std::make_unique<SiwcClient>(std::move(options));
    stop_ = false;
    thread_ = std::thread([this] { Run(); });
    Enqueue([this] {
        client_->Load();
        PostAccount();
    });
    return true;
}

void AiService::Stop(DWORD timeoutMs) {
    if (!thread_.joinable()) return;
    {
        std::lock_guard lock(mu_);
        stop_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    if (client_) {
        client_->CancelStream();
        client_->CancelSignIn();
    }
    // 종료 5초 조건을 지키기 위해 기다리는 시간을 제한한다 (취소된 WinHTTP 호출은 곧 반환됨)
    if (WaitForSingleObject(static_cast<HANDLE>(thread_.native_handle()), timeoutMs) == WAIT_OBJECT_0) {
        thread_.join();
        client_.reset();
    } else {
        log::Warn("[ai] 작업 스레드가 {}ms 안에 끝나지 않음 — 분리하고 종료 진행", timeoutMs);
        thread_.detach();
        (void)client_.release();  // 스레드가 아직 쓰는 중일 수 있어 해제하지 않는다 (프로세스 종료로 회수)
    }
}

void AiService::Enqueue(std::function<void()> job, std::function<void()> onFail) {
    {
        std::lock_guard lock(mu_);
        if (stop_) return;
        jobs_.push_back({std::move(job), std::move(onFail)});
    }
    cv_.notify_one();
}

void AiService::Post(std::unique_ptr<AiEvent> e) {
    if (!wnd_) return;
    {
        std::lock_guard lock(postedMu_);
        posted_.insert(e.get());
    }
    if (!PostMessageW(wnd_, msg_, 0, reinterpret_cast<LPARAM>(e.get()))) {
        std::lock_guard lock(postedMu_);
        posted_.erase(e.get());
        return;  // 실패하면 unique_ptr 가 해제
    }
    (void)e.release();  // 소유권은 UI 스레드로 (Take)
}

std::unique_ptr<AiEvent> AiService::Take(LPARAM lp) {
    auto* e = reinterpret_cast<AiEvent*>(lp);
    std::lock_guard lock(postedMu_);
    if (!posted_.erase(e)) return nullptr;
    return std::unique_ptr<AiEvent>(e);
}

void AiService::PostAccount() {
    auto e = std::make_unique<AiEvent>();
    e->kind = AiEvent::Kind::Account;
    e->account = client_->Info();
    Post(std::move(e));
}

void AiService::Run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);  // ShellExecute 용
    // 비실시간 스레드: EcoQoS 힌트 (ADR-08, 기준선 §8.1). E코어 배치는 보장되지 않는다
    THREAD_POWER_THROTTLING_STATE pt{};
    pt.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    pt.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    pt.StateMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    if (!SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &pt, sizeof(pt)))
        log::Warn("[ai] EcoQoS 설정 실패 err={}", GetLastError());
    SetThreadDescription(GetCurrentThread(), L"DesktopPet.AI");
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        // 외부 데이터 처리 중 예외가 나도 스레드(=앱)가 죽지 않게: 기록하고 실패 이벤트로 UI 상태를 되돌린다
        try {
            job.run();
        } catch (const std::exception& ex) {
            log::Error("[ai] 작업 예외 — 실패로 처리: {}", ex.what());
            if (job.onFail) job.onFail();
        } catch (...) {
            log::Error("[ai] 작업 예외(알 수 없음) — 실패로 처리");
            if (job.onFail) job.onFail();
        }
    }
    CoUninitialize();
}

void AiService::SignIn() {
    if (signingIn_.exchange(true)) return;  // 이미 진행 중
    client_->PrepareSignIn();  // 이 요청 이후의 취소는 작업이 시작되기 전이어도 반영된다
    Enqueue([this] {
        auto e = std::make_unique<AiEvent>();
        e->kind = AiEvent::Kind::SignInDone;
        e->result = client_->SignIn();
        signingIn_ = false;
        e->account = client_->Info();
        Post(std::move(e));
    }, [this] { PostFailure(AiEvent::Kind::SignInDone, 0, -1); });
}

void AiService::CancelSignIn() {
    if (client_) client_->CancelSignIn();
}

void AiService::SignOut(bool forceLocal) {
    if (const uint64_t a = activeRequest_.load()) Cancel(a);  // 로그아웃: 먼저 요청 중지
    Enqueue([this, forceLocal] {
        auto e = std::make_unique<AiEvent>();
        e->kind = AiEvent::Kind::SignOutDone;
        e->result = client_->SignOut(forceLocal);
        e->account = client_->Info();
        Post(std::move(e));
    }, [this] { PostFailure(AiEvent::Kind::SignOutDone, 0, -1); });
}

void AiService::RefreshModels() {
    Enqueue([this] {
        auto e = std::make_unique<AiEvent>();
        e->kind = AiEvent::Kind::Models;
        e->result = client_->ListModels(e->models);
        e->account = client_->Info();
        Post(std::move(e));
    }, [this] { PostFailure(AiEvent::Kind::Models, 0, -1); });
}

void AiService::MarkWelcomeShown() {
    Enqueue([this] { client_->MarkWelcomeShown(); });
}

void AiService::DiagnoseRefresh() {
    Enqueue([this] {
        auto e = std::make_unique<AiEvent>();
        e->kind = AiEvent::Kind::Diagnose;
        size_t count = 0;
        e->result = client_->DiagnoseRefresh(count);
        e->models.resize(count);
        e->account = client_->Info();
        Post(std::move(e));
    }, [this] { PostFailure(AiEvent::Kind::Diagnose, 0, -1); });
}

uint64_t AiService::Send(int petId, ai::AiRequest request, size_t maxChars, StreamTimeouts timeouts) {
    if (const uint64_t a = activeRequest_.load()) Cancel(a);
    const uint64_t id = nextRequest_++;
    activeRequest_ = id;
    Enqueue([this, id, petId, request = std::move(request), maxChars, timeouts]() mutable {
        auto done = std::make_unique<AiEvent>();
        done->kind = AiEvent::Kind::StreamDone;
        done->requestId = id;
        done->petId = petId;
        for (int pass = 0; pass < 2; ++pass) {
            client_->BeginRequest();
            if (activeRequest_ != id) {  // 큐에서 기다리는 동안 취소됨
                done->stream.outcome = StreamOutcome::Cancelled;
                break;
            }
            done->stream = client_->Stream(request, maxChars, timeouts, [&](std::string_view delta) {
                auto d = std::make_unique<AiEvent>();
                d->kind = AiEvent::Kind::Delta;
                d->requestId = id;
                d->petId = petId;
                d->text = std::string(delta);
                Post(std::move(d));
            });
            const auto& r = done->stream;
            // 고른 effort 를 이 모델이 받지 않음 (글자를 받기 전 400) → effort 없이 한 번 더
            if (r.outcome == StreamOutcome::Failed && r.http == 400 && r.chars == 0 && !request.reasoningEffort.empty() &&
                r.param.rfind("reasoning", 0) == 0) {
                request.reasoningEffort.clear();
                done->effortRejected = true;
                continue;
            }
            break;
        }
        uint64_t expected = id;
        activeRequest_.compare_exchange_strong(expected, 0);
        done->account = client_->Info();
        Post(std::move(done));
    }, [this, id, petId] { PostFailure(AiEvent::Kind::StreamDone, id, petId); });
    return id;
}

uint64_t AiService::Observe(ObserveJob job) {
    if (const uint64_t a = activeRequest_.load()) Cancel(a);
    const uint64_t id = nextRequest_++;
    activeRequest_ = id;
    Enqueue([this, id, job = std::move(job)]() mutable {
        auto done = std::make_unique<AiEvent>();
        done->kind = AiEvent::Kind::ObserveDone;
        done->requestId = id;
        done->petId = job.petId;
        auto finish = [&] {
            uint64_t expected = id;
            activeRequest_.compare_exchange_strong(expected, 0);
            done->account = client_->Info();
            Post(std::move(done));
        };
        // 1) 캡처 (UI 가 캡처 제외를 켠 직후: DWM 이 다음 프레임에 반영하도록 두 번 기다린다)
        std::vector<uint8_t> jpeg;
        auto cap = std::make_unique<AiEvent>();
        cap->kind = AiEvent::Kind::ObserveCaptured;
        cap->requestId = id;
        cap->petId = job.petId;
        if (activeRequest_ != id) {
            cap->captureError = "cancelled";
        } else {
            DwmFlush();
            DwmFlush();
            cap->captured = job.capture && job.capture(jpeg, cap->captureError);
        }
        const bool captured = cap->captured;
        done->captureError = cap->captureError;
        Post(std::move(cap));
        if (!captured || activeRequest_ != id) {
            SecureZeroMemory(jpeg.data(), jpeg.size());
            done->stream.outcome = activeRequest_ != id ? StreamOutcome::Cancelled : StreamOutcome::Failed;
            done->stream.code = captured ? "" : "capture_failed";
            return finish();
        }
        // 2) 요청: 이미지는 data URL 로만 (디스크에 쓰지 않음), 다 쓰면 지운다
        std::string dataUrl = "data:image/jpeg;base64," + ai::Base64Encode(jpeg.data(), jpeg.size());
        SecureZeroMemory(jpeg.data(), jpeg.size());
        const size_t maxModelChars = 2000;  // 모델 출력(JSON) 상한. 발화 길이는 검증에서 자른다
        for (int pass = 0; pass < 2; ++pass) {
            auto request = ai::BuildObserveRequest(job.model, job.instructions, job.context, dataUrl, job.structured,
                                                   job.maxSpeechChars, job.behaviors);
            request.reasoningEffort = job.reasoningEffort;
            client_->BeginRequest();
            if (activeRequest_ != id) {
                done->stream.outcome = StreamOutcome::Cancelled;
                break;
            }
            done->text.clear();
            done->stream = client_->Stream(request, maxModelChars, job.timeouts,
                                           [&](std::string_view delta) { done->text += delta; });
            SecureZeroMemory(request.imageDataUrl.data(), request.imageDataUrl.size());
            const auto& r = done->stream;
            const bool badRequest = r.outcome == StreamOutcome::Failed && r.http == 400;
            if (badRequest && !job.reasoningEffort.empty() && r.param.rfind("reasoning", 0) == 0) {
                job.reasoningEffort.clear();  // effort 거부 → 빼고 한 번 더
                done->effortRejected = true;
                continue;
            }
            if (badRequest && job.structured && r.param.rfind("text", 0) == 0) {
                job.structured = false;  // 구조화 출력 거부 → 안내문 JSON 으로 한 번 더
                done->structuredRejected = true;
                continue;
            }
            if (badRequest && r.param.find("image") != std::string::npos) done->imageRejected = true;
            break;
        }
        SecureZeroMemory(dataUrl.data(), dataUrl.size());
        finish();
    }, [this, id, petId = job.petId] {
        PostFailure(AiEvent::Kind::ObserveCaptured, id, petId);  // UI 가 캡처 제외를 되돌린다
        PostFailure(AiEvent::Kind::ObserveDone, id, petId);
    });
    return id;
}

void AiService::PostFailure(AiEvent::Kind kind, uint64_t requestId, int petId) {
    if (kind == AiEvent::Kind::SignInDone) signingIn_ = false;
    if (requestId) {
        uint64_t expected = requestId;
        activeRequest_.compare_exchange_strong(expected, 0);
    }
    auto e = std::make_unique<AiEvent>();
    e->kind = kind;
    e->requestId = requestId;
    e->petId = petId;
    e->result.code = "internal_error";
    e->stream.outcome = StreamOutcome::Failed;
    e->stream.code = "internal_error";
    e->captureError = "internal_error";
    try {
        e->account = client_->Info();
    } catch (...) {
    }
    Post(std::move(e));
}

void AiService::Cancel(uint64_t requestId) {
    uint64_t expected = requestId;
    if (activeRequest_.compare_exchange_strong(expected, 0) && client_) client_->CancelStream();
}

}  // namespace dp
