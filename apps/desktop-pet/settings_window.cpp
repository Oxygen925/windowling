#include "settings_window.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <format>
#include <vector>

#include "core/reasoning_effort.h"
#include "platform/windows/strings.h"
#include "ui/text.h"
#include "ui/ui_common.h"
#include "dp_version.h"

namespace dp {

namespace {

constexpr int kOff = ui::kLabelOffset;

// 고정 문구: 컨트롤 ID → 언어 키 (체크박스는 레이블 ID = 상자 + kLabelOffset)
struct Text {
    int id;
    const char* key;
};
constexpr Text kTexts[] = {
    {IDC_SET_LANGUAGE_L, "settings.language"},     {IDC_SET_THEME_L, "settings.theme"},
    {IDC_SET_SCALE_L, "settings.scale"},           {IDC_SET_FRAME_L, "settings.frame_rate"},
    {IDC_SET_PROXIMITY + kOff, "settings.proximity"}, {IDC_SET_STARTUP + kOff, "settings.startup"}, {IDC_SET_UPDATE + kOff, "settings.update_check"}, {IDC_SET_WIPE, "settings.wipe"}, {IDC_SET_WIPE_NOTE, "settings.wipe_note"},
    {IDC_SET_AI_ENABLED + kOff, "settings.ai_enabled"}, {IDC_SET_HOTKEY_L, "settings.hotkey"},
    {IDC_SET_HOTKEY_HINT, "settings.hotkey_hint"}, {IDC_SET_BUBBLE_L, "settings.bubble"},
    {IDC_SET_HISTORY_L, "settings.history"},       {IDC_SET_FIRST_L, "settings.first_token"},
    {IDC_SET_REQUEST_L, "settings.request_timeout"}, {IDC_SET_AI_NOTE, "settings.ai_note"},
    {IDC_SET_PET_CHAT + kOff, "settings.pet_chat"}, {IDC_SET_PET_CHAT_L, "settings.pet_chat_interval"},
    {IDC_SET_PET_CHAT_NOTE, "settings.pet_chat_note"},
    {IDC_SET_MODEL_L, "settings.model"}, {IDC_SET_EFFORT_L, "settings.effort"},
    {IDC_SET_PET_EFFORT_L, "settings.pet_chat_effort"}, {IDC_SET_MODEL_NOTE, "settings.model_note"},
    {IDC_SET_OBS_NOTE, "settings.observe_note"},   {IDC_SET_INTERVAL_L, "settings.interval"},
    {IDC_SET_INTERVAL_SEP, "settings.interval_sep"}, {IDC_SET_PER_HOUR_L, "settings.per_hour"},
    {IDC_SET_PER_DAY_L, "settings.per_day"},       {IDC_SET_AWAY_L, "settings.away"},
    {IDC_SET_SEND_APP + kOff, "settings.send_app"}, {IDC_SET_BEHAVIORS_L, "settings.behaviors"},
    {IDC_SET_BEH_SIT + kOff, "settings.beh_sit"},  {IDC_SET_BEH_LOOK + kOff, "settings.beh_look"},
    {IDC_SET_BEH_IDLE + kOff, "settings.beh_idle"}, {IDC_SET_BEH_WALK + kOff, "settings.beh_walk"},
    {IDC_SET_EXCLUDE_L, "settings.exclude"},       {IDC_SET_TTS_ENABLED + kOff, "settings.tts_enabled"},
    {IDC_SET_READ_REPLIES + kOff, "settings.read_replies"}, {IDC_SET_ENGINE_L, "settings.engine"},
    {IDC_SET_VOICE_L, "settings.voice"},           {IDC_SET_HTTP_URL_L, "settings.http_url"},
    {IDC_SET_HTTP_MODEL_L, "settings.http_model"}, {IDC_SET_HTTP_VOICE_L, "settings.http_voice"},
    {IDC_SET_CMD_PATH_L, "settings.command_path"}, {IDC_SET_CMD_ARGS_L, "settings.command_args"},
    {IDC_SET_RATE_L, "settings.rate"},             {IDC_SET_VOLUME_L, "settings.volume"},
    {IDC_SET_FULLSCREEN_L, "settings.fullscreen"}, {IDC_SET_SHOW_L, "settings.always_show"},
    {IDC_SET_HIDE_L, "settings.always_hide"},      {IDC_SET_APPLY, "settings.apply"},
    {IDCANCEL, "settings.close"},                  {IDC_SET_OPEN_FILE, "settings.open_file"},
};

// 설정 파일 범위 검사 경고("section.key …") → 그 값을 넣는 컨트롤
struct KeyControl {
    const char* key;
    int id;
};
constexpr KeyControl kKeyControls[] = {
    {"display.language", IDC_SET_LANGUAGE},          {"display.theme", IDC_SET_THEME},
    {"display.scale", IDC_SET_SCALE},                {"display.frame_rate", IDC_SET_FRAME},
    {"ai.hotkey", IDC_SET_HOTKEY},                   {"ai.history_turns", IDC_SET_HISTORY},
    {"ai.bubble_seconds", IDC_SET_BUBBLE},           {"ai.first_token_timeout_sec", IDC_SET_FIRST},
    {"ai.request_timeout_sec", IDC_SET_REQUEST},     {"ai.pet_chat_interval_min", IDC_SET_PET_CHAT_MIN},
    {"observe.interval_min", IDC_SET_INTERVAL_MIN},
    {"observe.interval_max", IDC_SET_INTERVAL_MAX},  {"observe.max_per_hour", IDC_SET_PER_HOUR},
    {"observe.max_per_day", IDC_SET_PER_DAY},        {"observe.require_activity_min", IDC_SET_AWAY},
    {"observe.exclude_exe", IDC_SET_EXCLUDE},        {"observe.allowed_behaviors", IDC_SET_BEH_SIT},
    {"tts.engine", IDC_SET_ENGINE},                  {"tts.voice", IDC_SET_VOICE},
    {"tts.rate", IDC_SET_RATE},                      {"tts.volume", IDC_SET_VOLUME},
    {"tts.http_url", IDC_SET_HTTP_URL},              {"tts.http_model", IDC_SET_HTTP_MODEL},
    {"tts.http_voice", IDC_SET_HTTP_VOICE},          {"tts.command_path", IDC_SET_CMD_PATH},
    {"tts.command_args", IDC_SET_CMD_ARGS},          {"fullscreen.always_show_exe", IDC_SET_SHOW},
    {"fullscreen.always_hide_exe", IDC_SET_HIDE},
};

constexpr const char* kLanguages[] = {"auto", "ko", "en"};
constexpr const char* kThemes[] = {"auto", "light", "dark"};
// effort 고르기 칸의 항목 순서 = core::kReasoningEfforts 순서
int EffortIndex(const std::string& e) {
    for (size_t i = 0; i < core::kReasoningEfforts.size(); ++i)
        if (core::kReasoningEfforts[i] == e) return static_cast<int>(i);
    return 0;
}
std::string EffortAt(int i) {
    return i >= 0 && i < static_cast<int>(core::kReasoningEfforts.size()) ? std::string(core::kReasoningEfforts[i]) : std::string();
}
constexpr const char* kEngines[] = {"winrt", "http", "command"};
constexpr const char* kBehaviors[] = {"sit", "look", "idle", "walk"};
constexpr int kBehaviorIds[] = {IDC_SET_BEH_SIT, IDC_SET_BEH_LOOK, IDC_SET_BEH_IDLE, IDC_SET_BEH_WALK};

int PageOf(int id) { return id / 100 - 6; }

// 값 칸의 제목 레이블 (대부분 바로 앞 ID)
int LabelOf(int id) {
    switch (id) {
        case IDC_SET_INTERVAL_MIN:
        case IDC_SET_INTERVAL_MAX: return IDC_SET_INTERVAL_L;
        case IDC_SET_BEH_SIT:
        case IDC_SET_BEH_LOOK:
        case IDC_SET_BEH_IDLE:
        case IDC_SET_BEH_WALK: return IDC_SET_BEHAVIORS_L;
        default: return id - 1;
    }
}

template <size_t N>
int IndexOf(const std::string& v, const char* const (&values)[N]) {
    for (size_t i = 0; i < N; ++i)
        if (v == values[i]) return static_cast<int>(i);
    return 0;
}

std::wstring Num(double v) { return std::format(L"{}", v); }

std::wstring Trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    const size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring JoinLines(const std::vector<std::string>& items) {
    std::wstring out;
    for (const auto& s : items) out += (out.empty() ? L"" : L"\r\n") + ToWide(s);
    return out;
}

// 한 줄에 하나, 앞뒤 공백 제거, 소문자 (설정 파일 읽기와 같은 규칙)
std::vector<std::string> SplitLines(const std::wstring& text) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find(L'\n', at);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = Trim(text.substr(at, end - at));
        for (auto& c : line) c = static_cast<wchar_t>(std::towlower(c));
        if (!line.empty()) out.push_back(ToUtf8(line));
        at = end + 1;
    }
    return out;
}

}  // namespace

