#include "ui/text.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "diagnostics/log.h"

namespace dp::ui {

namespace {
core::Strings g_table, g_english;
std::string g_lang = "en";

std::filesystem::path LangDir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path() / L"lang";
}

bool LoadFile(const std::string& code, core::Strings& out) {
    std::ifstream in(LangDir() / (code + ".toml"), std::ios::binary);
    if (!in) {
        log::Warn("[ui] 언어 파일 없음: lang/{}.toml", code);
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    if (const std::string e = out.Load(ss.str()); !e.empty()) {
        log::Warn("[ui] 언어 파일 문법 오류 lang/{}.toml: {}", code, e);
        return false;
    }
    return true;
}
}  // namespace

std::string LoadLanguage(const std::string& language) {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    // 화면 언어는 Windows 표시 언어(UI 언어) 기준
    wchar_t uiLang[LOCALE_NAME_MAX_LENGTH] = {};
    ULONG count = 0, size = LOCALE_NAME_MAX_LENGTH;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, uiLang, &size) && uiLang[0]) wcscpy_s(locale, uiLang);
    g_lang = core::ResolveLanguage(language, ToUtf8(locale));
    LoadFile("en", g_english);
    if (g_lang == "en") {
        g_table = g_english;
    } else if (!LoadFile(g_lang, g_table)) {
        g_table = g_english;
    }
    g_table.SetFallback(&g_english);
    log::Info("[ui] language={} (setting={}) strings={}", g_lang, language, g_table.Size());
    return g_lang;
}

const std::string& CurrentLanguage() { return g_lang; }
const core::Strings& Table() { return g_table; }
const core::Strings& EnglishTable() { return g_english; }

}  // namespace dp::ui
