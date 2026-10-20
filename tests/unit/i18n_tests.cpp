// UI-01: 언어 파일 — 키 집합·자리표시자 개수가 언어끼리 같은지, 대체 규칙
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/i18n.h"

using namespace dp;

namespace {
core::Strings LoadLang(const char* code) {
    std::ifstream in(std::filesystem::path(DP_SOURCE_DIR) / "lang" / (std::string(code) + ".toml"), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    core::Strings s;
    REQUIRE(s.Load(ss.str()).empty());
    return s;
}
size_t Placeholders(const std::string& s) {
    size_t n = 0;
    for (size_t at = s.find("{}"); at != std::string::npos; at = s.find("{}", at + 2)) ++n;
    return n;
}
}  // namespace

TEST_CASE("Language files have the same keys and placeholders", "[i18n]") {
    const auto ko = LoadLang("ko"), en = LoadLang("en");
    CHECK(ko.Keys() == en.Keys());
    for (const auto& k : en.Keys()) {
        INFO(k);
        CHECK(Placeholders(ko.Get(k)) == Placeholders(en.Get(k)));
    }
    // SIWC UI 지침 문구는 한국어 UI 에서도 영어 (2026-10-19 결정)
    CHECK(ko.Get("chat.continue") == "Continue with ChatGPT");
    CHECK(ko.Get("chat.got_it") == "Got it");
}

TEST_CASE("Strings fallback and language resolution", "[i18n]") {
    core::Strings en, ko;
    REQUIRE(en.Load("[a]\nb = \"B\"\nonly_en = \"E\"\n").empty());
    REQUIRE(ko.Load("[a]\nb = \"비\"\n").empty());
    ko.SetFallback(&en);
    CHECK(ko.Get("a.b") == "비");
    CHECK(ko.Get("a.only_en") == "E");
    CHECK(ko.Get("missing.key") == "missing.key");
    CHECK_FALSE(ko.Load("broken = ").empty());
    CHECK(core::ResolveLanguage("auto", "ko-KR") == "ko");
    CHECK(core::ResolveLanguage("auto", "ja-JP") == "en");
    CHECK(core::ResolveLanguage("en", "ko-KR") == "en");
}
