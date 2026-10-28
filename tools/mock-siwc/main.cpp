// 모의 SIWC 서버 (P3-04): 127.0.0.1 전용. 실제 OpenAI 와 통신하지 않고 인가·토큰·JWKS·revoke·models·responses(SSE)를
// 문서 계약대로 흉내 내며, /control 로 실패 모드를 바꾸고 /stats 로 받은 요청을 기록해 시나리오가 판정한다.
//   mock-siwc.exe --port-file=<path> [--port=0]
// 제어: GET /control?auth=ok|deny|noplan|badstate&refresh=ok|invalid_grant|unavailable&responses=ok|slow|long|drop|
//        usage_limit|invalid_user|not_eligible|unavailable|incomplete|silent&silent_ms=..&refresh_id=none|ok|bad_sub
//        &revoke=ok|fail&access_ttl=초&text=...&reset=1&expire=1
//        &observe=ok|invalid|silent|reject_format|reject_image|slow  (이미지가 든 요청, P4)
//        &tts=ok|fail|mp3  (POST /v1/audio/speech: OpenAI 호환 로컬 TTS 흉내)
// 로컬 TTS 실행 파일 흉내 (P4): mock-siwc.exe --tts-out=<wav> [--tts-record=<txt>] [--tts-text <text>] [--tts-stdin]
//   받은 텍스트(UTF-16 명령행 또는 UTF-8 stdin)를 record 파일에 UTF-8 로 쓰고, 0.25초 무음 WAV 를 out 에 쓴다
#include <winsock2.h>

#include <windows.h>

#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "ai/responses.h"
#include "ai/siwc_protocol.h"
#include "platform/windows/ai/mini_http.h"
#include "platform/windows/ai/win_crypto.h"

using nlohmann::json;
using namespace dp;

