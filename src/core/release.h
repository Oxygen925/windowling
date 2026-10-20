#pragma once
// 업데이트 확인 (P6-03): 릴리스의 latest.json 읽기·버전 비교·주소 검사. 앱은 내려받지 않고 릴리스 페이지만 연다
// (2026-10-20 결정: 확인·알림만). 서명이 없으므로 latest.json 의 주소는 "그 저장소의 GitHub 릴리스 페이지" 만 믿는다.
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace dp::core {

struct ReleaseInfo {
    std::string version;    // "0.1.1"
    std::string page;       // https://github.com/<repo>/releases/...
    std::string zip, sha256, published;
};

// "x.y.z" (숫자만, 각 0~99999) → 비교용
std::optional<std::array<int, 3>> ParseVersion(std::string_view v);
// a<b → -1, 같음 → 0, a>b → 1. 둘 중 하나가 형식에 맞지 않으면 nullopt
std::optional<int> CompareVersions(std::string_view a, std::string_view b);

// https://github.com/<repo>/releases/ 로 시작하고 안전한 문자만 (repo = "소유자/저장소")
bool IsReleasePageUrl(std::string_view url, std::string_view repo);
// latest.json 을 받으러 갈 때 따라갈 수 있는 리디렉션: https 의 github.com·*.githubusercontent.com,
// 또는 처음 요청과 같은 origin (시험용 로컬 서버)
bool IsAllowedUpdateRedirect(std::string_view target, std::string_view originalUrl);

// latest.json → ReleaseInfo. 문제가 있으면 이유 (빈 문자열 = 통과): schema 1, 버전 형식, 페이지 주소, sha256 64자 16진수
std::string ParseLatestJson(std::string_view body, std::string_view repo, ReleaseInfo& out);

}  // namespace dp::core