void SettingsWindow::Show(const core::Config& current, Page page, SettingsCallbacks callbacks) {
    if (IsOpen()) {
        SelectPage(page);
        BringToFront();
        return;
    }
    base_ = current;
    cb_ = std::move(callbacks);
    page_ = page;
    Open({DP_CLASS_PREFIX L"Settings", ui::T("settings.title"), 740, 720, 660, 640});
}

bool SettingsWindow::IsBoldControl(int id) const { return id == IDC_SET_FULLSCREEN_L; }

void SettingsWindow::OnCreate() {
    auto label = [&](int id) { Add(L"Static", L"", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, id); };
    auto note = [&](int id) { SetWindowLongPtrW(Add(L"Static", L"", SS_LEFT, id), GWLP_USERDATA, 1); };
    auto edit = [&](int id, DWORD extra = 0) { Add(L"Edit", L"", ES_AUTOHSCROLL | WS_TABSTOP | extra, id, WS_EX_CLIENTEDGE); };
    auto number = [&](int id) { edit(id, ES_NUMBER); };
    auto multi = [&](int id) {
        // ES_WANTRETURN: Enter 가 "적용" 이 아니라 줄바꿈
        Add(L"Edit", L"", ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, id, WS_EX_CLIENTEDGE);
    };
    auto combo = [&](int id) { Add(L"ComboBox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, id); };
    auto check = [&](int id) { ui::MakeCheck(hwnd_, L"", id); };

    // 왼쪽 목록: 항목 높이·선택 색을 직접 그린다 (기본 목록은 줄 간격이 좁다)
    Add(L"ListBox", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP, IDC_SET_NAV,
        WS_EX_CLIENTEDGE);
    // 일반·표시
    label(IDC_SET_LANGUAGE_L), combo(IDC_SET_LANGUAGE);
    label(IDC_SET_THEME_L), combo(IDC_SET_THEME);
    label(IDC_SET_SCALE_L), combo(IDC_SET_SCALE);
    label(IDC_SET_FRAME_L), combo(IDC_SET_FRAME);
    check(IDC_SET_PROXIMITY), check(IDC_SET_STARTUP), check(IDC_SET_UPDATE);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_SET_WIPE);
    note(IDC_SET_WIPE_NOTE);
    // AI 대화
    check(IDC_SET_AI_ENABLED);
    label(IDC_SET_MODEL_L), combo(IDC_SET_MODEL);
    label(IDC_SET_EFFORT_L), combo(IDC_SET_EFFORT);
    note(IDC_SET_MODEL_NOTE);
    label(IDC_SET_HOTKEY_L), edit(IDC_SET_HOTKEY), note(IDC_SET_HOTKEY_HINT);
    label(IDC_SET_BUBBLE_L), number(IDC_SET_BUBBLE);
    label(IDC_SET_HISTORY_L), number(IDC_SET_HISTORY);
    label(IDC_SET_FIRST_L), number(IDC_SET_FIRST);
    label(IDC_SET_REQUEST_L), number(IDC_SET_REQUEST);
    note(IDC_SET_AI_NOTE);
    check(IDC_SET_PET_CHAT);
    label(IDC_SET_PET_CHAT_L), number(IDC_SET_PET_CHAT_MIN);
    label(IDC_SET_PET_EFFORT_L), combo(IDC_SET_PET_EFFORT);
    note(IDC_SET_PET_CHAT_NOTE);
    // 화면 관찰
    note(IDC_SET_OBS_NOTE);
    label(IDC_SET_INTERVAL_L), number(IDC_SET_INTERVAL_MIN), label(IDC_SET_INTERVAL_SEP), number(IDC_SET_INTERVAL_MAX);
    label(IDC_SET_PER_HOUR_L), number(IDC_SET_PER_HOUR);
    label(IDC_SET_PER_DAY_L), number(IDC_SET_PER_DAY);
    label(IDC_SET_AWAY_L), number(IDC_SET_AWAY);
    check(IDC_SET_SEND_APP);
    label(IDC_SET_BEHAVIORS_L);
    for (int id : kBehaviorIds) check(id);
    label(IDC_SET_EXCLUDE_L), multi(IDC_SET_EXCLUDE);
    // 음성·전체화면 예외
    check(IDC_SET_TTS_ENABLED), check(IDC_SET_READ_REPLIES);
    label(IDC_SET_ENGINE_L), combo(IDC_SET_ENGINE);
    label(IDC_SET_VOICE_L), edit(IDC_SET_VOICE);
    label(IDC_SET_HTTP_URL_L), edit(IDC_SET_HTTP_URL);
    label(IDC_SET_HTTP_MODEL_L), edit(IDC_SET_HTTP_MODEL);
    label(IDC_SET_HTTP_VOICE_L), edit(IDC_SET_HTTP_VOICE);
    label(IDC_SET_CMD_PATH_L), edit(IDC_SET_CMD_PATH);
    label(IDC_SET_CMD_ARGS_L), edit(IDC_SET_CMD_ARGS);
    label(IDC_SET_RATE_L), edit(IDC_SET_RATE);
    label(IDC_SET_VOLUME_L), number(IDC_SET_VOLUME);
    label(IDC_SET_FULLSCREEN_L);
    label(IDC_SET_SHOW_L), multi(IDC_SET_SHOW);
    label(IDC_SET_HIDE_L), multi(IDC_SET_HIDE);
    // 아래
    note(IDC_SET_STATUS);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_SET_OPEN_FILE);
    Add(L"Button", L"", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_SET_APPLY);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);

    OnReloadTexts();
    Fill(base_);
    SetStatus(ui::T("settings.note_rewrite"));
    SelectPage(page_);
}

