#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "ai/jwt.h"
#include "ai/responses.h"
#include "ai/siwc_protocol.h"
#include "core/character/package.h"
#include "core/config.h"

using namespace dp;
using namespace dp::ai;
using nlohmann::json;

namespace {
std::string B64(const std::string& s) { return Base64UrlEncode(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
std::string MakeJwt(const json& header, const json& payload) {
    return B64(header.dump()) + "." + B64(payload.dump()) + "." + B64("sig");
}
}  // namespace

TEST_CASE("Base64url and URL encoding", "[ai]") {
    CHECK(B64("") == "");
    CHECK(B64("f") == "Zg");
    CHECK(B64("foob") == "Zm9vYg");
    CHECK(B64("\xfb\xff") == "-_8");
    auto d = Base64UrlDecode("-_8");
    REQUIRE(d);
    CHECK(*d == std::vector<uint8_t>{0xfb, 0xff});
    CHECK_FALSE(Base64UrlDecode("a"));
    CHECK_FALSE(Base64UrlDecode("ab$c"));
    CHECK(UrlEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
    CHECK(UrlDecode("a+b%26c") == "a b&c");
    auto q = ParseQuery("?code=x%2By&state=s1&empty=&dup=1&dup=2");
    CHECK(q["code"] == "x+y");
    CHECK(q["state"] == "s1");
    CHECK(q["empty"] == "");
    CHECK(q["dup"] == "1");
}

TEST_CASE("Authorize URL registration vs returning", "[ai]") {
    const auto ep = SiwcEndpoints::Production();
    CHECK(ep.authorize == "https://auth.openai.com/api/accounts/authorize");
    CHECK(ep.IsProduction());
    CHECK_FALSE(SiwcEndpoints::ForBase("http://127.0.0.1:5000/").IsProduction());
    CHECK(SiwcEndpoints::ForBase("http://127.0.0.1:5000/").token == "http://127.0.0.1:5000/api/accounts/oauth/token");

    AuthorizeParams p;
    p.agentName = "Desktop Pet";
    p.hostId = "urn:uuid:1234";
    p.redirectUri = RedirectUri(1455);
    p.state = "st";
    p.nonce = "no";
    p.codeChallenge = "ch";
    p.idTokenHint = "should-not-appear";
    const std::string reg = BuildAuthorizeUrl(ep, p);
    auto q = ParseQuery(reg.substr(reg.find('?') + 1));
    CHECK(q["client_id"] == "dynamic_agent_client");
    CHECK(q["agent_name_hint"] == "Desktop Pet");
    CHECK(q["ext_agent_host_id"] == "urn:uuid:1234");
    CHECK(q["redirect_uri"] == "http://127.0.0.1:1455/auth/callback");
    CHECK(q["scope"] == "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct");
    CHECK(q["resource"] == "https://api.openai.com/v1");
    CHECK(q["response_type"] == "code");
    CHECK(q["code_challenge_method"] == "S256");
    CHECK_FALSE(q.contains("id_token_hint"));  // 최초 등록에는 보내지 않음

    p.clientId = "oaiapp_abc";
    p.loginHint = "a@b.c";
    const std::string again = BuildAuthorizeUrl(ep, p);
    auto q2 = ParseQuery(again.substr(again.find('?') + 1));
    CHECK(q2["client_id"] == "oaiapp_abc");
    CHECK_FALSE(q2.contains("agent_name_hint"));  // 다시 로그인: 생략
    CHECK(q2["id_token_hint"] == "should-not-appear");
    CHECK(q2["login_hint"] == "a@b.c");
}

TEST_CASE("Discovery and reconsent", "[ai]") {
    auto ep = SiwcEndpoints::ForBase("http://127.0.0.1:5000");
    CHECK(ep.DiscoveryUrl() == "http://127.0.0.1:5000/.well-known/openid-configuration");
    const json d = {{"issuer", "http://127.0.0.1:5000"},
                    {"authorization_endpoint", "http://127.0.0.1:5000/a"},
                    {"token_endpoint", "http://127.0.0.1:5000/t"},
                    {"revocation_endpoint", "http://127.0.0.1:5000/r"},
                    {"jwks_uri", "http://127.0.0.1:5000/k"}};
    CHECK(ApplyDiscovery(ep, d.dump()).empty());
    CHECK(ep.revoke == "http://127.0.0.1:5000/r");
    CHECK(ep.api == "http://127.0.0.1:5000/v1");  // API 주소는 discovery 대상 아님
    auto ep2 = SiwcEndpoints::Production();
    json evil = d;
    evil["issuer"] = "https://auth.openai.com";
    evil["token_endpoint"] = "https://evil.example/t";  // 다른 origin 은 거부
    CHECK_FALSE(ApplyDiscovery(ep2, evil.dump()).empty());
    CHECK(ep2.token == SiwcEndpoints::Production().token);
    CHECK_FALSE(ApplyDiscovery(ep2, d.dump()).empty());  // issuer 불일치

    AuthorizeParams p;
    p.clientId = "oaiapp_1";
    p.hostId = "urn:uuid:x";
    p.redirectUri = RedirectUri(1455);
    p.reconsent = true;
    const std::string again = BuildAuthorizeUrl(SiwcEndpoints::Production(), p);
    auto q = ParseQuery(again.substr(again.find('?') + 1));
    CHECK(q["prompt"] == "consent");
    p.clientId.clear();  // 최초 등록에는 보내지 않음
    const std::string reg = BuildAuthorizeUrl(SiwcEndpoints::Production(), p);
    CHECK(reg.find("prompt=") == std::string::npos);
}

TEST_CASE("Callback parsing validates state first", "[ai]") {
    using K = CallbackResult::Kind;
    CHECK(ParseCallback("code=c&state=bad&client_id=oaiapp_1", "good", "").kind == K::Invalid);
    CHECK(ParseCallback("error=access_denied&state=bad", "good", "").kind == K::Invalid);  // state 불일치면 거부도 믿지 않음
    CHECK(ParseCallback("error=access_denied&state=good", "good", "").kind == K::Denied);
    CHECK(ParseCallback("code=c&state=good", "good", "").kind == K::Invalid);  // 최초 등록인데 client_id 없음
    CHECK(ParseCallback("code=c&state=good&client_id=dynamic_agent_client", "good", "").kind == K::Invalid);
    auto ok = ParseCallback("code=c&state=good&client_id=oaiapp_1&scope=openid", "good", "");
    CHECK(ok.kind == K::Code);
    CHECK(ok.clientId == "oaiapp_1");
    auto re = ParseCallback("code=c&state=good", "good", "oaiapp_9");  // 다시 로그인: client_id 생략 가능
    CHECK(re.kind == K::Code);
    CHECK(re.clientId == "oaiapp_9");
    CHECK(ParseCallback("code=c&state=good&client_id=oaiapp_other", "good", "oaiapp_9").kind == K::Invalid);
    CHECK(ParseCallback("state=good&client_id=oaiapp_1", "good", "").kind == K::Invalid);  // code 없음
}

TEST_CASE("Token form bodies and responses", "[ai]") {
    auto f = ParseQuery(CodeExchangeBody("oaiapp_1", "c0de", "ver", "http://127.0.0.1:1455/auth/callback"));
    CHECK(f["grant_type"] == "authorization_code");
    CHECK(f["client_id"] == "oaiapp_1");
    CHECK(f["code_verifier"] == "ver");
    CHECK(f["redirect_uri"] == "http://127.0.0.1:1455/auth/callback");
    CHECK(f["resource"] == "https://api.openai.com/v1");
    auto rf = ParseQuery(RefreshBody("oaiapp_1", "rt"));
    CHECK(rf["grant_type"] == "refresh_token");
    CHECK(rf["refresh_token"] == "rt");
    CHECK_FALSE(rf.contains("scope"));  // 생략 → 기존 권한 유지
    auto rv = ParseQuery(RevokeBody("oaiapp_1", "rt"));
    CHECK(rv["token_type_hint"] == "refresh_token");
    CHECK(rv["token"] == "rt");

    auto t = ParseTokenResponse(200, R"({"access_token":"a","refresh_token":"r","id_token":"i","expires_in":3600,
        "scope":"openid email chatgpt.tokens.use.direct","token_type":"Bearer"})", 1000);
    REQUIRE(t.tokens);
    CHECK(t.tokens->expiresAt == 4600);
    CHECK(t.tokens->HasPlanUsage());
    CHECK_FALSE(NeedsRefresh(*t.tokens, 4000));
    CHECK(NeedsRefresh(*t.tokens, 4500));  // 만료 120초 전부터 갱신
    // 갱신 응답에 id_token·scope 가 없으면 이전 값 유지
    auto r = ParseTokenResponse(200, R"({"access_token":"a2","refresh_token":"r2","expires_in":60})", 2000, &*t.tokens);
    REQUIRE(r.tokens);
    CHECK(r.tokens->idToken == "i");
    CHECK(r.tokens->refreshToken == "r2");
    CHECK(r.tokens->HasPlanUsage());
    auto noPlan = ParseTokenResponse(200, R"({"access_token":"a","scope":"openid profile email offline_access"})", 0);
    CHECK_FALSE(noPlan.tokens->HasPlanUsage());
    auto bad = ParseTokenResponse(400, R"({"error":"invalid_grant"})", 0);
    CHECK_FALSE(bad.tokens);
    CHECK(bad.error == "invalid_grant");
    CHECK(ParseTokenResponse(503, "oops", 0).error == "http_503");
    CHECK(ParseTokenResponse(200, "{}", 0).error == "invalid_response");
}

TEST_CASE("ID token claims validation", "[ai]") {
    const json h = {{"alg", "RS256"}, {"kid", "k1"}};
    json p = {{"iss", "https://auth.openai.com"}, {"aud", "oaiapp_1"}, {"exp", 2000}, {"iat", 900}, {"nonce", "n1"},
              {"sub", "user-1"}, {"email", "a@b.c"}};
    auto with = [&](const char* k, const json& v) {
        json c = p;
        c[k] = v;
        return c;
    };
    auto check = [&](const json& hh, const json& pp, int64_t now = 1000) {
        auto jwt = ParseJwt(MakeJwt(hh, pp));
        REQUIRE(jwt);
        Identity id;
        return ValidateIdTokenClaims(*jwt, "https://auth.openai.com", "oaiapp_1", "n1", now, id);
    };
    CHECK(check(h, p).empty());
    CHECK_FALSE(check({{"alg", "none"}}, p).empty());
    CHECK_FALSE(check(h, with("iss", "https://evil")).empty());
    CHECK_FALSE(check(h, with("aud", "oaiapp_2")).empty());
    CHECK(check(h, with("aud", json::array({"x", "oaiapp_1"}))).empty());
    CHECK_FALSE(check(h, with("nonce", "n2")).empty());
    CHECK(check(h, p, 2000 + 59).empty());          // 허용 오차 60초 안
    CHECK_FALSE(check(h, p, 2000 + 61).empty());    // 만료
    CHECK_FALSE(check(h, with("iat", 1100)).empty());  // iat 가 미래 (now=1000)
    json noIat = p;
    noIat.erase("iat");
    CHECK_FALSE(check(h, noIat).empty());
    CHECK(check(h, with("azp", "oaiapp_1")).empty());
    CHECK_FALSE(check(h, with("azp", "oaiapp_2")).empty());
    {
        auto jwt = ParseJwt(MakeJwt(h, with("nonce", "other")));
        Identity id;
        CHECK(ValidateIdTokenClaims(*jwt, "https://auth.openai.com", "oaiapp_1", "", 1000, id).empty());  // 갱신: nonce 생략
    }
    CHECK_FALSE(check(h, with("sub", "")).empty());
    CHECK_FALSE(ParseJwt("a.b"));
    CHECK_FALSE(ParseJwt("a.b.c.d"));
    auto jwt = ParseJwt(MakeJwt(h, p));
    CHECK(jwt->signingInput == MakeJwt(h, p).substr(0, MakeJwt(h, p).rfind('.')));

    auto k = FindRsaJwk(R"({"keys":[{"kty":"EC","kid":"k1"},{"kty":"RSA","kid":"k1","use":"sig","n":"AQAB","e":"AQAB"}]})", "k1");
    REQUIRE(k);
    CHECK(k->e == std::vector<uint8_t>{1, 0, 1});
    CHECK_FALSE(FindRsaJwk(R"({"keys":[{"kty":"RSA","kid":"k2","n":"AQAB","e":"AQAB"}]})", "k1"));
}

TEST_CASE("Error classification table", "[ai]") {
    CHECK(ClassifyErrorCode("subscription_sharing_user_not_eligible", 403) == Recovery::NotEligible);
    CHECK(ClassifyErrorCode("subscription_sharing_usage_limit_exceeded", 0) == Recovery::UsageLimit);
    CHECK(ClassifyErrorCode("subscription_sharing_usage_unavailable", 503) == Recovery::RetryBackoff);
    CHECK(ClassifyErrorCode("subscription_sharing_user_unavailable", 503) == Recovery::RetryBackoff);
    CHECK(ClassifyErrorCode("subscription_sharing_unsupported_capability", 400) == Recovery::FixRequest);
    CHECK(ClassifyErrorCode("subscription_sharing_route_not_supported", 403) == Recovery::FixRequest);
    CHECK(ClassifyErrorCode("subscription_sharing_invalid_user", 401) == Recovery::ReSignIn);
    CHECK(ClassifyErrorCode("chatpass_v2_scope_not_authorized", 403) == Recovery::ClientConfig);
    CHECK(ClassifyErrorCode("", 401) == Recovery::ReSignIn);
    CHECK(ClassifyErrorCode("", 403) == Recovery::Restricted);
    CHECK(ClassifyErrorCode("", 503) == Recovery::RetryBackoff);
    auto e = ClassifyHttpError(400, R"({"error":{"code":"subscription_sharing_unsupported_capability","param":"temperature"}})");
    CHECK(e.param == "temperature");
    CHECK(e.recovery == Recovery::FixRequest);
    auto d = ClassifyHttpError(403, R"({"detail":"region"})");
    CHECK(d.code.empty());
    CHECK(d.message == "region");
    CHECK(d.recovery == Recovery::Restricted);
    CHECK(ClassifyRefreshError("invalid_grant", 400) == Recovery::ReSignIn);
    CHECK(ClassifyRefreshError("refresh_token_reused", 400) == Recovery::ReSignIn);
    CHECK(ClassifyRefreshError("invalid_client", 401) == Recovery::ClientConfig);
    CHECK(ClassifyRefreshError("", 503) == Recovery::RetryBackoff);  // 일시 장애: 토큰 유지
    CHECK(ClassifyRefreshError("", 0) == Recovery::RetryBackoff);
}

TEST_CASE("SSE parser handles any chunking", "[ai]") {
    const std::string stream =
        ": comment\r\nevent: response.output_text.delta\r\ndata: {\"type\":\"response.output_text.delta\",\"delta\":\"안녕\"}\r\n\r\n"
        "event: x\ndata: line1\ndata: line2\n\n"
        "data: {\"type\":\"response.completed\"}\r\r";
    for (size_t step : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, stream.size()}) {
        SseParser p;
        std::vector<SseEvent> ev;
        for (size_t i = 0; i < stream.size(); i += step) p.Feed(std::string_view(stream).substr(i, step), ev);
        REQUIRE(ev.size() == 3);
        CHECK(ev[0].event == "response.output_text.delta");
        CHECK(ev[1].data == "line1\nline2");
        CHECK(InterpretEvent(ev[0]).kind == StreamUpdate::Kind::Delta);
        CHECK(InterpretEvent(ev[0]).text == "안녕");
        CHECK(InterpretEvent(ev[2]).kind == StreamUpdate::Kind::Completed);
    }
    SseParser p;
    std::vector<SseEvent> ev;
    p.Feed("data: unterminated", ev);
    p.Finish(ev);
    CHECK(ev.empty());  // 빈 줄로 끝나지 않은 이벤트는 버림
}

TEST_CASE("Stream event interpretation", "[ai]") {
    auto f = InterpretEvent({"", R"({"type":"response.failed","response":{"error":{"code":"subscription_sharing_usage_limit_exceeded"}}})"});
    CHECK(f.kind == StreamUpdate::Kind::Failed);
    CHECK(f.code == "subscription_sharing_usage_limit_exceeded");
    auto inc = InterpretEvent({"", R"({"type":"response.incomplete","response":{"incomplete_details":{"reason":"max_output_tokens"}}})"});
    CHECK(inc.kind == StreamUpdate::Kind::Incomplete);
    CHECK(inc.code == "max_output_tokens");
    CHECK(InterpretEvent({"error", R"({"type":"error","code":"x","message":"m"})"}).code == "x");
    CHECK(InterpretEvent({"", "not json"}).kind == StreamUpdate::Kind::Ignore);
    CHECK(InterpretEvent({"", "[DONE]"}).kind == StreamUpdate::Kind::Ignore);
    CHECK(InterpretEvent({"", R"({"type":"response.created"})"}).kind == StreamUpdate::Kind::Ignore);
}

TEST_CASE("Responses request and models", "[ai]") {
    const auto body = json::parse(BuildResponsesRequest("m1", "be nice", {{"user", "hi"}, {"assistant", "yo"}, {"user", "q"}}));
    CHECK(body["store"] == false);
    CHECK(body["stream"] == true);
    CHECK(body["model"] == "m1");
    CHECK(body["instructions"] == "be nice");
    CHECK(body["input"].size() == 3);
    CHECK(body["input"][1]["role"] == "assistant");
    for (const char* banned : {"max_output_tokens", "temperature", "previous_response_id", "user", "metadata"})
        CHECK_FALSE(body.contains(banned));  // preview-limitations 미지원 필드
    auto m = ParseModels(R"({"models":[{"slug":"a","display_name":"A","visibility":"list"},{"slug":"b","visibility":"hide"},
        {"slug":"c","visibility":"list"}]})");
    REQUIRE(m.size() == 2);
    CHECK(m[1].displayName == "c");
    CHECK(ParseModels("{}").empty());
}

