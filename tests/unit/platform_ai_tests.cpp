// 플랫폼 단위 테스트 (P3-02): BCrypt·DPAPI·루프백 리스너·WinHTTP 왕복
#include <catch2/catch_test_macros.hpp>

#include <thread>

#include <nlohmann/json.hpp>

#include "ai/siwc_protocol.h"
#include "platform/windows/ai/http_client.h"
#include "platform/windows/ai/mini_http.h"
#include "platform/windows/ai/win_crypto.h"
#include "platform/windows/tts/tts.h"

#include <shellapi.h>

using namespace dp;
using nlohmann::json;

TEST_CASE("PKCE S256 matches RFC 7636 appendix B", "[crypto]") {
    CHECK(crypto::PkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
          "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
    const std::string a = crypto::RandomToken(), b = crypto::RandomToken();
    CHECK(a.size() == 43);
    CHECK(a != b);
    const std::string u = crypto::NewUuidV4();
    CHECK(u.size() == 36);
    CHECK(u[14] == '4');
}

TEST_CASE("RSA RS256 sign and verify", "[crypto]") {
    crypto::RsaKey key;
    REQUIRE(key.Generate(2048));
    const std::string msg = "header.payload";
    auto sig = key.SignSha256(msg);
    REQUIRE(sig.size() == 256);
    CHECK(crypto::RsaVerifySha256(key.N(), key.E(), msg, sig));
    CHECK_FALSE(crypto::RsaVerifySha256(key.N(), key.E(), "header.payloaX", sig));
    sig[10] ^= 1;
    CHECK_FALSE(crypto::RsaVerifySha256(key.N(), key.E(), msg, sig));
    // JWT 경로: JWKS → FindRsaJwk → 검증
    const json jwks = {{"keys", json::array({{{"kty", "RSA"}, {"kid", "k"}, {"n", ai::Base64UrlEncode(key.N().data(), key.N().size())},
                                             {"e", ai::Base64UrlEncode(key.E().data(), key.E().size())}}})}};
    auto jwk = ai::FindRsaJwk(jwks.dump(), "k");
    REQUIRE(jwk);
    CHECK(crypto::RsaVerifySha256(jwk->n, jwk->e, msg, key.SignSha256(msg)));
}

TEST_CASE("DPAPI round trip hides plaintext", "[crypto]") {
    const std::string secret = "mock_rt_secret_value_123";
    auto blob = crypto::ProtectForUser(secret);
    REQUIRE(blob);
    const std::string raw(blob->begin(), blob->end());
    CHECK(raw.find(secret) == std::string::npos);
    auto back = crypto::UnprotectForUser(*blob);
    REQUIRE(back);
    CHECK(*back == secret);
    (*blob)[blob->size() / 2] ^= 0x55;
    CHECK_FALSE(crypto::UnprotectForUser(*blob));
}

TEST_CASE("Loopback callback and WinHTTP round trip", "[net]") {
    net::LoopbackListener l;
    std::string err;
    REQUIRE(l.Listen(0, err));
    const uint16_t port = l.Port();
    REQUIRE(port != 0);
    std::string query;
    bool cancelled = false;
    std::thread server([&] {
        auto q = net::WaitForCallback(l, "/auth/callback", 5000, cancelled);
        if (q) query = *q;
    });
    HttpClient http;
    HttpRequest miss;
    miss.url = "http://127.0.0.1:" + std::to_string(port) + "/favicon.ico";
    CHECK(http.Send(miss).status == 404);  // 다른 경로는 404 후 계속 대기
    HttpRequest hit;
    hit.url = ai::RedirectUri(port) + "?code=abc&state=xyz";
    auto res = http.Send(hit);
    server.join();
    CHECK(res.status == 200);
    CHECK(query == "code=abc&state=xyz");

    // 취소: 다른 스레드에서 Cancel → 대기 즉시 종료
    net::LoopbackListener l2;
    REQUIRE(l2.Listen(0, err));
    std::thread canceller([&] {
        Sleep(100);
        l2.Cancel();
    });
    bool c2 = false;
    auto none = net::WaitForCallback(l2, "/auth/callback", 10000, c2);
    canceller.join();
    CHECK_FALSE(none);
    CHECK(c2);

    // 같은 포트를 두 번 바인드하면 port_in_use
    net::LoopbackListener l3;
    REQUIRE(l3.Listen(0, err));
    net::LoopbackListener l4;
    CHECK_FALSE(l4.Listen(l3.Port(), err));
    CHECK(err == "port_in_use");
}

// ---- 보안 점검 (2026-10-20): TTS 명령 템플릿 — 한 번에 치환, 배치 파일 제한 ----
TEST_CASE("TTS command template expansion", "[tts][security]") {
    std::string err;
    const CommandArgValues v{L"hi \"x\" {out} & calc", L"C:\\t\\a.txt", L"C:\\t\\a.wav", L"a {text}", L"1.00"};
    // 실행 파일: 한 번만 치환 (값 안의 {out}·{text} 는 그대로), 각 값은 한 인수
    const std::wstring exe = ExpandCommandArgs(L"--out {out} --voice {voice} {text}", v, false, err);
    REQUIRE(err.empty());
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW((L"tts.exe " + exe).c_str(), &argc);
    REQUIRE(argc == 6);
    CHECK(std::wstring(argv[2]) == L"C:\\t\\a.wav");
    CHECK(std::wstring(argv[4]) == L"a {text}");
    CHECK(std::wstring(argv[5]) == L"hi \"x\" {out} & calc");
    LocalFree(argv);
    // 옵션처럼 보이는 발화는 앞에 공백 → 옵션으로 읽히지 않음
    const std::wstring dash = ExpandCommandArgs(L"{text}", {L"--output-filename=C:\\x"}, false, err);
    argv = CommandLineToArgvW((L"tts.exe " + dash).c_str(), &argc);
    REQUIRE(argc == 2);
    CHECK(std::wstring(argv[1]) == L" --output-filename=C:\\x");
    LocalFree(argv);
    // 배치: {text}·{voice} 거부, 파일 경로만 따옴표로
    CHECK(ExpandCommandArgs(L"{out} {text}", v, true, err).empty());
    CHECK(err == "batch_text_arg");
    CHECK(ExpandCommandArgs(L"{voice} {out}", v, true, err).empty());
    CHECK(err == "batch_text_arg");
    CHECK(ExpandCommandArgs(L"--in {text_file} --out {out} --speed {speed}", v, true, err) ==
          L"--in \"C:\\t\\a.txt\" --out \"C:\\t\\a.wav\" --speed \"1.00\"");
    CHECK(err.empty());
    // 배치: cmd.exe 특수 문자가 든 경로(사용자 이름 등)는 넘기지 않는다
    for (const wchar_t* bad : {L"C:\\Users\\a&b\\t.wav", L"C:\\%USERNAME%\\t.wav", L"C:\\a!b\\t.wav", L"C:\\a\"b", L"C:\\dir\\"}) {
        CHECK(ExpandCommandArgs(L"{out}", {L"", L"", bad}, true, err).empty());
        CHECK(err == "unsafe_command_value");
    }
}

// ---- 로컬 TTS (P4): 명령행 인자 따옴표, WAV 해석 ----
TEST_CASE("TTS command quoting and WAV parsing", "[tts]") {
    CHECK(QuoteArg(L"plain") == L"plain");
    CHECK(QuoteArg(L"") == L"\"\"");
    CHECK(QuoteArg(L"a b") == L"\"a b\"");
    CHECK(QuoteArg(L"say \"hi\"") == L"\"say \\\"hi\\\"\"");
    CHECK(QuoteArg(L"C:\\dir with space\\") == L"\"C:\\dir with space\\\\\"");
    // 왕복: CommandLineToArgvW 로 되읽으면 같은 값
    for (const std::wstring s : {L"안녕 こんにちは", L"a\\\"b", L"end\\", L"{text} \"x\" \\\\"}) {
        const std::wstring cmd = L"exe " + QuoteArg(s);
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(cmd.c_str(), &argc);
        REQUIRE(argc == 2);
        CHECK(std::wstring(argv[1]) == s);
        LocalFree(argv);
    }

    auto wav = [](uint16_t tag, uint32_t dataLen, size_t actual, bool extensible) {
        std::vector<uint8_t> w;
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back(static_cast<uint8_t>(v >> (8 * i))); };
        auto u16 = [&](uint16_t v) { w.push_back(static_cast<uint8_t>(v)); w.push_back(static_cast<uint8_t>(v >> 8)); };
        w.insert(w.end(), {'R', 'I', 'F', 'F'}); u32(0); w.insert(w.end(), {'W', 'A', 'V', 'E'});
        w.insert(w.end(), {'f', 'm', 't', ' '}); u32(extensible ? 40 : 16);
        u16(extensible ? 0xFFFE : tag); u16(1); u32(24000); u32(48000); u16(2); u16(16);
        if (extensible) { u16(22); u16(16); u32(4); u16(tag); for (int i = 0; i < 14; ++i) w.push_back(0); }
        w.insert(w.end(), {'d', 'a', 't', 'a'}); u32(dataLen);
        w.resize(w.size() + actual, 0);
        return w;
    };
    WAVEFORMATEX f{};
    const uint8_t* d = nullptr;
    uint32_t n = 0;
    REQUIRE(ParseWav(wav(1, 100, 100, false), f, d, n));
    CHECK(f.nSamplesPerSec == 24000);  // 샘플레이트는 헤더 값
    CHECK(n == 100);
    REQUIRE(ParseWav(wav(1, 0xFFFFFFFF, 101, false), f, d, n));  // 스트리밍 서버의 길이 미정 → 남은 길이, 블록 정렬
    CHECK(n == 100);
    REQUIRE(ParseWav(wav(1, 64, 64, true), f, d, n));  // WAVE_FORMAT_EXTENSIBLE(PCM)
    CHECK(f.wFormatTag == WAVE_FORMAT_PCM);
    CHECK(f.cbSize == 0);
    CHECK_FALSE(ParseWav(wav(0x55, 64, 64, false), f, d, n));  // MP3 등은 거부
    const std::vector<uint8_t> notWav = {'I', 'D', '3', ' '};
    CHECK_FALSE(ParseWav(notWav, f, d, n));
}