void SettingsWindow::OnReloadTexts() {
    SetWindowTextW(hwnd_, ui::T("settings.title").c_str());
    for (const auto& t : kTexts) SetWindowTextW(Item(t.id), ui::T(t.key).c_str());
    FillCombos();
}

void SettingsWindow::FillCombos() {
    auto fill = [&](int id, std::initializer_list<const char*> keys) {
        HWND c = Item(id);
        const LRESULT sel = SendMessageW(c, CB_GETCURSEL, 0, 0);
        SendMessageW(c, CB_RESETCONTENT, 0, 0);
        for (const char* k : keys) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ui::T(k).c_str()));
        SendMessageW(c, CB_SETCURSEL, sel < 0 ? 0 : sel, 0);
    };
    fill(IDC_SET_LANGUAGE, {"settings.lang_auto", "settings.lang_ko", "settings.lang_en"});
    fill(IDC_SET_THEME, {"settings.theme_auto", "settings.theme_light", "settings.theme_dark"});
    fill(IDC_SET_ENGINE, {"settings.engine_winrt", "settings.engine_http", "settings.engine_command"});
    for (int id : {IDC_SET_EFFORT, IDC_SET_PET_EFFORT})
        fill(id, {"settings.effort_default", "settings.effort_none", "settings.effort_minimal", "settings.effort_low",
                  "settings.effort_medium", "settings.effort_high", "settings.effort_xhigh"});
    HWND nav = Item(IDC_SET_NAV);
    SendMessageW(nav, LB_RESETCONTENT, 0, 0);
    for (const char* k : {"settings.page_general", "settings.page_ai", "settings.page_observe", "settings.page_tts"})
        SendMessageW(nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ui::T(k).c_str()));
    SendMessageW(nav, LB_SETCURSEL, page_, 0);
}

