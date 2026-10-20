#include "platform/windows/ai/siwc_client.h"

#include "ai/jwt.h"

#include <shellapi.h>

#include <chrono>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "diagnostics/log.h"
#include "platform/windows/ai/win_crypto.h"
#include "platform/windows/strings.h"
#include "ai/json_util.h"

namespace dp {

using nlohmann::json;

namespace {

int64_t NowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Fnv1aHex(std::string_view s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return buf;
}

// 임시 파일 → MoveFileEx(REPLACE | WRITE_THROUGH): 교체되는 refresh 토큰이 반쯤 쓰인 채 남지 않게
bool WriteAtomic(const std::filesystem::path& path, const void* data, size_t size) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.wstring() + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, data, static_cast<DWORD>(size), &written, nullptr) && written == size && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::optional<std::vector<uint8_t>> ReadAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

json TokensToJson(const ai::TokenSet& t) {
    return {{"access_token", t.accessToken}, {"refresh_token", t.refreshToken}, {"id_token", t.idToken},
            {"token_type", t.tokenType},     {"expires_at", t.expiresAt},       {"scopes", t.scopes}};
}

ai::TokenSet TokensFromJson(const json& j) {
    ai::TokenSet t;
    t.accessToken = ai::JStr(j, "access_token", "");
    t.refreshToken = ai::JStr(j, "refresh_token", "");
    t.idToken = ai::JStr(j, "id_token", "");
    t.tokenType = ai::JStr(j, "token_type", "Bearer");
    t.expiresAt = ai::JInt(j, "expires_at", int64_t{0});
    if (j.contains("scopes") && j["scopes"].is_array())
        for (const auto& s : j["scopes"])
            if (s.is_string()) t.scopes.push_back(s.get<std::string>());
    return t;
}

}  // namespace

const char* ToString(StreamOutcome o) {
    switch (o) {
        case StreamOutcome::Completed: return "completed";
        case StreamOutcome::Truncated: return "truncated";
        case StreamOutcome::Cancelled: return "cancelled";
        case StreamOutcome::Incomplete: return "incomplete";
        case StreamOutcome::Interrupted: return "interrupted";
        case StreamOutcome::TimedOut: return "timed_out";
        case StreamOutcome::Failed: return "failed";
    }
    return "failed";
}