namespace {

int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct PendingCode {
    std::string clientId, challenge, nonce, redirect, scope;
};

struct State {
    std::mutex mu;
    std::map<std::string, std::string> mode;
    std::map<std::string, PendingCode> codes;
    std::map<std::string, int64_t> access;      // token → 만료 시각
    std::map<std::string, std::string> refresh; // token → client_id (유효한 것만)
    std::map<std::string, std::string> clientHost;  // client_id → host id
    int clientSeq = 0;
    json stats = json::object();
    std::string base;
    crypto::RsaKey key;
    std::string Mode(const std::string& k, const std::string& def) {
        auto it = mode.find(k);
        return it == mode.end() ? def : it->second;
    }
    void Count(const std::string& k) { stats[k] = stats.value(k, 0) + 1; }
};
State g;

std::string Jwt(const json& payload) {
    const json header = {{"alg", "RS256"}, {"typ", "JWT"}, {"kid", "mock-key-1"}};
    const std::string h = header.dump(), p = payload.dump();
    const std::string input = ai::Base64UrlEncode(reinterpret_cast<const uint8_t*>(h.data()), h.size()) + "." +
                              ai::Base64UrlEncode(reinterpret_cast<const uint8_t*>(p.data()), p.size());
    const auto sig = g.key.SignSha256(input);
    return input + "." + ai::Base64UrlEncode(sig.data(), sig.size());
}

std::atomic<int> g_requestSeq{0};
std::string RequestIdHeader() { return "x-request-id: req_mock_" + std::to_string(++g_requestSeq) + "\r\n"; }
void Send(SOCKET s, int status, const json& body) {
    net::SendAll(s, net::BuildResponse(status, "application/json", body.dump(), RequestIdHeader()));
}

json IssueTokens(const std::string& clientId, const std::string& scope, const std::string& nonce, bool withId) {
    const int ttl = std::stoi(g.Mode("access_ttl", "3600"));
    const std::string at = "mock_at_" + crypto::RandomToken(16), rt = "mock_rt_" + crypto::RandomToken(16);
    g.access[at] = Now() + ttl;
    g.refresh[rt] = clientId;
    json j = {{"access_token", at}, {"refresh_token", rt}, {"token_type", "Bearer"}, {"expires_in", ttl},
              {"scope", scope}, {"earliest_refresh_at", Now()}};
    if (withId)
        j["id_token"] = Jwt({{"iss", g.base}, {"aud", clientId}, {"sub", "mock-user-1"}, {"email", "pet-tester@example.test"},
                             {"nonce", nonce}, {"iat", Now()}, {"exp", Now() + 3600}});
    return j;
}

// Bearer 확인. 실패하면 응답을 보내고 false
bool CheckBearer(SOCKET s, const net::MiniRequest& r) {
    const std::string auth = r.Header("authorization");
    const std::string tok = auth.rfind("Bearer ", 0) == 0 ? auth.substr(7) : "";
    auto it = g.access.find(tok);
    if (it == g.access.end() || it->second < Now()) {
        g.Count("unauthorized");
        Send(s, 401, {{"detail", "token not accepted"}});
        return false;
    }
    return true;
}

void Sse(SOCKET s, const json& e) { net::SendAll(s, "event: " + e.value("type", "") + "\ndata: " + e.dump() + "\n\n"); }

void Authorize(SOCKET s, const net::MiniRequest& r) {
    auto q = ai::ParseQuery(r.query);
    std::lock_guard lock(g.mu);
    g.Count("authorize");
    const std::string clientId = q["client_id"], redirect = q["redirect_uri"];
    json last = {{"client_id", clientId},
                 {"agent_name_hint", q.contains("agent_name_hint") ? q["agent_name_hint"] : ""},
                 {"ext_agent_host_id", q["ext_agent_host_id"]},
                 {"id_token_hint", q.contains("id_token_hint") && !q["id_token_hint"].empty()},
                 {"login_hint", q.contains("login_hint") && !q["login_hint"].empty()},
                 {"prompt", q.contains("prompt") ? q["prompt"] : ""},
                 {"redirect_uri", redirect}};
    g.stats["last_authorize"] = last;
    const bool ok = q["response_type"] == "code" && redirect.rfind("http://127.0.0.1:", 0) == 0 &&
                    redirect.size() > 14 && redirect.substr(redirect.size() - 14) == "/auth/callback" &&
                    q["scope"] == ai::kScopes && q["resource"] == ai::kResource && q["code_challenge_method"] == "S256" &&
                    !q["state"].empty() && !q["nonce"].empty() && !q["code_challenge"].empty() &&
                    q["ext_agent_host_id"].rfind("urn:uuid:", 0) == 0;
    if (!ok) {
        g.Count("authorize_rejected");
        net::SendAll(s, net::BuildResponse(400, "text/plain", "bad authorize request"));
        return;
    }
    std::string issued = clientId;
    if (clientId == ai::kDynamicClientId) {
        if (q["agent_name_hint"].empty()) {
            net::SendAll(s, net::BuildResponse(400, "text/plain", "agent_name_hint required"));
            return;
        }
        issued = "oaiapp_mock" + std::to_string(++g.clientSeq);
        g.clientHost[issued] = q["ext_agent_host_id"];
    } else if (!g.clientHost.contains(clientId)) {
        // 재시작한 모의 서버: 저장된 client_id 를 알려진 것으로 받아들인다 (실서버는 계정에 등록이 남아 있음)
        g.clientHost[clientId] = q["ext_agent_host_id"];
    }
    const std::string authMode = g.Mode("auth", "ok");
    std::string loc = redirect + "?state=" + ai::UrlEncode(authMode == "badstate" ? "wrong-state" : q["state"]);
    if (authMode == "deny") {
        loc += "&error=access_denied";
    } else {
        const std::string scope = authMode == "noplan" ? "openid profile email offline_access" : std::string(ai::kScopes);
        const std::string code = "mock_code_" + crypto::RandomToken(12);
        g.codes[code] = {issued, q["code_challenge"], q["nonce"], redirect, scope};
        loc += "&code=" + code + "&scope=" + ai::UrlEncode(scope);
        if (clientId == ai::kDynamicClientId) loc += "&client_id=" + issued;
    }
    net::SendAll(s, net::BuildResponse(302, "text/plain", "redirect", "Location: " + loc + "\r\n"));
}

void Token(SOCKET s, const net::MiniRequest& r) {
    auto f = ai::ParseQuery(r.body);
    std::lock_guard lock(g.mu);
    const std::string grant = f["grant_type"];
    if (f["resource"] != ai::kResource) {
        Send(s, 400, {{"error", "invalid_target"}});
        return;
    }
    if (grant == "authorization_code") {
        g.Count("token_code");
        auto it = g.codes.find(f["code"]);
        if (it == g.codes.end() || it->second.clientId != f["client_id"] || it->second.redirect != f["redirect_uri"] ||
            crypto::PkceChallenge(f["code_verifier"]) != it->second.challenge) {
            Send(s, 400, {{"error", "invalid_grant"}});
            return;
        }
        const PendingCode pc = it->second;
        g.codes.erase(it);
        Send(s, 200, IssueTokens(pc.clientId, pc.scope, pc.nonce, true));
    } else if (grant == "refresh_token") {
        g.Count("token_refresh");
        const std::string mode = g.Mode("refresh", "ok");
        if (mode == "unavailable") {
            Send(s, 503, {{"error", "temporarily_unavailable"}});
            return;
        }
        auto it = g.refresh.find(f["refresh_token"]);
        if (mode == "invalid_grant" || it == g.refresh.end() || it->second != f["client_id"]) {
            Send(s, 400, {{"error", "invalid_grant"}});
            return;
        }
        const std::string client = it->second;
        g.refresh.erase(it);  // 교체: 이전 refresh 토큰은 무효
        const std::string idMode = g.Mode("refresh_id", "none");
        json j = IssueTokens(client, std::string(ai::kScopes), "", idMode != "none");
        j.erase("scope");
        if (idMode == "bad_sub")  // 다른 계정의 ID 토큰 (거부되어야 함)
            j["id_token"] = Jwt({{"iss", g.base}, {"aud", client}, {"sub", "someone-else"}, {"iat", Now()}, {"exp", Now() + 3600}});
        Send(s, 200, j);
    } else {
        Send(s, 400, {{"error", "unsupported_grant_type"}});
    }
}

void Revoke(SOCKET s, const net::MiniRequest& r) {
    auto f = ai::ParseQuery(r.body);
    std::lock_guard lock(g.mu);
    g.Count("revoke");
    if (g.Mode("revoke", "ok") == "fail") {
        net::SendAll(s, net::BuildResponse(500, "text/plain", "revoke failed"));
        return;
    }
    if (f["token_type_hint"] == "refresh_token") g.refresh.erase(f["token"]);
    net::SendAll(s, net::BuildResponse(200, "", ""));
}

void Responses(SOCKET s, const net::MiniRequest& r) {
    std::string mode, text;
    int silentMs = 0;
    bool observe = false;
    {
        std::lock_guard lock(g.mu);
        g.Count("responses");
        if (!CheckBearer(s, r)) return;
        json b = json::parse(r.body, nullptr, false);
        bool hasSystem = false;
        if (b.is_object() && b.contains("input") && b["input"].is_array())
            for (const auto& m : b["input"])
                if (m.is_object() && m.value("role", "") == "system") hasSystem = true;
        // P4: 마지막 user 항목의 이미지·텍스트, 구조화 출력 여부 (이미지 내용은 기록하지 않고 형식만 확인)
        bool hasImage = false, jpeg = false, appName = false;
        size_t imageChars = 0;
        std::string detail;
        if (b.is_object() && b.contains("input") && b["input"].is_array())
            for (const auto& m : b["input"])
                if (m.is_object() && m.contains("content") && m["content"].is_array())
                    for (const auto& c : m["content"]) {
                        if (c.value("type", "") == "input_image") {
                            const std::string url = c.value("image_url", "");
                            hasImage = true;
                            imageChars = url.size();
                            jpeg = url.rfind("data:image/jpeg;base64,/9j/", 0) == 0;  // FF D8 FF
                            detail = c.value("detail", "");
                        } else if (c.value("type", "") == "input_text") {
                            appName |= c.value("text", "").find("Foreground app:") != std::string::npos;
                        }
                    }
        const bool hasFormat = b.is_object() && b.contains("text") && b["text"].is_object() && b["text"].contains("format");
        // reasoning.effort (2026-10-21): 받은 값을 기록하고, reasoning=reject 면 400 (모델이 그 effort 를 받지 않는 경우 흉내)
        const std::string effort = b.is_object() && b.contains("reasoning") && b["reasoning"].is_object()
                                       ? b["reasoning"].value("effort", "") : "";
        if (!effort.empty()) g.Count("with_effort");
        if (!effort.empty() && g.Mode("reasoning", "ok") == "reject") {
            g.Count("effort_rejected");
            Send(s, 400, {{"error", {{"code", "unsupported_value"}, {"param", "reasoning.effort"}}}});
            return;
        }
        g.stats["last_responses"] = {{"store", b.is_object() && b.contains("store") ? b["store"] : json()},
                                     {"stream", b.is_object() && b.contains("stream") ? b["stream"] : json()},
                                     {"model", b.is_object() ? b.value("model", "") : ""},
                                     {"has_instructions", b.is_object() && b.contains("instructions")},
                                     {"input_count", b.is_object() && b.contains("input") ? b["input"].size() : 0},
                                     {"has_system_role", hasSystem},
                                     {"has_image", hasImage},
                                     {"image_jpeg", jpeg},
                                     {"image_chars", imageChars},
                                     {"image_detail", detail},
                                     {"has_text_format", hasFormat},
                                     {"effort", effort},
                                     {"has_app_name", appName}};
        if (hasImage) {
            g.Count("observe");
            observe = true;
            const std::string om = g.Mode("observe", "ok");
            if (om == "reject_format" && hasFormat) {
                Send(s, 400, {{"error", {{"code", "subscription_sharing_unsupported_capability"}, {"param", "text.format"}}}});
                return;
            }
            if (om == "reject_image") {
                Send(s, 400, {{"error", {{"code", "subscription_sharing_unsupported_capability"},
                                         {"param", "input[0].content[1].image_url"}}}});
                return;
            }
            text = om == "invalid" ? "오늘 날씨 좋네 (JSON 아님)"
                 : om == "silent"  ? R"({"should_speak":false,"text":"","emotion":"neutral","action":{"type":"none","name":""}})"
                                   : g.Mode("observe_text", R"({"should_speak":true,"text":"모의 관찰: 열심히 하고 있네!","emotion":"happy","action":{"type":"behavior","name":"sit"}})");
            mode = om == "slow" ? "slow_observe" : "ok";
        }
        if (!b.is_object() || b.value("store", true) != false || b.value("stream", false) != true || hasSystem) {
            Send(s, 400, {{"error", {{"code", "subscription_sharing_unsupported_capability"}, {"param", "store"}}}});
            return;
        }
        if (!observe) {
            mode = g.Mode("responses", "ok");
            text = g.Mode("text", "안녕! 나는 모의 응답이야. 오늘도 창 위에서 놀자.");
        }
        silentMs = std::stoi(g.Mode("silent_ms", "10000"));
    }
    if (mode == "unavailable") return Send(s, 503, {{"error", {{"code", "subscription_sharing_usage_unavailable"}}}});
    if (mode == "invalid_user") return Send(s, 401, {{"error", {{"code", "subscription_sharing_invalid_user"}}}});
    if (mode == "not_eligible") return Send(s, 403, {{"error", {{"code", "subscription_sharing_user_not_eligible"}}}});

    net::SendAll(s, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nConnection: close\r\n" +
                        RequestIdHeader() + "\r\n");
    Sse(s, {{"type", "response.created"}, {"response", {{"id", "resp_mock"}}}});
    if (mode == "silent") Sleep(silentMs);  // 첫 글자 전에 오래 조용함 (추론이 긴 모델 흉내)
    int count = 6, delayMs = 40;
    if (mode == "slow") count = 200, delayMs = 100;  // 약 20초
    if (mode == "slow_observe") count = 60, delayMs = 100;  // 약 6초 (관찰 중 중지 시험)
    if (mode == "long") {
        text.clear();
        for (int i = 0; i < 300; ++i) text += "긴 응답 문장입니다. ";  // 약 3000자
        count = 60, delayMs = 5;
    }
    // text 를 count 조각으로 (UTF-8 경계 유지)
    const size_t total = ai::Utf8Length(text);
    std::string rest = text;
    for (int i = 0; i < count && !rest.empty(); ++i) {
        const size_t n = std::max<size_t>(1, total / count + 1);
        std::string piece = ai::Utf8Head(rest, n);
        rest = rest.substr(piece.size());
        if (mode == "slow" && rest.empty()) rest = text;  // 느린 모드는 개수만큼 반복
        Sse(s, {{"type", "response.output_text.delta"}, {"delta", piece}});
        if ((mode == "drop" || mode == "usage_limit" || mode == "incomplete") && i == 2) break;
        Sleep(delayMs);
    }
    if (mode == "drop") return;  // response.completed 없이 연결 종료
    if (mode == "usage_limit")
        return Sse(s, {{"type", "response.failed"},
                       {"response", {{"error", {{"code", "subscription_sharing_usage_limit_exceeded"}}}}}});
    if (mode == "incomplete")
        return Sse(s, {{"type", "response.incomplete"}, {"response", {{"incomplete_details", {{"reason", "max_output_tokens"}}}}}});
    Sse(s, {{"type", "response.completed"}, {"response", {{"id", "resp_mock"}, {"status", "completed"}}}});
}

// 16kHz mono 16-bit 무음 WAV
std::string SilentWav(int ms) {
    const uint32_t rate = 16000, samples = rate * ms / 1000, dataBytes = samples * 2;
    std::string w;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w += static_cast<char>((v >> (8 * i)) & 0xFF); };
    auto u16 = [&](uint16_t v) { w += static_cast<char>(v & 0xFF); w += static_cast<char>(v >> 8); };
    w += "RIFF"; u32(36 + dataBytes); w += "WAVE";
    w += "fmt "; u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    w += "data"; u32(dataBytes);
    w.append(dataBytes, '\0');
    return w;
}