// 모델: "목록의 첫 모델" + 계정의 목록 (+ 목록에 없는 지금 값)
void SettingsWindow::FillModels(const std::string& current) {
    HWND cb = Item(IDC_SET_MODEL);
    if (!cb) return;
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    modelSlugs_ = {""};
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ui::T("settings.model_first").c_str()));
    int sel = 0;
    if (cb_.models)
        for (const auto& [slug, name] : cb_.models()) {
            modelSlugs_.push_back(slug);
            SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            if (slug == current) sel = static_cast<int>(modelSlugs_.size()) - 1;
        }
    if (!current.empty() && sel == 0) {
        modelSlugs_.push_back(current);
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ToWide(current).c_str()));
        sel = static_cast<int>(modelSlugs_.size()) - 1;
    }
    SendMessageW(cb, CB_SETCURSEL, sel, 0);
}

void SettingsWindow::ModelsChanged() {
    if (!IsOpen()) return;
    const LRESULT i = SendMessageW(Item(IDC_SET_MODEL), CB_GETCURSEL, 0, 0);
    FillModels(i >= 0 && i < static_cast<LRESULT>(modelSlugs_.size()) ? modelSlugs_[static_cast<size_t>(i)] : base_.ai.model);
}

void SettingsWindow::Fill(const core::Config& c) {
    auto text = [&](int id, const std::wstring& s) { SetWindowTextW(Item(id), s.c_str()); };
    auto num = [&](int id, int v) { text(id, std::to_wstring(v)); };
    auto checked = [&](int id, bool on) { SendMessageW(Item(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); };
    auto select = [&](int id, int index) { SendMessageW(Item(id), CB_SETCURSEL, index, 0); };
    // 숫자 고르기: 미리 정한 값 + 지금 값 (설정 파일에서 다른 값을 썼을 때)
    auto presets = [&](int id, std::vector<double> values, double current) {
        if (std::none_of(values.begin(), values.end(), [&](double v) { return std::abs(v - current) < 1e-9; }))
            values.push_back(current);
        std::sort(values.begin(), values.end());
        HWND cb = Item(id);
        SendMessageW(cb, CB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < values.size(); ++i) {
            SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Num(values[i]).c_str()));
            if (std::abs(values[i] - current) < 1e-9) SendMessageW(cb, CB_SETCURSEL, i, 0);
        }
    };

    select(IDC_SET_LANGUAGE, IndexOf(c.language, kLanguages));
    select(IDC_SET_THEME, IndexOf(c.theme, kThemes));
    presets(IDC_SET_SCALE, {0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0}, c.scale);
    presets(IDC_SET_FRAME, {15, 20, 30, 60}, c.frameRate);
    checked(IDC_SET_PROXIMITY, c.proximityReaction);
    checked(IDC_SET_STARTUP, cb_.startupEnabled && cb_.startupEnabled());
    checked(IDC_SET_UPDATE, c.updateCheck);

    checked(IDC_SET_AI_ENABLED, c.ai.enabled);
    text(IDC_SET_HOTKEY, ToWide(c.ai.hotkey));
    num(IDC_SET_BUBBLE, c.ai.bubbleSeconds);
    num(IDC_SET_HISTORY, c.ai.historyTurns);
    num(IDC_SET_FIRST, c.ai.firstTokenTimeoutSec);
    num(IDC_SET_REQUEST, c.ai.requestTimeoutSec);
    checked(IDC_SET_PET_CHAT, c.ai.petChat);
    select(IDC_SET_EFFORT, EffortIndex(c.ai.reasoningEffort));
    select(IDC_SET_PET_EFFORT, EffortIndex(c.ai.petChatEffort));
    FillModels(c.ai.model);
    num(IDC_SET_PET_CHAT_MIN, c.ai.petChatIntervalMin);

    const auto& o = c.observe;
    num(IDC_SET_INTERVAL_MIN, o.intervalMinMin);
    num(IDC_SET_INTERVAL_MAX, o.intervalMaxMin);
    num(IDC_SET_PER_HOUR, o.maxPerHour);
    num(IDC_SET_PER_DAY, o.maxPerDay);
    num(IDC_SET_AWAY, o.requireActivityMin);
    checked(IDC_SET_SEND_APP, o.sendAppName);
    for (size_t i = 0; i < std::size(kBehaviors); ++i)
        checked(kBehaviorIds[i], std::find(o.allowedBehaviors.begin(), o.allowedBehaviors.end(), kBehaviors[i]) !=
                                     o.allowedBehaviors.end());
    text(IDC_SET_EXCLUDE, JoinLines(o.excludeExe));

    const auto& t = c.tts;
    checked(IDC_SET_TTS_ENABLED, t.enabled);
    checked(IDC_SET_READ_REPLIES, t.readChatReplies);
    select(IDC_SET_ENGINE, IndexOf(t.engine, kEngines));
    text(IDC_SET_VOICE, ToWide(t.voice));
    text(IDC_SET_HTTP_URL, ToWide(t.httpUrl));
    text(IDC_SET_HTTP_MODEL, ToWide(t.httpModel));
    text(IDC_SET_HTTP_VOICE, ToWide(t.httpVoice));
    text(IDC_SET_CMD_PATH, ToWide(t.commandPath));
    text(IDC_SET_CMD_ARGS, ToWide(t.commandArgs));
    text(IDC_SET_RATE, Num(t.rate));
    num(IDC_SET_VOLUME, static_cast<int>(std::lround(t.volume * 100)));
    text(IDC_SET_SHOW, JoinLines(c.alwaysShowExe));
    text(IDC_SET_HIDE, JoinLines(c.alwaysHideExe));
}