SiwcClient::SiwcClient(SiwcOptions options) : opt_(std::move(options)) {
    // 자격 증명 파일은 issuer 별로 나눈다: 테스트용 엔드포인트가 실제 계정 토큰을 읽거나 보낼 수 없다
    credPath_ = opt_.authDir / (L"siwc-" + ToWide(Fnv1aHex(opt_.endpoints.issuer)) + L".bin");
    hostPath_ = opt_.authDir / L"host_id.txt";
    cancelEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

SiwcClient::~SiwcClient() {
    if (cancelEvent_) CloseHandle(cancelEvent_);
}

void SiwcClient::Load() {
    reg_.reset();
    tokens_.reset();
    auto blob = ReadAll(credPath_);
    if (!blob) return;
    auto plain = crypto::UnprotectForUser(*blob);
    if (!plain) {
        log::Warn("[ai] 자격 증명 파일 복호화 실패 — 로그아웃 상태로 시작");
        return;
    }
    json j = json::parse(*plain, nullptr, false);
    SecureZeroMemory(plain->data(), plain->size());
    if (!j.is_object() || ai::JStr(j, "issuer", "") != opt_.endpoints.issuer) return;
    welcomeShown_ = ai::JBool(j, "welcome_shown", false);
    if (j.contains("registration") && j["registration"].is_object()) {
        const auto& r = j["registration"];
        reg_ = Registration{ai::JStr(r, "client_id", ""), ai::JStr(r, "subject", ""), ai::JStr(r, "email", ""), ai::JStr(r, "id_token", ""),
                            ai::JBool(r, "plan_granted", true)};
        if (reg_->clientId.empty()) reg_.reset();
    }
    if (reg_ && j.contains("tokens") && j["tokens"].is_object()) tokens_ = TokensFromJson(j["tokens"]);
    log::Info("[ai] 자격 증명 로드: registered={} signed_in={} plan_usage={}", reg_.has_value(), tokens_.has_value(),
              tokens_ && tokens_->HasPlanUsage());
}

bool SiwcClient::Save() {
    json j = {{"version", 1}, {"issuer", opt_.endpoints.issuer}, {"welcome_shown", welcomeShown_}};
    if (reg_)
        j["registration"] = {{"client_id", reg_->clientId}, {"subject", reg_->subject}, {"email", reg_->email},
                             {"id_token", reg_->idToken}, {"plan_granted", reg_->planGranted}};
    if (tokens_) j["tokens"] = TokensToJson(*tokens_);
    std::string plain = j.dump();
    auto blob = crypto::ProtectForUser(plain);
    SecureZeroMemory(plain.data(), plain.size());
    if (!blob || !WriteAtomic(credPath_, blob->data(), blob->size())) {
        log::Error("[ai] 자격 증명 저장 실패 err={}", GetLastError());
        return false;
    }
    return true;
}

AccountInfo SiwcClient::Info() const {
    AccountInfo a;
    a.registered = reg_.has_value();
    a.state = tokens_ ? AccountState::SignedIn : AccountState::SignedOut;
    a.planUsage = tokens_ && tokens_->HasPlanUsage();
    a.welcomeShown = welcomeShown_;
    if (reg_) a.email = reg_->email;
    return a;
}

void SiwcClient::MarkWelcomeShown() {
    welcomeShown_ = true;
    Save();
}

// 로그아웃: 보관하던 ID 토큰(다시 로그인 때 id_token_hint)도 지운다. 문서: revoke 뒤 access·refresh·ID 토큰을
// 로컬에서 지우고, id_token_hint 는 "로그아웃 뒤에는 생략". 실계정 검증 7번(2026-10-14)에서 힌트를 보냈을 때
// 확인(동의) 페이지가 나왔다. 이메일(login_hint)·client_id·host id 는 다시 로그인을 위해 유지한다
void SiwcClient::ForgetSessionIdToken() {
    if (reg_) reg_->idToken.clear();
}

void SiwcClient::ClearTokens() {
    tokens_.reset();
    Save();
}

std::string SiwcClient::LoadOrCreateHostId() {
    // ext_agent_host_id: 호스트마다 한 번 만들어 계속 쓴다. 불투명 값, 사용자 식별 정보 아님 (P3-R9)
    std::ifstream in(hostPath_);
    std::string id;
    if (in && std::getline(in, id) && id.rfind("urn:uuid:", 0) == 0) return id;
    id = "urn:uuid:" + crypto::NewUuidV4();
    WriteAtomic(hostPath_, id.data(), id.size());
    log::Info("[ai] 새 host id 생성");
    return id;
}

HttpResponse SiwcClient::PostForm(const std::string& url, const std::string& body) {
    HttpRequest req;
    req.method = "POST";
    req.url = url;
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "application/json"}};
    req.body = body;
    http_.ResetCancel();
    return http_.Send(req);
}

// OpenID 설정에서 엔드포인트를 받는다 (revoke 는 discovery 기준). 실패하면 내장값(설정 문서 기재값)으로 계속
void SiwcClient::EnsureDiscovery() {
    if (discovered_) return;
    HttpRequest req;
    req.url = opt_.endpoints.DiscoveryUrl();
    req.timeoutMs = 10000;
    http_.ResetCancel();
    auto res = http_.Send(req);
    if (!res.Ok()) {
        log::Warn("[ai] discovery 실패 http={} — 내장 엔드포인트 사용", res.status);
        return;
    }
    if (const std::string e = ai::ApplyDiscovery(opt_.endpoints, res.body); !e.empty()) {
        log::Warn("[ai] discovery 무시: {} — 내장 엔드포인트 사용", e);
        return;
    }
    discovered_ = true;
    log::Info("[ai] discovery ok");
}

std::string SiwcClient::Jwks(bool forceReload) {
    if (!jwksCache_.empty() && !forceReload) return jwksCache_;
    HttpRequest req;
    req.url = opt_.endpoints.jwks;
    http_.ResetCancel();
    auto res = http_.Send(req);
    if (res.Ok()) jwksCache_ = res.body;
    return jwksCache_;
}

std::string SiwcClient::VerifyIdToken(const std::string& idToken, const std::string& clientId, const std::string& nonce,
                                      ai::Identity& out) {
    auto jwt = ai::ParseJwt(idToken);
    if (!jwt) return "ID 토큰 형식 오류";
    const std::string kid = ai::JStr(jwt->header, "kid", "");
    auto key = ai::FindRsaJwk(Jwks(false), kid);
    if (!key) key = ai::FindRsaJwk(Jwks(true), kid);  // 키 교체 대비 1회 다시 받기
    if (!key) return "JWKS 에서 서명 키를 찾지 못함";
    if (!crypto::RsaVerifySha256(key->n, key->e, jwt->signingInput, jwt->signature)) return "ID 토큰 서명 불일치";
    return ai::ValidateIdTokenClaims(*jwt, opt_.endpoints.issuer, clientId, nonce, NowUnix(), out);
}

