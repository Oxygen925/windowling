#pragma once
// Sign in with ChatGPT 클라이언트 (IAiProvider 의 SIWC 구현). 모든 메서드는 AI 작업 스레드에서 호출한다.
// 예외: CancelSignIn·CancelStream 은 UI 스레드에서 호출 가능.
// 비밀 정보(토큰·인가 URL·대화 원문)는 로그에 남기지 않는다 (P3-R13).
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ai/responses.h"
#include "ai/siwc_protocol.h"
#include "platform/windows/ai/ai_provider.h"
#include "platform/windows/ai/http_client.h"
#include "platform/windows/ai/mini_http.h"

namespace dp {

struct SiwcOptions {
    ai::SiwcEndpoints endpoints = ai::SiwcEndpoints::Production();
    std::filesystem::path authDir;           // 자격 증명·호스트 ID 폴더 (설정 폴더\auth)
    std::string agentName = "Desktop Pet";   // P3-R10
    uint16_t callbackPort = 1455;            // P3-R11: 최초 등록은 이 포트만, 다시 로그인은 사용 중이면 빈 포트
    int callbackTimeoutMs = 5 * 60 * 1000;
    std::wstring openUrlFile;                // 테스트 전용: 브라우저 대신 인가 URL 을 이 파일에 쓴다
    int retryBaseMs = 1000;                  // 503 백오프 (1·2·4초, P3-R12)
    int maxRetries = 3;
};

class SiwcClient final : public IAiProvider {
public:
    explicit SiwcClient(SiwcOptions options);
    ~SiwcClient() override;

    void Load();  // 저장된 등록·토큰 읽기 (네트워크 없음)
    ProviderCaps Caps() const override { return {}; }
    AccountInfo Info() const override;

    void PrepareSignIn();  // UI 스레드: 새 로그인 요청 직전 (이전 취소 표시 지움)
    AiResult SignIn();
    void CancelSignIn();   // 스레드 안전. Listen 전에 와도 사라지지 않는다
    AiResult ListModels(std::vector<ai::ModelInfo>& out) override;
    void BeginRequest() override;
    StreamResult Stream(const ai::AiRequest& request, size_t maxChars, const StreamTimeouts& timeouts,
                        const std::function<void(std::string_view)>& onDelta) override;
    // body: BuildResponsesRequest 결과
    StreamResult StreamBody(const std::string& body, size_t maxChars, const StreamTimeouts& timeouts,
                            const std::function<void(std::string_view)>& onDelta);
    void CancelStream() override;
    // forceLocal: revoke 를 확인하지 못해도 로컬 토큰 삭제
    AiResult SignOut(bool forceLocal);
    void MarkWelcomeShown();
    // 실연동 점검: 토큰을 강제로 갱신하고 모델 목록을 다시 받는다 (민감 정보 없이 결과만 기록)
    AiResult DiagnoseRefresh(size_t& modelCount);

    const std::filesystem::path& CredentialPath() const { return credPath_; }

private:
    struct Registration {
        std::string clientId, subject, email, idToken;
        bool planGranted = true;  // 마지막 동의에 plan usage 가 있었나 (없었으면 다음 로그인에 prompt=consent)
    };
    void EnsureDiscovery();
    AiResult EnsureFreshToken();
    AiResult Refresh();
    std::string LoadOrCreateHostId();
    bool Save();
    void ClearTokens();
    void ForgetSessionIdToken();
    HttpResponse PostForm(const std::string& url, const std::string& body);
    std::string Jwks(bool forceReload);
    std::string VerifyIdToken(const std::string& idToken, const std::string& clientId, const std::string& nonce,
                              ai::Identity& out);
    bool SleepCancellable(int ms);

    SiwcOptions opt_;
    std::filesystem::path credPath_, hostPath_;
    std::optional<Registration> reg_;
    std::optional<ai::TokenSet> tokens_;
    bool welcomeShown_ = false;
    std::string jwksCache_;
    HttpClient http_;
    net::LoopbackListener listener_;
    HANDLE cancelEvent_ = nullptr;  // 스트림·백오프 대기 취소
    std::atomic<bool> signInCancel_{false};
    bool discovered_ = false;
};

}  // namespace dp