bool SettingsWindow::Read(core::Config& out, int& badId) const {
    auto text = [&](int id) {
        HWND h = Item(id);
        std::wstring s(static_cast<size_t>(GetWindowTextLengthW(h)), L'\0');
        GetWindowTextW(h, s.data(), static_cast<int>(s.size()) + 1);
        return s;
    };
    auto comboText = [&](int id) {
        HWND h = Item(id);
        const LRESULT sel = SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (sel < 0) return std::wstring();
        std::wstring s(static_cast<size_t>(SendMessageW(h, CB_GETLBTEXTLEN, sel, 0)), L'\0');
        SendMessageW(h, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(s.data()));
        return s;
    };
    auto toInt = [&](int id, const std::wstring& raw, int& v) {
        const std::wstring s = Trim(raw);
        wchar_t* end = nullptr;
        const long x = s.empty() ? 0 : std::wcstol(s.c_str(), &end, 10);
        if (s.empty() || *end) return badId = id, false;
        v = static_cast<int>(x);
        return true;
    };
    auto toDouble = [&](int id, const std::wstring& raw, double& v) {
        std::wstring s = Trim(raw);
        std::replace(s.begin(), s.end(), L',', L'.');  // 소수점 쉼표도 받는다
        wchar_t* end = nullptr;
        const double x = s.empty() ? 0 : std::wcstod(s.c_str(), &end);
        if (s.empty() || *end || !std::isfinite(x)) return badId = id, false;
        v = x;
        return true;
    };
    auto isChecked = [&](int id) { return SendMessageW(Item(id), BM_GETCHECK, 0, 0) == BST_CHECKED; };
    auto index = [&](int id) { return std::max<int>(0, static_cast<int>(SendMessageW(Item(id), CB_GETCURSEL, 0, 0))); };

    out = base_;
    out.language = kLanguages[index(IDC_SET_LANGUAGE)];
    out.theme = kThemes[index(IDC_SET_THEME)];
    if (!toDouble(IDC_SET_SCALE, comboText(IDC_SET_SCALE), out.scale)) return false;
    if (!toInt(IDC_SET_FRAME, comboText(IDC_SET_FRAME), out.frameRate)) return false;
    out.proximityReaction = isChecked(IDC_SET_PROXIMITY);
    out.updateCheck = isChecked(IDC_SET_UPDATE);

    out.ai.enabled = isChecked(IDC_SET_AI_ENABLED);
    out.ai.hotkey = ToUtf8(Trim(text(IDC_SET_HOTKEY)));
    if (!toInt(IDC_SET_BUBBLE, text(IDC_SET_BUBBLE), out.ai.bubbleSeconds) ||
        !toInt(IDC_SET_HISTORY, text(IDC_SET_HISTORY), out.ai.historyTurns) ||
        !toInt(IDC_SET_FIRST, text(IDC_SET_FIRST), out.ai.firstTokenTimeoutSec) ||
        !toInt(IDC_SET_REQUEST, text(IDC_SET_REQUEST), out.ai.requestTimeoutSec) ||
        !toInt(IDC_SET_PET_CHAT_MIN, text(IDC_SET_PET_CHAT_MIN), out.ai.petChatIntervalMin))
        return false;
    out.ai.petChat = isChecked(IDC_SET_PET_CHAT);
    out.ai.reasoningEffort = EffortAt(index(IDC_SET_EFFORT));
    out.ai.petChatEffort = EffortAt(index(IDC_SET_PET_EFFORT));
    {
        const int m = index(IDC_SET_MODEL);
        out.ai.model = m >= 0 && m < static_cast<int>(modelSlugs_.size()) ? modelSlugs_[m] : base_.ai.model;
    }

    auto& o = out.observe;
    if (!toInt(IDC_SET_INTERVAL_MIN, text(IDC_SET_INTERVAL_MIN), o.intervalMinMin) ||
        !toInt(IDC_SET_INTERVAL_MAX, text(IDC_SET_INTERVAL_MAX), o.intervalMaxMin) ||
        !toInt(IDC_SET_PER_HOUR, text(IDC_SET_PER_HOUR), o.maxPerHour) ||
        !toInt(IDC_SET_PER_DAY, text(IDC_SET_PER_DAY), o.maxPerDay) ||
        !toInt(IDC_SET_AWAY, text(IDC_SET_AWAY), o.requireActivityMin))
        return false;
    o.sendAppName = isChecked(IDC_SET_SEND_APP);
    o.allowedBehaviors.clear();
    for (size_t i = 0; i < std::size(kBehaviors); ++i)
        if (isChecked(kBehaviorIds[i])) o.allowedBehaviors.emplace_back(kBehaviors[i]);
    o.excludeExe = SplitLines(text(IDC_SET_EXCLUDE));

    auto& t = out.tts;
    t.enabled = isChecked(IDC_SET_TTS_ENABLED);
    t.readChatReplies = isChecked(IDC_SET_READ_REPLIES);
    t.engine = kEngines[index(IDC_SET_ENGINE)];
    t.voice = ToUtf8(Trim(text(IDC_SET_VOICE)));
    t.httpUrl = ToUtf8(Trim(text(IDC_SET_HTTP_URL)));
    t.httpModel = ToUtf8(Trim(text(IDC_SET_HTTP_MODEL)));
    t.httpVoice = ToUtf8(Trim(text(IDC_SET_HTTP_VOICE)));
    t.commandPath = ToUtf8(Trim(text(IDC_SET_CMD_PATH)));
    t.commandArgs = ToUtf8(Trim(text(IDC_SET_CMD_ARGS)));
    if (!toDouble(IDC_SET_RATE, text(IDC_SET_RATE), t.rate)) return false;
    int volume = 0;
    if (!toInt(IDC_SET_VOLUME, text(IDC_SET_VOLUME), volume)) return false;
    t.volume = volume / 100.0;
    out.alwaysShowExe = SplitLines(text(IDC_SET_SHOW));
    out.alwaysHideExe = SplitLines(text(IDC_SET_HIDE));
    return true;
}