// ---------------------------------------------------------------------------
// 로그인
// ---------------------------------------------------------------------------

void SiwcClient::PrepareSignIn() { signInCancel_ = false; }

AiResult SiwcClient::SignIn() {
    AiResult r;
    auto cancelledEarly = [&] {
        if (!signInCancel_) return false;
        listener_.Close();
        r.code = "cancelled";
        log::Info("[ai] signin cancelled");
        return true;
    };
    if (cancelledEarly()) return r;
    EnsureDiscovery();
    const std::string hostId = LoadOrCreateHostId();
    const bool registration = !reg_.has_value();
    std::string err;
    bool listening = listener_.Listen(opt_.callbackPort, err);
    if (!listening && !registration && err == "port_in_use") {
        // 다시 로그인: 문서상 포트만 달라도 된다 ("Later sign-ins may use another available port")
        log::Info("[ai] 콜백 포트 {} 사용 중 — 다시 로그인은 빈 포트 사용", opt_.callbackPort);
        listening = listener_.Listen(0, err);
    }
    if (!listening) {
        log::Warn("[ai] 콜백 수신 시작 실패: {} (port {})", err, opt_.callbackPort);
        r.code = err == "port_in_use" ? "port_in_use" : "listener_failed";
        return r;
    }
    if (cancelledEarly()) return r;  // Listen 직전에 들어온 취소 (Listen 이 취소 이벤트를 초기화하므로 플래그로 확인)
    const std::string redirect = ai::RedirectUri(listener_.Port());
    ai::AuthorizeParams p;
    p.clientId = registration ? "" : reg_->clientId;
    p.agentName = opt_.agentName;
    p.hostId = hostId;
    p.redirectUri = redirect;
    p.state = crypto::RandomToken();
    p.nonce = crypto::RandomToken();
    const std::string verifier = crypto::RandomToken(48);
    p.codeChallenge = crypto::PkceChallenge(verifier);
    if (!registration) {
        p.idTokenHint = reg_->idToken;
        p.loginHint = reg_->email;
        p.reconsent = !reg_->planGranted;  // plan usage 없이 동의했던 등록: 다시 동의 화면을 띄운다
    }
    const std::string url = ai::BuildAuthorizeUrl(opt_.endpoints, p);  // id_token_hint 포함 가능 → 로그 금지
    log::Info("[ai] signin start registration={} port={} reconsent={}", registration, listener_.Port(), p.reconsent);
    if (!opt_.openUrlFile.empty()) {
        std::ofstream(std::filesystem::path(opt_.openUrlFile), std::ios::binary) << url;
    } else {
        const auto h = ShellExecuteW(nullptr, L"open", ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(h) <= 32) {
            listener_.Close();
            r.code = "browser_failed";
            return r;
        }
    }

    bool cancelled = false;
    std::optional<std::string> query;
    // state 가 맞는 콜백만 받는다 (다른 요청은 400 으로 답하고 계속 기다림). query 에는 인가 코드가 있어 기록하지 않는다
    auto stateMatches = [&](const std::string& q) {
        const bool okState = ai::ParseCallback(q, p.state, p.clientId).error != "state 불일치";
        if (!okState) log::Info("[ai] callback rejected reason=state");
        return okState;
    };
    if (!signInCancel_)
        query = net::WaitForCallback(listener_, ai::kCallbackPath, opt_.callbackTimeoutMs, cancelled, stateMatches);
    listener_.Close();  // 성공·취소·시간 초과 모두 여기서 포트를 놓는다
    if (signInCancel_) {
        query.reset();
        cancelled = true;
    }
    if (!query) {
        r.code = cancelled ? "cancelled" : "timeout";
        log::Info("[ai] signin {}", r.code);
        return r;
    }
    const auto cb = ai::ParseCallback(*query, p.state, p.clientId);
    if (cb.kind == ai::CallbackResult::Kind::Denied) {
        r.code = "access_denied";
        log::Info("[ai] signin denied");
        return r;
    }
    if (cb.kind != ai::CallbackResult::Kind::Code) {
        r.code = "invalid_callback";
        log::Warn("[ai] signin 콜백 거부: {}", cb.error);
        return r;
    }

    auto res = PostForm(opt_.endpoints.token, ai::CodeExchangeBody(cb.clientId, cb.code, verifier, redirect));
    auto tr = ai::ParseTokenResponse(res.status, res.body, NowUnix());
    if (!tr.tokens) {
        r.code = tr.error.empty() ? "token_exchange_failed" : tr.error;
        r.http = res.status;
        r.recovery = res.status == 0 ? ai::Recovery::Network : ai::Recovery::None;
        log::Warn("[ai] 코드 교환 실패 http={} error={} request_id={}", res.status, r.code, res.requestId);
        return r;
    }
    ai::Identity id;
    if (const std::string verr = VerifyIdToken(tr.tokens->idToken, cb.clientId, p.nonce, id); !verr.empty()) {
        r.code = "id_token_invalid";
        log::Warn("[ai] ID 토큰 검증 실패: {}", verr);
        return r;
    }
    if (!registration && !reg_->subject.empty() && reg_->subject != id.subject) {
        r.code = "account_mismatch";  // 다른 계정의 토큰으로 이 등록을 덮어쓰지 않는다
        log::Warn("[ai] 다시 로그인한 계정이 등록 계정과 다름 — 저장하지 않음");
        return r;
    }
    reg_ = Registration{cb.clientId, id.subject, id.email, tr.tokens->idToken, tr.tokens->HasPlanUsage()};
    tokens_ = std::move(tr.tokens);
    Save();
    r.ok = true;
    log::Info("[ai] signin ok registration={} plan_usage={} client=issued", registration, tokens_->HasPlanUsage());
    return r;
}