TEST_CASE("Conversation history trimming", "[ai]") {
    Conversation c(2);
    auto t = c.BeginTurn("q1");
    CHECK(t.size() == 1);
    c.AppendReply("a1");
    c.EndTurn(true);
    c.BeginTurn("q2");
    c.EndTurn(false);  // 응답 없음 → 질문도 지움
    CHECK(c.History().size() == 2);
    c.BeginTurn("q3");
    c.AppendReply("a3");
    c.EndTurn(true);
    c.BeginTurn("q4");
    c.AppendReply("a4");
    c.EndTurn(true);
    REQUIRE(c.History().size() == 4);  // 최근 2턴
    CHECK(c.History().front().text == "q3");
    auto t5 = c.BeginTurn("q5");
    CHECK(t5.front().role == "user");  // 항상 user 로 시작
    CHECK(t5.size() == 3);
    CHECK(BuildInstructions("Orange", "persona text").find("persona text") != std::string::npos);
}

TEST_CASE("UTF-8 helpers", "[ai]") {
    const std::string s = "가나다abc";
    CHECK(Utf8Length(s) == 6);
    CHECK(Utf8Head(s, 2) == "가나");
    CHECK(Utf8Tail(s, 4) == "다abc");
    CHECK(Utf8Tail(s, 10) == s);
    CHECK(Utf8Sanitize("a\xff" "b") == "a\xEF\xBF\xBD" "b");
    CHECK(Utf8Sanitize("\xEA\xB0") == "\xEF\xBF\xBD\xEF\xBF\xBD");  // 잘린 시퀀스
}

