#pragma once
// AI 공급자 경계 (기준선 §7.1·§9.1, ADR-06). 지금 구현은 SIWC(SiwcClient) 1개다.
// 로컬 LLM(OpenAI 호환 로컬 HTTP 서버 등)은 이 경계로 추가한다 — P4 는 인터페이스만 (2026-10-14 결정).
// 모든 메서드는 AI 작업 스레드에서 호출한다. 예외: CancelStream 은 아무 스레드에서나.
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ai/responses.h"
#include "ai/siwc_protocol.h"

namespace dp {

enum class AccountState { SignedOut, SignedIn };

struct AccountInfo {
    AccountState state = AccountState::SignedOut;
    bool registered = false;   // 발급 client_id 보유 (로그아웃해도 유지)
    bool planUsage = false;    // chatgpt.tokens.use.direct 범위
    bool welcomeShown = false;
    std::string email;         // UI 표시용 (로그에는 남기지 않음)
};

struct AiResult {
    bool ok = false;
    ai::Recovery recovery = ai::Recovery::None;
    std::string code;     // 오류 코드 (access_denied, port_in_use, subscription_sharing_* …)
    int http = 0;
};

enum class StreamOutcome { Completed, Truncated, Cancelled, Incomplete, Interrupted, TimedOut, Failed };
const char* ToString(StreamOutcome o);

struct StreamResult {
    StreamOutcome outcome = StreamOutcome::Failed;
    ai::Recovery recovery = ai::Recovery::None;
    std::string code;
    std::string param;        // 400 오류의 error.param (어떤 기능을 거부했는지: text.format, input_image …)
    int http = 0;
    int attempts = 0;
    size_t chars = 0;
    std::string requestId;    // x-request-id (진단)
    double firstDeltaSec = -1;  // 요청 시작 → 첫 글자 (진단)
};

struct StreamTimeouts {
    int firstTokenSec = 120;  // 첫 응답 글자까지
    int totalSec = 300;       // 요청 전체
};

struct ProviderCaps {
    bool requiresSignIn = true;     // SIWC: 로그인 필요. 로컬 LLM: 보통 불필요
    bool sendsOffDevice = true;     // 요청이 이 PC 밖으로 나간다 (동의 화면 문구에 반영)
    bool images = true;             // 이미지 입력 (모델별로 거부될 수 있음 → 오류로 판단)
    bool structuredOutput = true;   // text.format json_schema (거부되면 안내문 JSON 으로 전환)
};

class IAiProvider {
public:
    virtual ~IAiProvider() = default;
    virtual ProviderCaps Caps() const = 0;
    virtual AccountInfo Info() const = 0;
    virtual AiResult ListModels(std::vector<ai::ModelInfo>& out) = 0;
    // 요청 직전 취소 상태 초기화. 이후의 CancelStream 은 Stream 이 시작 전이어도 반영된다
    virtual void BeginRequest() = 0;
    // onDelta 는 작업 스레드에서 호출된다. maxChars 를 넘으면 잘라서 끝낸다 (Truncated)
    virtual StreamResult Stream(const ai::AiRequest& request, size_t maxChars, const StreamTimeouts& timeouts,
                                const std::function<void(std::string_view)>& onDelta) = 0;
    virtual void CancelStream() = 0;
};

}  // namespace dp
