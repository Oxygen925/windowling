#include "ai/siwc_protocol.h"

#include "ai/jwt.h"
#include "ai/json_util.h"

#include <algorithm>
#include <sstream>

namespace dp::ai {

using nlohmann::json;

SiwcEndpoints SiwcEndpoints::Production() {
    return {"https://auth.openai.com",
            "https://auth.openai.com/api/accounts/authorize",
            "https://auth.openai.com/api/accounts/oauth/token",
            "https://auth.openai.com/api/accounts/oauth/revoke",
            "https://auth.openai.com/.well-known/jwks.json",
            "https://api.openai.com/v1"};
}

SiwcEndpoints SiwcEndpoints::ForBase(std::string_view base) {
    std::string b(base);
    while (!b.empty() && b.back() == '/') b.pop_back();
    return {b, b + "/api/accounts/authorize", b + "/api/accounts/oauth/token", b + "/api/accounts/oauth/revoke",
            b + "/.well-known/jwks.json", b + "/v1"};
}

std::string ApplyDiscovery(SiwcEndpoints& ep, std::string_view discoveryJson) {
    json j = json::parse(discoveryJson, nullptr, false);
    if (!j.is_object()) return "discovery JSON 아님";
    if (JStr(j, "issuer", "") != ep.issuer) return "discovery issuer 불일치";
    auto origin = [](const std::string& url) {
        const size_t scheme = url.find("://");
        if (scheme == std::string::npos) return std::string();
        const size_t slash = url.find('/', scheme + 3);
        return url.substr(0, slash);
    };
    const std::string want = origin(ep.issuer);
    SiwcEndpoints next = ep;
    struct Field {
        const char* key;
        std::string* out;
    } fields[] = {{"authorization_endpoint", &next.authorize},
                  {"token_endpoint", &next.token},
                  {"revocation_endpoint", &next.revoke},
                  {"jwks_uri", &next.jwks}};
    for (auto& f : fields) {
        const std::string v = JStr(j, f.key, "");
        if (v.empty()) return std::string(f.key) + " 없음";
        if (origin(v) != want) return std::string(f.key) + " 가 issuer 와 다른 origin";
        *f.out = v;
    }
    ep = next;
    return {};
}

// ---------------------------------------------------------------------------
// 인코딩
// ---------------------------------------------------------------------------

std::string UrlEncode(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string UrlDecode(std::string_view s) {
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && val(s[i + 1]) >= 0 && val(s[i + 2]) >= 0) {
            out += static_cast<char>(val(s[i + 1]) * 16 + val(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::map<std::string, std::string> ParseQuery(std::string_view q) {
    std::map<std::string, std::string> out;
    if (!q.empty() && q.front() == '?') q.remove_prefix(1);
    while (!q.empty()) {
        const size_t amp = q.find('&');
        const std::string_view part = q.substr(0, amp);
        const size_t eq = part.find('=');
        if (!part.empty()) {
            const std::string key = UrlDecode(part.substr(0, eq));
            const std::string value = eq == std::string_view::npos ? std::string() : UrlDecode(part.substr(eq + 1));
            out.emplace(key, value);  // 중복 키는 첫 값 유지
        }
        if (amp == std::string_view::npos) break;
        q.remove_prefix(amp + 1);
    }
    return out;
}

std::string FormEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out;
    for (const auto& [k, v] : fields) {
        if (!out.empty()) out += '&';
        out += UrlEncode(k) + "=" + UrlEncode(v);
    }
    return out;
}

std::string Base64UrlEncode(const uint8_t* data, size_t size) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t n = (uint32_t(data[i]) << 16) | (i + 1 < size ? uint32_t(data[i + 1]) << 8 : 0) |
                           (i + 2 < size ? uint32_t(data[i + 2]) : 0);
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        if (i + 1 < size) out += tbl[(n >> 6) & 63];
        if (i + 2 < size) out += tbl[n & 63];
    }
    return out;  // 패딩 없음 (PKCE·JWT)
}

std::optional<std::vector<uint8_t>> Base64UrlDecode(std::string_view s) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-' || c == '+') return 62;
        if (c == '_' || c == '/') return 63;
        return -1;
    };
    while (!s.empty() && s.back() == '=') s.remove_suffix(1);
    if (s.size() % 4 == 1) return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve(s.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        const int v = val(c);
        if (v < 0) return std::nullopt;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 인가
// ---------------------------------------------------------------------------

std::string RedirectUri(uint16_t port) { return "http://127.0.0.1:" + std::to_string(port) + std::string(kCallbackPath); }

std::string BuildAuthorizeUrl(const SiwcEndpoints& ep, const AuthorizeParams& p) {
    const bool registration = p.clientId.empty();
    std::vector<std::pair<std::string, std::string>> q;
    q.emplace_back("client_id", registration ? std::string(kDynamicClientId) : p.clientId);
    if (registration) q.emplace_back("agent_name_hint", p.agentName);
    q.emplace_back("ext_agent_host_id", p.hostId);
    q.emplace_back("response_type", "code");
    q.emplace_back("redirect_uri", p.redirectUri);
    q.emplace_back("scope", std::string(kScopes));
    q.emplace_back("resource", std::string(kResource));
    q.emplace_back("state", p.state);
    q.emplace_back("nonce", p.nonce);
    q.emplace_back("code_challenge_method", "S256");
    q.emplace_back("code_challenge", p.codeChallenge);
    if (!registration && !p.idTokenHint.empty()) q.emplace_back("id_token_hint", p.idTokenHint);
    if (!registration && !p.loginHint.empty()) q.emplace_back("login_hint", p.loginHint);
    if (!registration && p.reconsent) q.emplace_back("prompt", "consent");
    return ep.authorize + "?" + FormEncode(q);
}

CallbackResult ParseCallback(std::string_view query, std::string_view expectedState, std::string_view pendingClientId) {
    CallbackResult r;
    const auto q = ParseQuery(query);
    auto get = [&](const char* k) {
        auto it = q.find(k);
        return it == q.end() ? std::string() : it->second;
    };
    // state 를 먼저 검증한다 (거부 응답도 state 확인 후에만 신뢰)
    if (expectedState.empty() || get("state") != expectedState) {
        r.error = "state 불일치";
        return r;
    }
    if (const std::string e = get("error"); !e.empty()) {
        r.kind = e == "access_denied" ? CallbackResult::Kind::Denied : CallbackResult::Kind::Invalid;
        r.error = e;
        return r;
    }
    r.code = get("code");
    r.clientId = get("client_id");
    r.scope = get("scope");
    if (r.code.empty()) {
        r.error = "code 없음";
        return r;
    }
    if (pendingClientId.empty()) {
        if (r.clientId.empty() || r.clientId == kDynamicClientId) {
            r.error = "발급 client_id 없음 (등록 미완료)";
            return r;
        }
    } else {
        if (!r.clientId.empty() && r.clientId != pendingClientId) {
            r.error = "client_id 불일치";
            return r;
        }
        r.clientId = std::string(pendingClientId);
    }
    r.kind = CallbackResult::Kind::Code;
    return r;
}

// ---------------------------------------------------------------------------
// 토큰
// ---------------------------------------------------------------------------

std::string CodeExchangeBody(std::string_view clientId, std::string_view code, std::string_view verifier,
                             std::string_view redirectUri) {
    return FormEncode({{"grant_type", "authorization_code"},
                       {"client_id", std::string(clientId)},
                       {"code", std::string(code)},
                       {"code_verifier", std::string(verifier)},
                       {"redirect_uri", std::string(redirectUri)},
                       {"resource", std::string(kResource)}});
}

std::string RefreshBody(std::string_view clientId, std::string_view refreshToken) {
    // scope 를 생략하면 기존 권한 유지 (profiles-and-sessions)
    return FormEncode({{"grant_type", "refresh_token"},
                       {"client_id", std::string(clientId)},
                       {"refresh_token", std::string(refreshToken)},
                       {"resource", std::string(kResource)}});
}

std::string RevokeBody(std::string_view clientId, std::string_view refreshToken) {
    return FormEncode({{"token", std::string(refreshToken)},
                       {"token_type_hint", "refresh_token"},
                       {"client_id", std::string(clientId)}});
}

bool TokenSet::HasScope(std::string_view s) const { return std::find(scopes.begin(), scopes.end(), s) != scopes.end(); }

namespace {
std::vector<std::string> SplitSpaces(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    for (std::string w; is >> w;) out.push_back(w);
    return out;
}
}  // namespace

TokenResult ParseTokenResponse(int httpStatus, std::string_view body, int64_t nowUnix, const TokenSet* previous) {
    TokenResult r;
    json j = json::parse(body, nullptr, false);
    if (httpStatus != 200) {
        if (j.is_object()) {
            r.error = JStr(j, "error", "");
            r.errorDescription = JStr(j, "error_description", "");
            if (r.error.empty() && j.contains("error") && j["error"].is_object()) r.error = JStr(j["error"], "code", "");
        }
        if (r.error.empty()) r.error = "http_" + std::to_string(httpStatus);
        return r;
    }
    if (!j.is_object() || !j.contains("access_token") || !j["access_token"].is_string()) {
        r.error = "invalid_response";
        return r;
    }
    TokenSet t;
    t.accessToken = j["access_token"].get<std::string>();
    t.refreshToken = JStr(j, "refresh_token", previous ? previous->refreshToken : "");
    t.idToken = JStr(j, "id_token", previous ? previous->idToken : "");
    t.tokenType = JStr(j, "token_type", "Bearer");
    const int64_t expiresIn = j.contains("expires_in") && j["expires_in"].is_number() ? j["expires_in"].get<int64_t>() : 3600;
    t.expiresAt = nowUnix + std::max<int64_t>(0, expiresIn);
    if (j.contains("scope") && j["scope"].is_string()) t.scopes = SplitSpaces(j["scope"].get<std::string>());
    else if (previous) t.scopes = previous->scopes;
    r.tokens = std::move(t);
    return r;
}

bool NeedsRefresh(const TokenSet& t, int64_t nowUnix, int64_t marginSec) { return nowUnix + marginSec >= t.expiresAt; }

// ---------------------------------------------------------------------------
// ID 토큰
// ---------------------------------------------------------------------------

std::optional<Jwt> ParseJwt(std::string_view token) {
    const size_t a = token.find('.');
    const size_t b = a == std::string_view::npos ? a : token.find('.', a + 1);
    if (a == std::string_view::npos || b == std::string_view::npos || token.find('.', b + 1) != std::string_view::npos)
        return std::nullopt;
    auto h = Base64UrlDecode(token.substr(0, a));
    auto p = Base64UrlDecode(token.substr(a + 1, b - a - 1));
    auto s = Base64UrlDecode(token.substr(b + 1));
    if (!h || !p || !s) return std::nullopt;
    Jwt jwt;
    jwt.header = json::parse(h->begin(), h->end(), nullptr, false);
    jwt.payload = json::parse(p->begin(), p->end(), nullptr, false);
    if (!jwt.header.is_object() || !jwt.payload.is_object()) return std::nullopt;
    jwt.signingInput = std::string(token.substr(0, b));
    jwt.signature = std::move(*s);
    return jwt;
}

std::string ValidateIdTokenClaims(const Jwt& jwt, std::string_view issuer, std::string_view audience,
                                  std::string_view nonce, int64_t nowUnix, Identity& out, int64_t leewaySec) {
    const auto& h = jwt.header;
    const auto& p = jwt.payload;
    if (JStr(h, "alg", "") != "RS256") return "alg 가 RS256 이 아님";
    if (JStr(p, "iss", "") != issuer) return "iss 불일치";
    bool audOk = false;
    if (p.contains("aud")) {
        if (p["aud"].is_string()) audOk = p["aud"].get<std::string>() == audience;
        else if (p["aud"].is_array())
            for (const auto& a : p["aud"])
                if (a.is_string() && a.get<std::string>() == audience) audOk = true;
    }
    if (!audOk) return "aud 불일치";
    if (p.contains("azp") && (!p["azp"].is_string() || p["azp"].get<std::string>() != audience)) return "azp 불일치";
    if (!p.contains("exp") || !p["exp"].is_number()) return "exp 없음";
    if (!p.contains("iat") || !p["iat"].is_number()) return "iat 없음";
    if (p["exp"].get<int64_t>() + leewaySec < nowUnix) return "만료된 ID 토큰";
    if (p["iat"].get<int64_t>() - leewaySec > nowUnix) return "iat 가 미래";
    if (!nonce.empty() && JStr(p, "nonce", "") != nonce) return "nonce 불일치";
    out.subject = JStr(p, "sub", "");
    out.email = JStr(p, "email", "");
    if (out.subject.empty()) return "sub 없음";
    return {};
}

std::optional<RsaJwk> FindRsaJwk(std::string_view jwksJson, std::string_view kid) {
    json j = json::parse(jwksJson, nullptr, false);
    if (!j.is_object() || !j.contains("keys") || !j["keys"].is_array()) return std::nullopt;
    for (const auto& k : j["keys"]) {
        if (!k.is_object() || JStr(k, "kty", "") != "RSA") continue;
        if (!kid.empty() && JStr(k, "kid", "") != kid) continue;
        if (k.contains("use") && JStr(k, "use", "") != "sig") continue;
        auto n = Base64UrlDecode(JStr(k, "n", ""));
        auto e = Base64UrlDecode(JStr(k, "e", ""));
        if (!n || !e || n->empty() || e->empty()) continue;
        return RsaJwk{std::move(*n), std::move(*e)};
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// 오류 분류
// ---------------------------------------------------------------------------

const char* ToString(Recovery r) {
    switch (r) {
        case Recovery::None: return "none";
        case Recovery::RetryBackoff: return "retry_backoff";
        case Recovery::ReSignIn: return "re_sign_in";
        case Recovery::UsageLimit: return "usage_limit";
        case Recovery::NotEligible: return "not_eligible";
        case Recovery::Restricted: return "restricted";
        case Recovery::FixRequest: return "fix_request";
        case Recovery::ClientConfig: return "client_config";
        case Recovery::Network: return "network";
        case Recovery::Unknown: return "unknown";
    }
    return "unknown";
}

Recovery ClassifyErrorCode(std::string_view code, int http) {
    if (code == "subscription_sharing_user_not_eligible") return Recovery::NotEligible;
    if (code == "subscription_sharing_usage_limit_exceeded") return Recovery::UsageLimit;
    if (code == "subscription_sharing_usage_unavailable" || code == "subscription_sharing_user_unavailable")
        return Recovery::RetryBackoff;
    if (code == "subscription_sharing_unsupported_capability" || code == "subscription_sharing_route_not_supported")
        return Recovery::FixRequest;
    if (code == "subscription_sharing_invalid_user") return Recovery::ReSignIn;
    if (code == "chatpass_v2_scope_not_authorized" || code == "chatpass_v2_invalid_authorization_context")
        return Recovery::ClientConfig;
    // 코드가 없거나 모르는 코드: HTTP 상태로 (direct-route admission 표)
    switch (http) {
        case 401: return Recovery::ReSignIn;
        case 403: return Recovery::Restricted;
        case 429: return Recovery::UsageLimit;
        case 500: case 502: case 503: case 504: return Recovery::RetryBackoff;
        case 400: return Recovery::FixRequest;
        default: return Recovery::Unknown;
    }
}

ApiError ClassifyHttpError(int http, std::string_view body) {
    ApiError e;
    e.http = http;
    json j = json::parse(body, nullptr, false);
    if (j.is_object()) {
        if (j.contains("error") && j["error"].is_object()) {
            e.code = JStr(j["error"], "code", "");
            e.param = j["error"].contains("param") && j["error"]["param"].is_string() ? j["error"]["param"].get<std::string>() : "";
            e.message = JStr(j["error"], "message", "");
        } else if (j.contains("detail") && j["detail"].is_string()) {
            e.message = j["detail"].get<std::string>();  // 안정 코드 없음 → 진단 문구로만
        }
    }
    e.recovery = ClassifyErrorCode(e.code, http);
    return e;
}

Recovery ClassifyRefreshError(std::string_view err, int http) {
    if (err == "invalid_grant" || err == "invalid_refresh_token" || err == "token_expired" ||
        err == "refresh_token_expired" || err == "refresh_token_invalidated" || err == "refresh_token_reused")
        return Recovery::ReSignIn;
    if (err == "invalid_client") return Recovery::ClientConfig;
    if (http >= 500 || http == 0) return Recovery::RetryBackoff;  // 일시 장애: 자격 증명 유지
    if (http == 400 || http == 401) return Recovery::ReSignIn;
    return Recovery::Unknown;
}

}  // namespace dp::ai
