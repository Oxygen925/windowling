#pragma once
// Sign in with ChatGPT (ChatGPT plan usage, 오픈소스·로컬 앱 흐름) 프로토콜 — Windows 비의존 순수 로직.
// 근거: developers.openai.com/siwc/token-sharing-open-source/* (2026-10-14 조사, 문서 기재·실연동 미검증).
// 네트워크·암호·저장은 플랫폼 계층이 담당하고, 여기서는 URL·본문 생성, 응답 해석, 검증 규칙, 오류 분류만 한다.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>


namespace dp::ai {

inline constexpr std::string_view kDynamicClientId = "dynamic_agent_client";
inline constexpr std::string_view kScopes = "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct";
inline constexpr std::string_view kPlanScope = "chatgpt.tokens.use.direct";
inline constexpr std::string_view kResource = "https://api.openai.com/v1";
inline constexpr std::string_view kCallbackPath = "/auth/callback";
inline constexpr std::string_view kUsageUrl = "https://chatgpt.com/settings/usage";

// OIDC 설정 (auth.openai.com/.well-known/openid-configuration 기재값). 테스트는 모의 서버 주소로 전체를 바꾼다.
struct SiwcEndpoints {
    std::string issuer, authorize, token, revoke, jwks, api;
    static SiwcEndpoints Production();
    static SiwcEndpoints ForBase(std::string_view base);  // 모의 서버: base 아래 같은 경로 구조, issuer = base
    bool IsProduction() const { return issuer == Production().issuer; }
    std::string DiscoveryUrl() const { return issuer + "/.well-known/openid-configuration"; }
};
// OpenID 설정 문서로 인증 엔드포인트를 갱신한다 (revocation_endpoint 는 discovery 기준 — profiles-and-sessions).
// issuer 가 일치하고 모든 엔드포인트가 issuer 와 같은 origin 일 때만 반영. 실패 사유 또는 빈 문자열
std::string ApplyDiscovery(SiwcEndpoints& ep, std::string_view discoveryJson);

// ---- 인코딩 ----
std::string UrlEncode(std::string_view s);
std::string UrlDecode(std::string_view s);  // '+' → 공백
std::map<std::string, std::string> ParseQuery(std::string_view query);
std::string FormEncode(const std::vector<std::pair<std::string, std::string>>& fields);
std::string Base64UrlEncode(const uint8_t* data, size_t size);
std::optional<std::vector<uint8_t>> Base64UrlDecode(std::string_view s);

// ---- 인가 ----
struct AuthorizeParams {
    std::string clientId;     // 비면 최초 등록(dynamic_agent_client + agent_name_hint)
    std::string agentName;    // 최초 등록에만 전송
    std::string hostId;       // ext_agent_host_id (항상)
    std::string redirectUri;  // http://127.0.0.1:<port>/auth/callback
    std::string state, nonce, codeChallenge;
    std::string idTokenHint, loginHint;  // 다시 로그인 시 선택
    bool reconsent = false;   // prompt=consent: plan usage 를 거부했던 등록에 다시 동의를 받는다 (DevKit 동작, 문서 미기재 [검증])
};
std::string BuildAuthorizeUrl(const SiwcEndpoints& ep, const AuthorizeParams& p);
std::string RedirectUri(uint16_t port);

struct CallbackResult {
    enum class Kind { Code, Denied, Invalid } kind = Kind::Invalid;
    std::string code, clientId, scope;
    std::string error;  // Invalid 사유 또는 OAuth error
};
// state 검증을 가장 먼저 한다. pendingClientId 가 비면(최초 등록) 콜백에 발급 client_id 가 있어야 하고,
// 비어 있지 않으면 콜백의 client_id 는 생략 가능하되 다르면 거부한다.
CallbackResult ParseCallback(std::string_view query, std::string_view expectedState, std::string_view pendingClientId);

// ---- 토큰 ----
std::string CodeExchangeBody(std::string_view clientId, std::string_view code, std::string_view verifier,
                             std::string_view redirectUri);
std::string RefreshBody(std::string_view clientId, std::string_view refreshToken);
std::string RevokeBody(std::string_view clientId, std::string_view refreshToken);

struct TokenSet {
    std::string accessToken, refreshToken, idToken, tokenType;
    int64_t expiresAt = 0;  // unix 초
    std::vector<std::string> scopes;
    bool HasScope(std::string_view s) const;
    bool HasPlanUsage() const { return HasScope(kPlanScope); }
};
struct TokenResult {
    std::optional<TokenSet> tokens;
    std::string error, errorDescription;  // OAuth 오류 (invalid_grant 등)
};
// previous: 갱신 응답에 빠진 필드(id_token·scope)를 이전 값으로 채운다
TokenResult ParseTokenResponse(int httpStatus, std::string_view body, int64_t nowUnix, const TokenSet* previous = nullptr);
bool NeedsRefresh(const TokenSet& t, int64_t nowUnix, int64_t marginSec = 120);

// ---- ID 토큰 ----
// JWT 파싱·클레임 검증은 ai/jwt.h (nlohmann/json 을 쓰므로 이 공개 헤더에서 분리 — 앱 TU 컴파일 비용 절감)
struct Identity {
    std::string subject, email;
};
struct RsaJwk {
    std::vector<uint8_t> n, e;
};
std::optional<RsaJwk> FindRsaJwk(std::string_view jwksJson, std::string_view kid);

// ---- 오류 분류 (errors-and-recovery) ----
enum class Recovery {
    None,
    RetryBackoff,  // 503 계열: 자격 증명 유지, 제한된 백오프
    ReSignIn,      // 401·invalid_user·갱신 영구 오류
    UsageLimit,    // 429 usage_limit_exceeded: 새 요청 중지 + Manage usage
    NotEligible,   // 403 user_not_eligible·범위 없음: 재시도·OAuth 반복 금지
    Restricted,    // 403 정책·지역 제한
    FixRequest,    // 400 unsupported_capability·route_not_supported
    ClientConfig,  // invalid_client·chatpass_v2_*: 클라이언트 설정 문제
    Network,       // 연결 실패·끊김
    Unknown,
};
const char* ToString(Recovery r);
struct ApiError {
    int http = 0;
    std::string code, param, message;
    Recovery recovery = Recovery::Unknown;
};
ApiError ClassifyHttpError(int http, std::string_view body);  // 스트림 시작 전 오류 응답
Recovery ClassifyErrorCode(std::string_view code, int http);    // response.failed 의 error.code 포함
Recovery ClassifyRefreshError(std::string_view oauthError, int http);

}  // namespace dp::ai