void Speech(SOCKET s, const net::MiniRequest& r) {
    std::string mode;
    {
        std::lock_guard lock(g.mu);
        g.Count("tts");
        json b = json::parse(r.body, nullptr, false);
        json last = json::object();
        if (b.is_object())
            for (auto& [k, v] : b.items()) last[k] = k == "input" ? json(v.is_string() ? v.get<std::string>() : "") : v;
        g.stats["last_tts"] = last;
        mode = g.Mode("tts", "ok");
    }
    if (mode == "fail") return Send(s, 500, {{"detail", "tts engine error"}});
    if (mode == "mp3") return (void)net::SendAll(s, net::BuildResponse(200, "audio/mpeg", "ID3 fake-mp3"));
    net::SendAll(s, net::BuildResponse(200, "audio/wav", SilentWav(250)));
}

int TtsCli() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring out, record;
    std::string text;
    bool fromStdin = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a.rfind(L"--tts-out=", 0) == 0) out = a.substr(10);
        else if (a.rfind(L"--tts-record=", 0) == 0) record = a.substr(13);
        else if (a == L"--tts-text" && i + 1 < argc) {
            const std::wstring t = argv[++i];
            const int n = WideCharToMultiByte(CP_UTF8, 0, t.c_str(), static_cast<int>(t.size()), nullptr, 0, nullptr, nullptr);
            text.resize(n);
            WideCharToMultiByte(CP_UTF8, 0, t.c_str(), static_cast<int>(t.size()), text.data(), n, nullptr, nullptr);
        } else if (a == L"--tts-stdin") fromStdin = true;
    }
    LocalFree(argv);
    if (fromStdin) {
        char buf[4096];
        DWORD n = 0;
        text.clear();
        while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, sizeof(buf), &n, nullptr) && n) text.append(buf, n);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    }
    if (out.empty()) return 2;
    if (!record.empty()) std::ofstream(record, std::ios::binary) << text;
    std::ofstream(out, std::ios::binary) << SilentWav(250);
    return 0;
}

