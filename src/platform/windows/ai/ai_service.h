#pragma once
// AI 작업 스레드 1개 (기준선 §8.1: 네트워크는 제한된 백그라운드 큐). 큐가 비면 이벤트 대기 → 쉬는 동안 기상 0.
// 결과는 PostMessage(notifyWnd, notifyMsg, 0, AiEvent*) 로 UI 스레드에 넘기고, 받는 쪽이 delete 한다.
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "ai/observe.h"
#include "platform/windows/ai/siwc_client.h"

namespace dp {

// 관찰 1회 (P4). 캡처는 작업 스레드에서 capture 콜백으로 한다 (WGC 모듈은 앱이 넘긴다)
struct ObserveJob {
    int petId = -1;
    std::function<bool(std::vector<uint8_t>& jpeg, std::string& error)> capture;
    std::string model, instructions;
    ai::ObserveContext context;
    bool structured = true;
    size_t maxSpeechChars = 120;
    std::vector<std::string> behaviors;
    StreamTimeouts timeouts;
};

struct AiEvent {
    enum class Kind { Account, Models, Delta, StreamDone, SignInDone, SignOutDone, Diagnose, ObserveCaptured, ObserveDone };
    Kind kind = Kind::Account;
    uint64_t requestId = 0;
    int petId = -1;
    std::string text;  // Delta
    AccountInfo account;
    std::vector<ai::ModelInfo> models;
    AiResult result;          // SignIn/SignOut/Models
    StreamResult stream;      // StreamDone, ObserveDone
    // ObserveCaptured: 캡처 성공 여부·오류 코드 (UI 는 이때 캡처 제외 설정을 되돌린다)
    // ObserveDone: text = 모델 출력 전체 (UI 가 검증), structuredRejected = text.format 거부로 안내문 JSON 으로 재시도함
    bool captured = false;
    std::string captureError;
    bool structuredRejected = false, imageRejected = false;
};

class AiService {
public:
    ~AiService();
    bool Start(HWND notifyWnd, UINT notifyMsg, SiwcOptions options);
    void Stop(DWORD timeoutMs = 2000);  // 진행 중 요청 취소 후 join (최대 timeoutMs)
    bool Running() const { return thread_.joinable(); }

    void SignIn();
    void CancelSignIn();
    void SignOut(bool forceLocal);
    void RefreshModels();
    void MarkWelcomeShown();
    // 요청 id 를 돌려준다. 같은 시점에는 스트림 1개만 (새 요청 전에 이전 요청은 취소한다)
    uint64_t Send(int petId, ai::AiRequest request, size_t maxChars, StreamTimeouts timeouts);
    // 같은 단일 요청 슬롯을 쓴다: 대화 Send 가 오면 관찰은 취소된다
    uint64_t Observe(ObserveJob job);
    void DiagnoseRefresh();
    void Cancel(uint64_t requestId);
    uint64_t ActiveRequest() const { return activeRequest_; }
    // UI 스레드: 알림 메시지의 lParam → 이 서비스가 보낸 이벤트일 때만 소유권을 돌려준다.
    // 다른 프로세스가 같은 메시지 번호로 임의 값을 보내도 지우지 않는다 (보안 점검 2026-10-20)
    std::unique_ptr<AiEvent> Take(LPARAM lp);

private:
    void Post(std::unique_ptr<AiEvent> e);
    // onFail: job 이 예외로 끝났을 때 UI 가 상태를 되돌리도록 실패 이벤트를 보낸다 (작업 스레드가 죽지 않게)
    void Enqueue(std::function<void()> job, std::function<void()> onFail = {});
    void Run();
    void PostAccount();
    void PostFailure(AiEvent::Kind kind, uint64_t requestId, int petId);

    HWND wnd_ = nullptr;
    UINT msg_ = 0;
    std::unique_ptr<SiwcClient> client_;  // IAiProvider 의 SIWC 구현 (로그인·로그아웃은 SIWC 전용)
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    struct Job {
        std::function<void()> run, onFail;
    };
    std::deque<Job> jobs_;
    std::mutex postedMu_;
    std::unordered_set<AiEvent*> posted_;  // 보냈고 아직 UI 가 받지 않은 이벤트
    bool stop_ = false;
    std::atomic<uint64_t> nextRequest_{1};
    std::atomic<uint64_t> activeRequest_{0};
    std::atomic<bool> signingIn_{false};
};

}  // namespace dp