TEST_CASE("Config ai section and hotkey", "[config]") {
    auto d = core::ParseConfig("");
    CHECK_FALSE(d.config.ai.enabled);
    CHECK(d.config.ai.historyTurns == 20);
    CHECK(d.config.ai.firstTokenTimeoutSec == 120);
    CHECK(d.config.ai.requestTimeoutSec == 300);
    CHECK(d.config.ai.callbackPort == 1455);
    auto to = core::ParseConfig("[ai]\nfirst_token_timeout_sec = 90\nrequest_timeout_sec = 30\n");
    CHECK(to.config.ai.requestTimeoutSec == 90);  // 전체 < 첫 응답 → 맞춤
    CHECK(to.warnings.size() == 1);
    CHECK(d.config.ai.hotkey == "Ctrl+Alt+P");
    auto r = core::ParseConfig("[ai]\nenabled = true\nhotkey = \"Ctrl+Shift+F9\"\nmodel = \"m\"\nhistory_turns = 4\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.ai.enabled);
    CHECK(r.config.ai.model == "m");
    CHECK(r.config.ai.historyTurns == 4);
    auto bad = core::ParseConfig("[ai]\nhotkey = \"P\"\nhistory_turns = 0\n");
    CHECK(bad.config.ai.hotkey == "Ctrl+Alt+P");
    CHECK(bad.warnings.size() == 2);
    auto round = core::ParseConfig(core::SerializeConfig(r.config));
    CHECK(round.warnings.empty());
    CHECK(round.config.ai == r.config.ai);

    auto h = core::ParseHotkey("ctrl + alt + p");
    REQUIRE(h);
    CHECK((h->ctrl && h->alt && !h->shift && h->key == 'P'));
    auto f = core::ParseHotkey("F8");
    REQUIRE(f);
    CHECK(f->fkey == 8);
    CHECK_FALSE(core::ParseHotkey("Ctrl+Alt"));
    CHECK_FALSE(core::ParseHotkey("Ctrl+Foo"));
    CHECK_FALSE(core::ParseHotkey("Shift+A"));
    CHECK_FALSE(core::ParseHotkey("F13"));
}

TEST_CASE("Shipped characters persona and talk", "[package]") {
    for (const char* id : {"test-orange", "test-blue"}) {
        auto r = core::LoadPackage(std::filesystem::path(DP_SOURCE_DIR) / "characters" / id);
        REQUIRE(r.package);
        CHECK_FALSE(r.package->persona.empty());
        CHECK(r.package->sprite.clips.count("talk") == 1);
    }
    // 캐릭터 음성 [voice] (P4, B12): test-blue 만 지정
    auto blue = core::LoadPackage(std::filesystem::path(DP_SOURCE_DIR) / "characters" / "test-blue");
    REQUIRE(blue.package);
    CHECK(blue.package->voice.aiVoice == "blue-voice");
    CHECK(blue.package->voice.rate == 1.1);
    auto orange = core::LoadPackage(std::filesystem::path(DP_SOURCE_DIR) / "characters" / "test-orange");
    REQUIRE(orange.package);
    CHECK(orange.package->voice.aiVoice.empty());
    CHECK(orange.package->voice.rate == 0);
}

// 보안 점검 (2026-10-20): 서버 응답·모델 출력의 null·다른 타입은 예외 없이 기본값 (작업 스레드 종료 방지)
TEST_CASE("Untrusted JSON with null or wrong types does not throw", "[ai][security]") {
    ApiError e;
    REQUIRE_NOTHROW(e = ClassifyHttpError(500, R"({"error":{"message":null,"type":"server_error","param":null,"code":null}})"));
    CHECK(e.code.empty());
    REQUIRE_NOTHROW(e = ClassifyHttpError(400, R"({"error":{"message":7,"code":["x"]}})"));
    TokenResult t;
    REQUIRE_NOTHROW(t = ParseTokenResponse(400, R"({"error":{"code":"refresh_token_expired"}})", 0));
    CHECK(t.error == "refresh_token_expired");
    REQUIRE_NOTHROW(t = ParseTokenResponse(200, R"({"access_token":"a","refresh_token":null,"id_token":null,"token_type":null})", 0));
    CHECK(t.error.empty());
    StreamUpdate u;
    REQUIRE_NOTHROW(u = InterpretEvent({"", R"({"type":"response.incomplete","response":{"incomplete_details":{"reason":null}}})"}));
    CHECK(u.kind == StreamUpdate::Kind::Incomplete);
    std::vector<ModelInfo> models;
    REQUIRE_NOTHROW(models = ParseModels(R"({"models":[{"slug":null,"visibility":null},{"slug":"m","display_name":5,"visibility":"list"}]})"));
}