void SiwcClient::CancelSignIn() {
    signInCancel_ = true;
    listener_.Cancel();
}

// ---------------------------------------------------------------------------
// 토큰 갱신
// ---------------------------------------------------------------------------

AiResult SiwcClient::Refresh() {
    AiResult r;
    if (!reg_ || !tokens_ || tokens_->refreshToken.empty()) {
        r.recovery = ai::Recovery::ReSignIn;
        r.code = "no_refresh_token";
        return r;
    }
    EnsureDiscovery();
    const std::string previousIdToken = tokens_->idToken;
    auto res = PostForm(opt_.endpoints.token, ai::RefreshBody(reg_->clientId, tokens_->refreshToken));
    auto tr = ai::ParseTokenResponse(res.status, res.body, NowUnix(), &*tokens_);
    if (!tr.tokens) {
        r.http = res.status;
        r.code = tr.error;
        r.recovery = ai::ClassifyRefreshError(tr.error, res.status);
        log::Warn("[ai] token refresh 실패 http={} error={} recovery={} request_id={}", res.status, tr.error,
                  ai::ToString(r.recovery), res.requestId);
        if (r.recovery == ai::Recovery::ReSignIn) ClearTokens();  // 영구 오류만 삭제 (일시 장애는 유지)
        return r;
    }
    if (tr.tokens->idToken != previousIdToken && !tr.tokens->idToken.empty()) {
        // 갱신 응답의 새 ID 토큰: 서명·클레임을 확인하고 같은 계정인지 본다 (nonce 없음)
        ai::Identity id;
        const std::string verr = VerifyIdToken(tr.tokens->idToken, reg_->clientId, "", id);
        if (!verr.empty() || (!reg_->subject.empty() && id.subject != reg_->subject)) {
            log::Warn("[ai] 갱신 응답의 ID 토큰 거부: {}", verr.empty() ? "계정 불일치" : verr);
            r.code = "refresh_id_token_invalid";
            r.recovery = ai::Recovery::ReSignIn;
            ClearTokens();
            return r;
        }
        reg_->idToken = tr.tokens->idToken;
    }
    tokens_ = std::move(tr.tokens);
    Save();  // 교체된 refresh 토큰을 먼저 저장한 뒤 새 access 토큰을 쓴다
    r.ok = true;
    log::Info("[ai] token refresh ok");
    return r;
}