void SettingsWindow::Apply() {
    auto invalid = [&](int id, const std::wstring& fallback) {
        std::wstring name = fallback;
        if (id) {
            SelectPage(PageOf(id));
            SetFocus(Item(id));
            HWND l = Item(LabelOf(id));
            std::wstring s(static_cast<size_t>(GetWindowTextLengthW(l)), L'\0');
            GetWindowTextW(l, s.data(), static_cast<int>(s.size()) + 1);
            name = s;
        }
        SetStatus(ui::TF("settings.invalid", name));
    };
    core::Config draft;
    int bad = 0;
    if (!Read(draft, bad)) return invalid(bad, L"");
    // 설정 파일과 같은 범위 검사: 저장했다가 다시 읽었을 때 경고가 없어야 한다
    const auto check = core::ParseConfig(core::SerializeConfig(draft));
    if (!check.warnings.empty()) {
        const std::string& w = check.warnings.front();
        for (const auto& kc : kKeyControls) {
            const size_t n = std::strlen(kc.key);
            if (w.compare(0, n, kc.key) == 0 && (w.size() == n || !(std::islower(static_cast<unsigned char>(w[n])) || w[n] == '_')))
                return invalid(kc.id, L"");
        }
        return invalid(0, ToWide(w.substr(0, w.find(' '))));
    }
    if (!cb_.apply) return;
    const SettingsResult r = cb_.apply(draft, SendMessageW(Item(IDC_SET_STARTUP), BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (r.ok) base_ = draft;
    if (IsOpen()) SetStatus(r.message);
}

LRESULT SettingsWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
    if (msg == WM_MEASUREITEM && wp == IDC_SET_NAV) {
        reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight = S(34);
        handled = true;
        return TRUE;
    }
    if (msg == WM_DRAWITEM && wp == IDC_SET_NAV) {
        const auto* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        handled = true;
        if (d->itemID == static_cast<UINT>(-1)) return TRUE;
        const ui::Palette& p = ui::Colors();
        const bool selected = (d->itemState & ODS_SELECTED) != 0;
        // 선택: 강조색 바탕 + 창 바탕색 글자 (밝게·어둡게 모두 대비가 난다)
        HBRUSH bg = selected ? CreateSolidBrush(p.accent) : nullptr;
        FillRect(d->hDC, &d->rcItem, selected ? bg : ui::ControlBrush());
        if (bg) DeleteObject(bg);
        std::wstring s(static_cast<size_t>(SendMessageW(d->hwndItem, LB_GETTEXTLEN, d->itemID, 0)), L'\0');
        SendMessageW(d->hwndItem, LB_GETTEXT, d->itemID, reinterpret_cast<LPARAM>(s.data()));
        RECT r = d->rcItem;
        r.left += S(10);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, selected ? p.window : p.controlText);
        HGDIOBJ old = SelectObject(d->hDC, ui::UiFont(GetDpiForWindow(hwnd_), 0, selected));
        DrawTextW(d->hDC, s.c_str(), static_cast<int>(s.size()), &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(d->hDC, old);
        if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) DrawFocusRect(d->hDC, &d->rcItem);
        return TRUE;
    }
    handled = false;
    return 0;
}

