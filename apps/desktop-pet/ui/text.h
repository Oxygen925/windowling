#pragma once
// 앱 UI 문자열 (UI-01): lang/<코드>.toml 을 읽어 T("key") 로 꺼낸다. 형식 문자열은 TF("key", 값...) —
// 번역 파일의 {} 가 잘못되면(형식 오류) 영어 → 키 이름으로 대체한다.
#include <format>
#include <string>
#include <string_view>

#include "core/i18n.h"
#include "platform/windows/strings.h"

namespace dp::ui {

// language: "auto" | "ko" | "en". 실행 파일 옆 lang 폴더에서 읽는다. 결과 언어 코드를 돌려준다
std::string LoadLanguage(const std::string& language);
const std::string& CurrentLanguage();
const core::Strings& Table();
const core::Strings& EnglishTable();

inline std::wstring T(std::string_view key) { return ToWide(Table().Get(key)); }

template <class... Args>
std::wstring TF(std::string_view key, const Args&... args) {
    for (const core::Strings* t : {&Table(), &EnglishTable()}) {
        try {
            return std::vformat(ToWide(t->Get(key)), std::make_wformat_args(args...));
        } catch (const std::format_error&) {
        }
    }
    return ToWide(std::string(key));
}

}  // namespace dp::ui