AiResult SiwcClient::EnsureFreshToken() {
    AiResult r;
    if (!tokens_) {
        r.recovery = ai::Recovery::ReSignIn;
        r.code = "signed_out";
        return r;
    }
    if (ai::NeedsRefresh(*tokens_, NowUnix())) return Refresh();
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------------------
// 모델·추론
// ---------------------------------------------------------------------------

AiResult SiwcClient::ListModels(std::vector<ai::ModelInfo>& out) {
    AiResult r = EnsureFreshToken();
    if (!r.ok) return r;
    HttpRequest req;
    req.url = opt_.endpoints.api + "/models";
    req.headers = {{"Authorization", "Bearer " + tokens_->accessToken}, {"Accept", "application/json"}};
    http_.ResetCancel();
    auto res = http_.Send(req);
    r.http = res.status;
    if (!res.Ok()) {
        r.ok = false;
        if (res.status == 0) {
            r.recovery = ai::Recovery::Network;
            r.code = "network";
        } else {
            auto e = ai::ClassifyHttpError(res.status, res.body);
            r.recovery = e.recovery;
            r.code = e.code.empty() ? "http_" + std::to_string(res.status) : e.code;
        }
        log::Warn("[ai] models 실패 http={} code={} request_id={}", res.status, r.code, res.requestId);
        return r;
    }
    out = ai::ParseModels(res.body);
    log::Info("[ai] models {}개", out.size());
    return r;
}

bool SiwcClient::SleepCancellable(int ms) { return WaitForSingleObject(cancelEvent_, ms) == WAIT_TIMEOUT; }

namespace {
// 요청 마감 감시: 첫 글자 마감·전체 마감을 넘기면 HTTP 요청을 취소하고 사유를 남긴다 (스레드풀 타이머)
struct StreamWatchdog {
    // 첫 응답 마감: 시도마다 요청 전송 시점부터 (503 백오프 대기는 세지 않음). 전체 마감: 백오프·갱신 포함 요청 전체
    HttpClient* http = nullptr;
    std::atomic<int> reason{0};  // 0 없음, 1 첫 응답 시간 초과, 2 전체 시간 초과
    std::atomic<bool> firstSeen{false}, inAttempt{false};
    std::atomic<ULONGLONG> attemptStart{0};
    PTP_TIMER timer = nullptr;
    ULONGLONG start = 0, firstMs = 0, totalMs = 0;
    static void CALLBACK Fire(PTP_CALLBACK_INSTANCE, PVOID ctx, PTP_TIMER) {
        auto* w = static_cast<StreamWatchdog*>(ctx);
        const ULONGLONG now = GetTickCount64();
        if (w->inAttempt && !w->firstSeen && now - w->attemptStart >= w->firstMs) w->reason = 1;
        else if (now - w->start >= w->totalMs) w->reason = 2;
        else return w->Arm();  // 첫 글자를 받았거나 대기 중이면 남은 마감으로 다시 건다
        w->http->Cancel();
    }
    void Arm() {
        const ULONGLONG now = GetTickCount64();
        ULONGLONG due = start + totalMs;
        if (inAttempt && !firstSeen) due = std::min(due, attemptStart.load() + firstMs);
        const LONGLONG left = due > now ? static_cast<LONGLONG>(due - now) : 0;
        LARGE_INTEGER li;
        li.QuadPart = -left * 10000;
        FILETIME ft{li.LowPart, static_cast<DWORD>(li.HighPart)};
        SetThreadpoolTimer(timer, &ft, 0, 50);
    }
    void BeginAttempt() {
        attemptStart = GetTickCount64();
        inAttempt = true;
        if (timer) Arm();
    }
    void EndAttempt() {
        inAttempt = false;
        if (timer) Arm();
    }
    StreamWatchdog(HttpClient* h, const StreamTimeouts& t) : http(h) {
        firstMs = static_cast<ULONGLONG>(t.firstTokenSec) * 1000;
        totalMs = static_cast<ULONGLONG>(t.totalSec) * 1000;
        start = GetTickCount64();
        timer = CreateThreadpoolTimer(&StreamWatchdog::Fire, this, nullptr);
        if (timer) Arm();
    }
    ~StreamWatchdog() {
        if (!timer) return;
        SetThreadpoolTimer(timer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(timer, TRUE);
        CloseThreadpoolTimer(timer);
    }
};
}  // namespace

StreamResult SiwcClient::Stream(const ai::AiRequest& request, size_t maxChars, const StreamTimeouts& timeouts,
                                const std::function<void(std::string_view)>& onDelta) {
    std::string body = ai::BuildResponsesRequest(request);
    StreamResult r = StreamBody(body, maxChars, timeouts, onDelta);
    SecureZeroMemory(body.data(), body.size());  // 이미지·대화가 든 본문은 보낸 뒤 지운다
    return r;
}

StreamResult SiwcClient::StreamBody(const std::string& body, size_t maxChars, const StreamTimeouts& timeouts,
                                    const std::function<void(std::string_view)>& onDelta) {
    StreamResult sr;
    bool refreshedFor401 = false;
    StreamWatchdog watchdog(&http_, timeouts);
    auto timedOut = [&] {
        const int why = watchdog.reason.load();
        if (!why) return false;
        sr.outcome = StreamOutcome::TimedOut;
        sr.code = why == 1 ? "first_token_timeout" : "request_timeout";
        sr.recovery = ai::Recovery::Network;
        return true;
    };
    for (int attempt = 0;; ++attempt) {
        sr.attempts = attempt + 1;
        if (timedOut()) return sr;  // 백오프·갱신 중에 마감이 지났을 수 있다
        if (WaitForSingleObject(cancelEvent_, 0) == WAIT_OBJECT_0) {
            sr.outcome = StreamOutcome::Cancelled;
            return sr;
        }
        AiResult fresh = EnsureFreshToken();
        if (!fresh.ok) {
            sr.recovery = fresh.recovery;
            sr.code = fresh.code;
            sr.http = fresh.http;
            if (fresh.recovery == ai::Recovery::RetryBackoff && attempt < opt_.maxRetries &&
                SleepCancellable(opt_.retryBaseMs << attempt))
                continue;
            return sr;
        }
        if (!tokens_->HasPlanUsage()) {
            sr.recovery = ai::Recovery::NotEligible;
            sr.code = "plan_usage_disabled";  // 범위 없음: 로그인은 유지, 추론 금지 (errors-and-recovery §5)
            return sr;
        }
        HttpRequest req;
        req.method = "POST";
        req.url = opt_.endpoints.api + "/responses";
        req.headers = {{"Authorization", "Bearer " + tokens_->accessToken},
                       {"Content-Type", "application/json"},
                       {"Accept", "text/event-stream"}};
        req.body = body;
        req.timeoutMs = timeouts.totalSec * 1000 + 5000;  // 읽기 제한은 감시 타이머가 맡는다
        if (timedOut()) return sr;
        if (WaitForSingleObject(cancelEvent_, 0) == WAIT_OBJECT_0) {  // 갱신 도중 들어온 취소
            sr.outcome = StreamOutcome::Cancelled;
            return sr;
        }

        ai::SseParser parser;
        std::vector<ai::SseEvent> events;
        bool completed = false, incomplete = false, failed = false, truncated = false, sawEvent = false;
        std::string failCode;
        auto handle = [&](const ai::SseEvent& e) -> bool {
            sawEvent = true;
            const auto u = ai::InterpretEvent(e);
            switch (u.kind) {
                case ai::StreamUpdate::Kind::Delta: {
                    std::string text = u.text;
                    const size_t len = ai::Utf8Length(text);
                    if (sr.chars + len > maxChars) {
                        text = ai::Utf8Head(text, maxChars - sr.chars);
                        truncated = true;
                    }
                    sr.chars += ai::Utf8Length(text);
                    if (!text.empty()) {
                        if (!watchdog.firstSeen.exchange(true))
                            sr.firstDeltaSec = (GetTickCount64() - watchdog.start) / 1000.0;
                        onDelta(text);
                    }
                    return !truncated;  // 앱 상한 도달 → 스트림 중단 (P3-R7)
                }
                case ai::StreamUpdate::Kind::Completed: completed = true; return false;
                case ai::StreamUpdate::Kind::Incomplete: incomplete = true; failCode = u.code; return false;
                case ai::StreamUpdate::Kind::Failed:
                case ai::StreamUpdate::Kind::Error: failed = true; failCode = u.code; return false;
                default: return true;
            }
        };
        watchdog.BeginAttempt();
        auto res = http_.SendStreaming(req, [&](std::string_view chunk) {
            events.clear();
            parser.Feed(chunk, events);
            for (const auto& e : events)
                if (!handle(e)) return false;
            return true;
        });
        watchdog.EndAttempt();
        sr.http = res.status;
        sr.requestId = res.requestId;
        if (timedOut()) return sr;
        if (res.cancelled || WaitForSingleObject(cancelEvent_, 0) == WAIT_OBJECT_0) {
            sr.outcome = truncated ? StreamOutcome::Truncated : StreamOutcome::Cancelled;
            return sr;
        }
        if (res.Ok()) {
            if (completed) sr.outcome = StreamOutcome::Completed;
            else if (truncated) sr.outcome = StreamOutcome::Truncated;  // 콜백이 false 를 돌려 수신 중단됨
            else if (incomplete) {
                sr.outcome = StreamOutcome::Incomplete;
                sr.code = failCode;
            } else if (failed) {
                sr.outcome = StreamOutcome::Failed;
                sr.code = failCode;
                sr.recovery = ai::ClassifyErrorCode(failCode, 0);
            } else {
                sr.outcome = StreamOutcome::Interrupted;  // response.completed 없이 끝남 → 실패로 취급
                sr.recovery = ai::Recovery::Network;
                sr.code = sawEvent ? "stream_ended" : "no_events";
            }
            return sr;
        }
        if (res.status == 0) {
            sr.outcome = StreamOutcome::Failed;
            sr.recovery = ai::Recovery::Network;
            sr.code = "network";
            return sr;
        }
        // 스트림 시작 전 오류 응답
        auto e = ai::ClassifyHttpError(res.status, res.body);
        sr.code = e.code.empty() ? "http_" + std::to_string(res.status) : e.code;
        sr.param = e.param;
        sr.recovery = e.recovery;
        sr.outcome = StreamOutcome::Failed;
        log::Warn("[ai] responses http={} code={} param={} recovery={} attempt={} request_id={}", res.status, sr.code,
                  e.param, ai::ToString(e.recovery), sr.attempts, res.requestId);
        if (e.recovery == ai::Recovery::ReSignIn && !refreshedFor401) {
            refreshedFor401 = true;  // 401: 토큰을 한 번 갱신해 보고 재시도
            AiResult rf = Refresh();
            if (rf.ok) continue;
            sr.code = rf.code.empty() ? sr.code : rf.code;
            return sr;
        }
        if (e.recovery == ai::Recovery::RetryBackoff && attempt < opt_.maxRetries &&
            SleepCancellable(opt_.retryBaseMs << attempt))
            continue;
        return sr;
    }
}

void SiwcClient::BeginRequest() {
    ResetEvent(cancelEvent_);
    http_.ResetCancel();
}

AiResult SiwcClient::DiagnoseRefresh(size_t& modelCount) {
    modelCount = 0;
    AiResult r = Refresh();
    log::Info("[ai] diag refresh ok={} http={} code={}", r.ok, r.http, r.code);
    if (!r.ok) return r;
    std::vector<ai::ModelInfo> models;
    r = ListModels(models);
    modelCount = models.size();
    log::Info("[ai] diag models ok={} count={}", r.ok, modelCount);
    return r;
}

void SiwcClient::CancelStream() {
    SetEvent(cancelEvent_);
    http_.Cancel();
}

// ---------------------------------------------------------------------------
// 로그아웃
// ---------------------------------------------------------------------------

AiResult SiwcClient::SignOut(bool forceLocal) {
    AiResult r;
    if (!tokens_) {
        r.ok = true;
        return r;
    }
    if (reg_ && !tokens_->refreshToken.empty()) {
        EnsureDiscovery();
        HttpResponse res;
        for (int attempt = 0; attempt < 2; ++attempt) {  // 5xx·네트워크 오류만 1회 재시도
            if (attempt) Sleep(300);
            res = PostForm(opt_.endpoints.revoke, ai::RevokeBody(reg_->clientId, tokens_->refreshToken));
            if (res.status != 0 && res.status < 500) break;
        }
        r.http = res.status;
        if (res.status == 200) {
            ForgetSessionIdToken();
            ClearTokens();  // 등록(client_id·host id)은 유지
            r.ok = true;
            log::Info("[ai] signout revoked");
            return r;
        }
        log::Warn("[ai] revoke 실패 http={} err={} request_id={}", res.status, res.winError, res.requestId);
    }
    if (forceLocal) {
        ForgetSessionIdToken();
        ClearTokens();
        r.ok = true;
        r.code = "revoke_unconfirmed";  // 원격 해제 미확인 → ChatGPT 설정에서 연결 해제 안내
        log::Info("[ai] signout local only (revoke 미확인)");
        return r;
    }
    r.code = "revoke_failed";
    r.recovery = ai::Recovery::RetryBackoff;
    return r;
}

}  // namespace dp