void SettingsWindow::SetStatus(const std::wstring& text) { SetWindowTextW(Item(IDC_SET_STATUS), text.c_str()); }

void SettingsWindow::SelectPage(int page) {
    page_ = std::clamp(page, 0, kPageCount - 1);
    SendMessageW(Item(IDC_SET_NAV), LB_SETCURSEL, page_, 0);
    UpdateVisibility();
    Relayout();
}

void SettingsWindow::UpdateVisibility() {
    const int engine = static_cast<int>(SendMessageW(Item(IDC_SET_ENGINE), CB_GETCURSEL, 0, 0));
    for (HWND c = GetWindow(hwnd_, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        int id = GetDlgCtrlID(c);
        if (id >= kOff) id -= kOff;
        if (id < 600 || id >= 1000) continue;
        bool show = PageOf(id) == page_;
        if (show && page_ == kTts) {  // 엔진별 칸
            if (id == IDC_SET_VOICE_L || id == IDC_SET_VOICE) show = engine == 0;
            else if (id >= IDC_SET_HTTP_URL_L && id <= IDC_SET_HTTP_VOICE) show = engine == 1;
            else if (id >= IDC_SET_CMD_PATH_L && id <= IDC_SET_CMD_ARGS) show = engine == 2;
        }
        ShowWindow(c, show ? SW_SHOW : SW_HIDE);
    }
}

void SettingsWindow::OnLayout(int width, int height) {
    const int m = S(14), gap = S(8), row = S(28), btnH = S(30), btnW = S(100), navW = S(170);
    auto mv = [&](int id, int x, int y, int w, int h) { MoveWindow(Item(id), x, y, std::max(0, w), std::max(0, h), TRUE); };

    const int bottomY = height - m - btnH;
    const int statusH = S(38), statusY = bottomY - gap - statusH;
    mv(IDC_SET_OPEN_FILE, m, bottomY, S(230), btnH);
    mv(IDCANCEL, width - m - btnW, bottomY, btnW, btnH);
    mv(IDC_SET_APPLY, width - m - 2 * btnW - gap, bottomY, btnW, btnH);
    mv(IDC_SET_STATUS, m, statusY, width - 2 * m, statusH);
    mv(IDC_SET_NAV, m, m, navW, statusY - gap - m);
    SendMessageW(Item(IDC_SET_NAV), LB_SETITEMHEIGHT, 0, S(34));  // DPI 가 바뀌어도 맞게

    const int x0 = m + navW + S(18), cw = width - x0 - m;
    const int labelW = std::min(S(230), cw * 45 / 100);
    const int cx = x0 + labelW + gap, avail = width - m - cx;
    const int box = S(18);
    int y = m;
    auto field = [&](int label, int control, int w96) {
        mv(label, x0, y, labelW, row);
        wchar_t cls[16] = {};
        GetClassNameW(Item(control), cls, 16);
        const bool combo = std::wcscmp(cls, L"ComboBox") == 0;
        mv(control, cx, y + S(1), std::min(S(w96), avail), combo ? S(320) : row - S(2));
        y += row + gap;
    };
    auto checkAt = [&](int id, int x, int w) {
        mv(id, x, y + (row - box) / 2, box, box);
        mv(id + kOff, x + box + S(6), y, w - box - S(6), row);
    };
    auto check = [&](int id) {
        checkAt(id, x0, cw);
        y += row + gap;
    };
    auto note = [&](int id, int lines) {
        mv(id, x0, y, cw, S(20) * lines);
        y += S(20) * lines + gap;
    };
    const int contentBottom = statusY - gap;

    switch (page_) {
        case kGeneral:
            field(IDC_SET_LANGUAGE_L, IDC_SET_LANGUAGE, 280);
            field(IDC_SET_THEME_L, IDC_SET_THEME, 280);
            field(IDC_SET_SCALE_L, IDC_SET_SCALE, 100);
            field(IDC_SET_FRAME_L, IDC_SET_FRAME, 100);
            check(IDC_SET_PROXIMITY);
            check(IDC_SET_STARTUP);
            check(IDC_SET_UPDATE);
            y += gap;
            mv(IDC_SET_WIPE, x0, y, S(230), row + S(2));
            y += row + S(2) + S(4);
            note(IDC_SET_WIPE_NOTE, 2);
            break;
        case kAi:
            check(IDC_SET_AI_ENABLED);
            field(IDC_SET_MODEL_L, IDC_SET_MODEL, 260);
            field(IDC_SET_EFFORT_L, IDC_SET_EFFORT, 200);
            note(IDC_SET_MODEL_NOTE, 3);
            field(IDC_SET_HOTKEY_L, IDC_SET_HOTKEY, 180);
            mv(IDC_SET_HOTKEY_HINT, cx, y - gap + S(2), avail, S(20));
            y += S(20) + gap;
            field(IDC_SET_BUBBLE_L, IDC_SET_BUBBLE, 80);
            field(IDC_SET_HISTORY_L, IDC_SET_HISTORY, 80);
            field(IDC_SET_FIRST_L, IDC_SET_FIRST, 80);
            field(IDC_SET_REQUEST_L, IDC_SET_REQUEST, 80);
            note(IDC_SET_AI_NOTE, 1);
            check(IDC_SET_PET_CHAT);
            field(IDC_SET_PET_CHAT_L, IDC_SET_PET_CHAT_MIN, 80);
            field(IDC_SET_PET_EFFORT_L, IDC_SET_PET_EFFORT, 200);
            note(IDC_SET_PET_CHAT_NOTE, 2);
            break;
        case kObserve: {
            note(IDC_SET_OBS_NOTE, 2);
            const int nw = S(64);
            mv(IDC_SET_INTERVAL_L, x0, y, labelW, row);
            mv(IDC_SET_INTERVAL_MIN, cx, y + S(1), nw, row - S(2));
            mv(IDC_SET_INTERVAL_SEP, cx + nw + S(8), y, S(16), row);
            mv(IDC_SET_INTERVAL_MAX, cx + nw + S(28), y + S(1), nw, row - S(2));
            y += row + gap;
            field(IDC_SET_PER_HOUR_L, IDC_SET_PER_HOUR, 80);
            field(IDC_SET_PER_DAY_L, IDC_SET_PER_DAY, 80);
            field(IDC_SET_AWAY_L, IDC_SET_AWAY, 80);
            check(IDC_SET_SEND_APP);
            mv(IDC_SET_BEHAVIORS_L, x0, y, labelW, row);
            {
                const int each = avail / 2;
                for (int i = 0; i < 4; ++i) {
                    if (i == 2) y += row;
                    checkAt(kBehaviorIds[i], cx + (i % 2) * each, each);
                }
            }
            y += row + gap;
            mv(IDC_SET_EXCLUDE_L, x0, y, cw, row);
            y += row;
            mv(IDC_SET_EXCLUDE, x0, y, cw, std::max(S(60), contentBottom - y));
            break;
        }
        case kTts: {
            check(IDC_SET_TTS_ENABLED);
            check(IDC_SET_READ_REPLIES);
            field(IDC_SET_ENGINE_L, IDC_SET_ENGINE, 320);
            const int engine = static_cast<int>(SendMessageW(Item(IDC_SET_ENGINE), CB_GETCURSEL, 0, 0));
            if (engine == 1) {
                field(IDC_SET_HTTP_URL_L, IDC_SET_HTTP_URL, 400);
                field(IDC_SET_HTTP_MODEL_L, IDC_SET_HTTP_MODEL, 220);
                field(IDC_SET_HTTP_VOICE_L, IDC_SET_HTTP_VOICE, 220);
            } else if (engine == 2) {
                field(IDC_SET_CMD_PATH_L, IDC_SET_CMD_PATH, 400);
                field(IDC_SET_CMD_ARGS_L, IDC_SET_CMD_ARGS, 400);
            } else {
                field(IDC_SET_VOICE_L, IDC_SET_VOICE, 260);
            }
            field(IDC_SET_RATE_L, IDC_SET_RATE, 80);
            field(IDC_SET_VOLUME_L, IDC_SET_VOLUME, 80);
            mv(IDC_SET_FULLSCREEN_L, x0, y, cw, row);
            y += row;
            const int half = (cw - gap) / 2;
            mv(IDC_SET_SHOW_L, x0, y, half, S(22));
            mv(IDC_SET_HIDE_L, x0 + half + gap, y, half, S(22));
            y += S(24);
            const int eh = std::max(S(50), contentBottom - y);
            mv(IDC_SET_SHOW, x0, y, half, eh);
            mv(IDC_SET_HIDE, x0 + half + gap, y, half, eh);
            break;
        }
    }
}

void SettingsWindow::OnCommand(int id, int code, HWND) {
    switch (id) {
        case IDC_SET_NAV:
            if (code == LBN_SELCHANGE) SelectPage(static_cast<int>(SendMessageW(Item(IDC_SET_NAV), LB_GETCURSEL, 0, 0)));
            break;
        case IDC_SET_ENGINE:
            if (code == CBN_SELCHANGE) {
                UpdateVisibility();
                Relayout();
            }
            break;
        case IDOK:  // 한 줄 입력칸에서 Enter
        case IDC_SET_APPLY:
            if (code == BN_CLICKED) Apply();
            break;
        case IDCANCEL:
            if (code == BN_CLICKED) Close();
            break;
        case IDC_SET_WIPE:
            if (code == BN_CLICKED && cb_.wipe) cb_.wipe();
            break;
        case IDC_SET_OPEN_FILE:
            if (code == BN_CLICKED && cb_.openFile) cb_.openFile();
            break;
    }
}

}  // namespace dp