void Handle(SOCKET s) {
    net::MiniRequest r;
    if (!net::ReadRequest(s, r, 10000)) {
        closesocket(s);
        return;
    }
    if (r.path == "/.well-known/openid-configuration") {
        std::lock_guard lock(g.mu);
        g.Count("discovery");
        Send(s, 200, {{"issuer", g.base},
                      {"authorization_endpoint", g.base + "/api/accounts/authorize"},
                      {"token_endpoint", g.base + "/api/accounts/oauth/token"},
                      {"revocation_endpoint", g.base + "/api/accounts/oauth/revoke"},
                      {"jwks_uri", g.base + "/.well-known/jwks.json"},
                      {"id_token_signing_alg_values_supported", json::array({"RS256"})}});
    } else if (r.path == "/api/accounts/authorize") Authorize(s, r);
    else if (r.path == "/api/accounts/oauth/token" && r.method == "POST") Token(s, r);
    else if (r.path == "/api/accounts/oauth/revoke" && r.method == "POST") Revoke(s, r);
    else if (r.path == "/.well-known/jwks.json") {
        std::lock_guard lock(g.mu);
        Send(s, 200, {{"keys", json::array({{{"kty", "RSA"}, {"kid", "mock-key-1"}, {"use", "sig"}, {"alg", "RS256"},
                                             {"n", ai::Base64UrlEncode(g.key.N().data(), g.key.N().size())},
                                             {"e", ai::Base64UrlEncode(g.key.E().data(), g.key.E().size())}}})}});
    } else if (r.path == "/v1/models") {
        std::lock_guard lock(g.mu);
        g.Count("models");
        if (CheckBearer(s, r))
            Send(s, 200, {{"models", json::array({{{"slug", "mock-chat"}, {"display_name", "Mock Chat"}, {"visibility", "list"}},
                                                  {{"slug", "mock-hidden"}, {"display_name", "Hidden"}, {"visibility", "hide"}}})}});
    } else if (r.path == "/v1/responses" && r.method == "POST") {
        Responses(s, r);
    } else if (r.path == "/v1/audio/speech" && r.method == "POST") {
        Speech(s, r);
    } else if (r.path == "/latest.json" || r.path == "/redirect/latest.json" || r.path == "/redirect-evil/latest.json") {
        // 업데이트 확인 (P6-03): 본문은 /control?latest=<url 인코딩 JSON>. 리디렉션 2종 (같은 origin / 다른 곳)
        std::lock_guard lock(g.mu);
        g.Count("latest");
        if (r.path == "/redirect/latest.json") {
            net::SendAll(s, "HTTP/1.1 302 Found\r\nLocation: " + g.base + "/latest.json\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        } else if (r.path == "/redirect-evil/latest.json") {
            net::SendAll(s, "HTTP/1.1 302 Found\r\nLocation: http://evil.invalid/latest.json\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        } else if (auto it = g.mode.find("latest"); it != g.mode.end()) {
            net::SendAll(s, net::BuildResponse(200, "application/json", it->second));
        } else {
            net::SendAll(s, net::BuildResponse(404, "text/plain", "not found"));
        }
    } else if (r.path == "/control") {
        std::lock_guard lock(g.mu);
        for (auto& [k, v] : ai::ParseQuery(r.query)) {
            if (k == "reset") g.mode.clear();
            else if (k == "expire") {
                for (auto& [_, exp] : g.access) exp = 0;  // 발급된 access 토큰 즉시 만료
            } else g.mode[k] = v;
        }
        Send(s, 200, {{"mode", g.mode}});
    } else if (r.path == "/stats") {
        std::lock_guard lock(g.mu);
        Send(s, 200, g.stats);
    } else {
        net::SendAll(s, net::BuildResponse(404, "text/plain", "not found"));
    }
    shutdown(s, SD_SEND);
    closesocket(s);
}

}  // namespace

int main(int argc, char** argv) {
    std::string portFile;
    uint16_t port = 0;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]).rfind("--tts-out=", 0) == 0) return TtsCli();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--port-file=", 0) == 0) portFile = a.substr(12);
        else if (a.rfind("--port=", 0) == 0) port = static_cast<uint16_t>(std::stoi(a.substr(7)));
    }
    if (!g.key.Generate(2048)) {
        std::fprintf(stderr, "RSA 키 생성 실패\n");
        return 2;
    }
    net::LoopbackListener listener;
    std::string err;
    if (!listener.Listen(port, err)) {
        std::fprintf(stderr, "listen 실패: %s\n", err.c_str());
        return 3;
    }
    g.base = "http://127.0.0.1:" + std::to_string(listener.Port());
    std::printf("%s\n", g.base.c_str());
    std::fflush(stdout);
    if (!portFile.empty()) std::ofstream(portFile) << listener.Port();
    for (;;) {
        bool cancelled = false;
        SOCKET c = listener.Accept(-1, cancelled);
        if (c == INVALID_SOCKET) {
            if (cancelled) break;
            continue;
        }
        std::thread([c] { Handle(c); }).detach();
    }
    return 0;
}
