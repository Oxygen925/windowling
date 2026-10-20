// P6-03: 업데이트 확인 — latest.json·버전 비교·주소 검사
#include <catch2/catch_test_macros.hpp>

#include "core/release.h"

using namespace dp::core;

TEST_CASE("Version parsing and comparison", "[release]") {
    CHECK(ParseVersion("0.1.0").has_value());
    CHECK(ParseVersion("10.20.30") == std::array<int, 3>{10, 20, 30});
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "1.2.x", "v1.2.3", "1..3", "1.2.3-beta", "123456.0.0", " 1.2.3"})
        CHECK_FALSE(ParseVersion(bad).has_value());
    CHECK(CompareVersions("0.1.0", "0.1.1") == -1);
    CHECK(CompareVersions("0.10.0", "0.9.9") == 1);
    CHECK(CompareVersions("1.0.0", "1.0.0") == 0);
    CHECK_FALSE(CompareVersions("1.0", "1.0.0").has_value());
}

TEST_CASE("Release page and redirect checks", "[release][security]") {
    const std::string repo = "owner/desktop-pet";
    CHECK(IsReleasePageUrl("https://github.com/owner/desktop-pet/releases/tag/v0.1.1", repo));
    for (const char* bad : {"http://github.com/owner/desktop-pet/releases/tag/v0.1.1",
                            "https://github.com/other/desktop-pet/releases/tag/v0.1.1",
                            "https://github.com/owner/desktop-pet/issues/1",
                            "https://github.com/owner/desktop-pet/releases/",
                            "https://github.com/owner/desktop-pet/releases/../../evil",
                            "https://github.com/owner/desktop-pet/releases/tag/v1?x=https://evil",
                            "https://github.com/owner/desktop-pet/releases/tag/v1 x",
                            "https://github.com.evil.com/owner/desktop-pet/releases/tag/v1",
                            "https://github.com/owner/desktop-pet/releases/tag/https://evil"})
        CHECK_FALSE(IsReleasePageUrl(bad, repo));
    CHECK_FALSE(IsReleasePageUrl("https://github.com/owner/desktop-pet/releases/tag/v1", ""));

    const std::string orig = "https://github.com/owner/desktop-pet/releases/latest/download/latest.json";
    CHECK(IsAllowedUpdateRedirect("https://github.com/owner/desktop-pet/releases/download/v0.1.1/latest.json", orig));
    CHECK(IsAllowedUpdateRedirect("https://objects.githubusercontent.com/github-production-release-asset/1", orig));
    CHECK(IsAllowedUpdateRedirect("https://release-assets.githubusercontent.com/x", orig));
    CHECK_FALSE(IsAllowedUpdateRedirect("http://objects.githubusercontent.com/x", orig));
    CHECK_FALSE(IsAllowedUpdateRedirect("https://evil.com/githubusercontent.com", orig));
    CHECK_FALSE(IsAllowedUpdateRedirect("https://githubusercontent.com.evil.com/x", orig));
    CHECK_FALSE(IsAllowedUpdateRedirect("https://x.githubusercontent.com:8443/x", orig));
    // 시험용 로컬 서버: 같은 origin 만
    CHECK(IsAllowedUpdateRedirect("http://127.0.0.1:5000/latest.json", "http://127.0.0.1:5000/redir"));
    CHECK_FALSE(IsAllowedUpdateRedirect("http://127.0.0.1:5001/latest.json", "http://127.0.0.1:5000/redir"));
}

TEST_CASE("latest.json parsing", "[release]") {
    const std::string repo = "owner/desktop-pet";
    const std::string hash(64, 'a');
    ReleaseInfo r;
    const std::string ok = R"({"schema":1,"version":"0.1.1","published":"2026-10-20T00:00:00Z","page":"https://github.com/owner/desktop-pet/releases/tag/v0.1.1","zip":"DesktopPet-0.1.1-win-x64.zip","sha256":")" + hash + R"("})";
    REQUIRE(ParseLatestJson(ok, repo, r).empty());
    CHECK(r.version == "0.1.1");
    CHECK(r.zip == "DesktopPet-0.1.1-win-x64.zip");
    CHECK_FALSE(ParseLatestJson("not json", repo, r).empty());
    CHECK_FALSE(ParseLatestJson(R"({"schema":2,"version":"0.1.1"})", repo, r).empty());
    CHECK_FALSE(ParseLatestJson(R"({"schema":1,"version":null,"page":null,"sha256":null})", repo, r).empty());
    std::string evilPage = ok;
    evilPage.replace(evilPage.find("https://github.com/owner"), 24, "https://evil.example/own");
    CHECK_FALSE(ParseLatestJson(evilPage, repo, r).empty());
    std::string badHash = ok;
    badHash.replace(badHash.find(hash), 64, std::string(64, 'z'));
    CHECK_FALSE(ParseLatestJson(badHash, repo, r).empty());
}
