#pragma once
// UI 문자열 표 (UI-01). 실행 파일 옆 lang/<코드>.toml 의 평평한 키("menu.exit") → 문구.
// TOML 표는 점으로 이어 붙인다: [menu] exit = "종료" → "menu.exit". 없는 키는 대체 표(영어) → 키 이름.
// 로그는 번역하지 않는다 (진단용).
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dp::core {

class Strings {
public:
    // 성공하면 빈 문자열, 실패하면 오류 문구 (TOML 문법)
    std::string Load(const std::string& tomlText);
    void SetFallback(const Strings* fallback) { fallback_ = fallback; }
    // 없으면 대체 표, 그래도 없으면 키 이름
    std::string Get(std::string_view key) const;
    bool Has(std::string_view key) const { return map_.contains(std::string(key)); }
    std::vector<std::string> Keys() const;
    size_t Size() const { return map_.size(); }

private:
    std::map<std::string, std::string> map_;
    const Strings* fallback_ = nullptr;
};

// "auto" 이면 Windows 표시 언어(BCP-47, 예 "ko-KR")로 정한다. 지원: ko, en (그 외는 en)
std::string ResolveLanguage(std::string_view setting, std::string_view uiLocale);

}  // namespace dp::core
