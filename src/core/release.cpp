#include "core/release.h"

#include <cctype>

#include <nlohmann/json.hpp>

namespace dp::core {

namespace {

std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// scheme://host[:port] 부분 (소문자). 형식이 아니면 빈 값
std::string OriginOf(std::string_view url) {
    const size_t sep = url.find("://");
    if (sep == std::string_view::npos) return {};
    const size_t end = url.find_first_of("/?#", sep + 3);
    return Lower(url.substr(0, end));
}

bool SafeUrlChars(std::string_view url) {
    for (unsigned char c : url)
        if (!(std::isalnum(c) || c == '/' || c == ':' || c == '.' || c == '-' || c == '_' || c == '~')) return false;
    return true;
}

}  // namespace

std::optional<std::array<int, 3>> ParseVersion(std::string_view v) {
    std::array<int, 3> out{};
    size_t part = 0, digits = 0;
    int value = 0;
    for (size_t i = 0; i <= v.size(); ++i) {
        if (i == v.size() || v[i] == '.') {
            if (digits == 0 || part > 2) return std::nullopt;
            out[part++] = value;
            value = 0;
            digits = 0;
            continue;
        }
        if (v[i] < '0' || v[i] > '9' || ++digits > 5) return std::nullopt;
        value = value * 10 + (v[i] - '0');
    }
    if (part != 3) return std::nullopt;
    return out;
}

std::optional<int> CompareVersions(std::string_view a, std::string_view b) {
    const auto x = ParseVersion(a), y = ParseVersion(b);
    if (!x || !y) return std::nullopt;
    for (int i = 0; i < 3; ++i)
        if ((*x)[i] != (*y)[i]) return (*x)[i] < (*y)[i] ? -1 : 1;
    return 0;
}

bool IsReleasePageUrl(std::string_view url, std::string_view repo) {
    if (repo.empty() || repo.find('/') == std::string_view::npos || !SafeUrlChars(repo)) return false;
    const std::string prefix = "https://github.com/" + std::string(repo) + "/releases/";
    return url.size() > prefix.size() && url.substr(0, prefix.size()) == prefix && SafeUrlChars(url) &&
           url.find("..") == std::string_view::npos && url.find("://", 8) == std::string_view::npos;
}

bool IsAllowedUpdateRedirect(std::string_view target, std::string_view originalUrl) {
    const std::string origin = OriginOf(target);
    if (origin.empty()) return false;
    if (origin == OriginOf(originalUrl)) return true;  // 같은 origin (시험용 로컬 서버도 이 경우만)
    if (origin == "https://github.com") return true;
    constexpr std::string_view suffix = ".githubusercontent.com";
    return origin.rfind("https://", 0) == 0 && origin.size() > 8 + suffix.size() &&
           origin.compare(origin.size() - suffix.size(), suffix.size(), suffix) == 0 &&
           origin.find(':', 8) == std::string::npos && origin.find('@') == std::string::npos;
}

std::string ParseLatestJson(std::string_view body, std::string_view repo, ReleaseInfo& out) {
    const auto j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object()) return "JSON 아님";
    auto str = [&](const char* k) {
        auto it = j.find(k);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    auto schema = j.find("schema");
    if (schema == j.end() || !schema->is_number_integer() || schema->get<int>() != 1) return "schema 1 아님";
    ReleaseInfo r;
    r.version = str("version");
    r.page = str("page");
    r.zip = str("zip");
    r.sha256 = Lower(str("sha256"));
    r.published = str("published");
    if (!ParseVersion(r.version)) return "버전 형식 오류";
    if (!IsReleasePageUrl(r.page, repo)) return "릴리스 페이지 주소가 아님";
    if (r.sha256.size() != 64) return "sha256 형식 오류";
    for (char c : r.sha256)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return "sha256 형식 오류";
    out = std::move(r);
    return {};
}

}  // namespace dp::core
