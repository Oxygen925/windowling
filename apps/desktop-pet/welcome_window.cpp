#include "welcome_window.h"

#include <algorithm>

#include "ui/text.h"
#include "ui/ui_common.h"
#include "dp_version.h"

namespace dp {

void WelcomeWindow::Show(WelcomeCallbacks callbacks, bool activate, bool noCharacters) {
    cb_ = std::move(callbacks);
    noCharacters_ = noCharacters;
    if (IsOpen()) {
        ShowWindow(Item(IDC_WEL_NOCHAR), noCharacters_ ? SW_SHOW : SW_HIDE);
        Relayout();
        if (activate) BringToFront();
        return;
    }
    Open({DP_CLASS_PREFIX L"Welcome", ui::T("welcome.title"), 560, noCharacters ? 450 : 400, 0, 0, false, activate});
}

void WelcomeWindow::OnCreate() {
    Add(L"Static", L"", SS_LEFT, IDC_WEL_HEADING);
    SendMessageW(Item(IDC_WEL_HEADING), WM_SETFONT, reinterpret_cast<WPARAM>(ui::UiFont(GetDpiForWindow(hwnd_), 4, true)), TRUE);
    Add(L"Static", L"", SS_LEFT, IDC_WEL_INTRO);
    Add(L"Static", L"", SS_LEFT, IDC_WEL_CONTROLS_L);
    Add(L"Static", L"", SS_LEFT, IDC_WEL_CONTROLS);
    SetWindowLongPtrW(Add(L"Static", L"", SS_LEFT, IDC_WEL_MORE), GWLP_USERDATA, 1);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_WEL_CHARACTERS);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_WEL_SETTINGS);
    Add(L"Button", L"", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_WEL_START);
    Add(L"Static", L"", SS_LEFT, IDC_WEL_NOCHAR);
    ShowWindow(Item(IDC_WEL_NOCHAR), noCharacters_ ? SW_SHOW : SW_HIDE);
    OnReloadTexts();
}

void WelcomeWindow::OnReloadTexts() {
    SetWindowTextW(hwnd_, ui::T("welcome.title").c_str());
    const std::pair<int, const char*> texts[] = {
        {IDC_WEL_HEADING, "welcome.heading"},   {IDC_WEL_INTRO, "welcome.intro"},
        {IDC_WEL_CONTROLS_L, "welcome.controls_heading"}, {IDC_WEL_CONTROLS, "welcome.controls"},
        {IDC_WEL_MORE, "welcome.more"},         {IDC_WEL_CHARACTERS, "welcome.characters"},
        {IDC_WEL_SETTINGS, "welcome.settings"}, {IDC_WEL_START, "welcome.start"},
        {IDC_WEL_NOCHAR, "welcome.no_characters"},
    };
    for (const auto& [id, key] : texts) SetWindowTextW(Item(id), ui::T(key).c_str());
}

void WelcomeWindow::OnLayout(int width, int height) {
    const int m = S(20), gap = S(10), btnH = S(32), W = width - 2 * m;
    // 제목은 DPI 가 바뀌어도 크게 (AppWindow 는 굵게만 다시 맞춘다)
    SendMessageW(Item(IDC_WEL_HEADING), WM_SETFONT, reinterpret_cast<WPARAM>(ui::UiFont(GetDpiForWindow(hwnd_), 4, true)), FALSE);
    int y = m;
    MoveWindow(Item(IDC_WEL_HEADING), m, y, W, S(34), TRUE);
    y += S(34) + gap;
    MoveWindow(Item(IDC_WEL_INTRO), m, y, W, S(44), TRUE);
    y += S(44) + S(14);
    MoveWindow(Item(IDC_WEL_CONTROLS_L), m, y, W, S(22), TRUE);
    y += S(24);
    MoveWindow(Item(IDC_WEL_CONTROLS), m, y, W, S(84), TRUE);
    y += S(84) + S(14);
    const int bottomY = height - m - btnH;
    if (noCharacters_) {  // 캐릭터 관리 버튼 바로 위
        MoveWindow(Item(IDC_WEL_NOCHAR), m, bottomY - gap - S(40), W, S(40), TRUE);
        MoveWindow(Item(IDC_WEL_MORE), m, y, W, std::max(S(40), bottomY - 2 * gap - S(40) - y), TRUE);
    } else {
        MoveWindow(Item(IDC_WEL_MORE), m, y, W, std::max(S(40), bottomY - gap - y), TRUE);
    }
    const int bw = S(140), sw = S(120);
    MoveWindow(Item(IDC_WEL_CHARACTERS), m, bottomY, bw, btnH, TRUE);
    MoveWindow(Item(IDC_WEL_SETTINGS), m + bw + gap, bottomY, bw, btnH, TRUE);
    MoveWindow(Item(IDC_WEL_START), width - m - sw, bottomY, sw, btnH, TRUE);
}

void WelcomeWindow::OnCommand(int id, int code, HWND) {
    if (code != BN_CLICKED) return;
    switch (id) {
        case IDC_WEL_CHARACTERS:
            if (cb_.openCharacters) cb_.openCharacters();
            break;
        case IDC_WEL_SETTINGS:
            if (cb_.openSettings) cb_.openSettings();
            break;
        case IDOK:
        case IDC_WEL_START:
        case IDCANCEL: Close(); break;
    }
}

}  // namespace dp
