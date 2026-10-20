#include "chat_window.h"

#include <commctrl.h>

#include <algorithm>

#include "platform/windows/strings.h"
#include "ui/text.h"
#include "ui/ui_common.h"

namespace dp {

namespace {
constexpr wchar_t kChatClass[] = L"DesktopPet.Chat";
}

bool ChatWindow::Create(HINSTANCE instance, ChatCallbacks callbacks) {
    cb_ = std::move(callbacks);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &ChatWindow::StaticProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = ui::AppIcon(false);
    wc.hIconSm = ui::AppIcon(true);
    wc.lpszClassName = kChatClass;
    RegisterClassExW(&wc);

    const UINT dpi = GetDpiForSystem();
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kChatClass, ui::T("chat.title_default").c_str(), WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, ui::Scale(460, dpi), ui::Scale(620, dpi), nullptr, nullptr,
                            instance, this);
    if (!hwnd_) return false;
    auto make = [&](const wchar_t* cls, const std::wstring& text, DWORD style, int id, DWORD ex = 0) {
        return ui::Make(hwnd_, cls, text.c_str(), style, id, ex);
    };
    make(L"Static", ui::T("chat.signed_out"), SS_LEFT | SS_ENDELLIPSIS | SS_CENTERIMAGE, IDC_CHAT_ACCOUNT);
    make(L"Button", ui::T("chat.continue"), BS_PUSHBUTTON | WS_TABSTOP, IDC_CHAT_SIGNIN);
    make(L"Static", ui::T("chat.using_plan"), SS_LEFT | SS_ENDELLIPSIS | SS_CENTERIMAGE, IDC_CHAT_PLAN);
    make(L"Button", ui::T("chat.manage_usage"), BS_PUSHBUTTON | WS_TABSTOP, IDC_CHAT_USAGE);
    make(L"Static", ui::T("chat.model"), SS_LEFT | SS_CENTERIMAGE, IDC_CHAT_MODEL_LABEL);
    make(L"ComboBox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_CHAT_MODEL);
    HWND wt = make(L"Static", ui::T("chat.welcome_title"), SS_LEFT, IDC_CHAT_WELCOME_TITLE);
    SendMessageW(wt, WM_SETFONT, reinterpret_cast<WPARAM>(ui::UiFont(GetDpiForWindow(hwnd_), 0, true)), TRUE);
    make(L"Static", ui::T("chat.welcome_body"), SS_LEFT, IDC_CHAT_WELCOME);
    make(L"Button", ui::T("chat.got_it"), BS_PUSHBUTTON | WS_TABSTOP, IDC_CHAT_GOTIT);
    make(L"Edit", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, IDC_CHAT_HISTORY,
         WS_EX_CLIENTEDGE);
    SendMessageW(Item(IDC_CHAT_HISTORY), EM_SETLIMITTEXT, 0, 0);
    make(L"Edit", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_CHAT_INPUT, WS_EX_CLIENTEDGE);
    SendMessageW(Item(IDC_CHAT_INPUT), EM_SETLIMITTEXT, 4000, 0);
    SendMessageW(Item(IDC_CHAT_INPUT), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(ui::T("chat.input_cue").c_str()));
    make(L"Button", ui::T("chat.send"), BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_CHAT_SEND);
    HWND status = make(L"Static", L"", SS_LEFT, IDC_CHAT_STATUS);
    SetWindowLongPtrW(status, GWLP_USERDATA, 1);  // 보조 글자색 (ui::OnCtlColor)
    make(L"Button", ui::T("chat.local_signout"), BS_PUSHBUTTON | WS_TABSTOP, IDC_CHAT_LOCALOUT);
    ui::ApplyWindowTheme(hwnd_);
    ShowWelcome(false);
    ShowLocalSignOut(false);
    SetAccount(false, false, false, L"");
    Layout();
    return true;
}

void ChatWindow::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void ChatWindow::ApplyTheme() {
    if (hwnd_) ui::ApplyWindowTheme(hwnd_);
}

void ChatWindow::ReloadTexts() {
    if (!hwnd_) return;
    SetWindowTextW(Item(IDC_CHAT_USAGE), ui::T("chat.manage_usage").c_str());
    SetWindowTextW(Item(IDC_CHAT_MODEL_LABEL), ui::T("chat.model").c_str());
    SetWindowTextW(Item(IDC_CHAT_WELCOME_TITLE), ui::T("chat.welcome_title").c_str());
    SetWindowTextW(Item(IDC_CHAT_WELCOME), ui::T("chat.welcome_body").c_str());
    SetWindowTextW(Item(IDC_CHAT_GOTIT), ui::T("chat.got_it").c_str());
    SetWindowTextW(Item(IDC_CHAT_LOCALOUT), ui::T("chat.local_signout").c_str());
    SendMessageW(Item(IDC_CHAT_INPUT), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(ui::T("chat.input_cue").c_str()));
    SetWindowTextW(hwnd_, target_.empty() ? ui::T("chat.title_default").c_str() : ui::TF("chat.title", target_).c_str());
    SetAccount(signedIn_, planUsage_, signingIn_, email_);
    SetObserveNote(observeNote_);
    Layout();
}

void ChatWindow::Show(bool activate) {
    if (!hwnd_) return;
    ShowWindow(hwnd_, activate ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
    if (activate) {
        // [검증] 다른 앱이 전경일 때 SetForegroundWindow 가 성공하는지는 상황에 따라 다르다 (전경 잠금 규칙)
        SetForegroundWindow(hwnd_);
        SetFocus(Item(IDC_CHAT_INPUT));
    }
}

void ChatWindow::Hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

bool ChatWindow::IsDialogMsg(MSG* msg) {
    return hwnd_ && (msg->hwnd == hwnd_ || IsChild(hwnd_, msg->hwnd)) && IsDialogMessageW(hwnd_, msg);
}

void ChatWindow::SetTarget(const std::wstring& petName) {
    target_ = petName;
    SetWindowTextW(hwnd_, ui::TF("chat.title", petName).c_str());
}

void ChatWindow::SetHistory(const std::wstring& text) {
    SetWindowTextW(Item(IDC_CHAT_HISTORY), text.c_str());
    const int len = GetWindowTextLengthW(Item(IDC_CHAT_HISTORY));
    SendMessageW(Item(IDC_CHAT_HISTORY), EM_SETSEL, len, len);
    SendMessageW(Item(IDC_CHAT_HISTORY), EM_SCROLLCARET, 0, 0);
}

void ChatWindow::AppendHistory(const std::wstring& text) {
    HWND h = Item(IDC_CHAT_HISTORY);
    const int len = GetWindowTextLengthW(h);
    SendMessageW(h, EM_SETSEL, len, len);
    SendMessageW(h, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(h, EM_SCROLLCARET, 0, 0);
}

void ChatWindow::SetAccount(bool signedIn, bool planUsage, bool signingIn, const std::wstring& email) {
    signedIn_ = signedIn;
    signingIn_ = signingIn;
    planUsage_ = planUsage;
    email_ = email;
    std::wstring label;
    if (signingIn) label = ui::T("chat.signing_in");
    else if (signedIn) label = ui::TF("chat.account", email.empty() ? ui::T("chat.no_email") : email) +
                               (planUsage ? L"" : ui::T("chat.plan_off"));
    else label = ui::T("chat.signed_out");
    SetWindowTextW(Item(IDC_CHAT_ACCOUNT), label.c_str());
    SetWindowTextW(Item(IDC_CHAT_SIGNIN),
                   (signingIn ? ui::T("chat.cancel_signin") : signedIn ? ui::T("chat.sign_out") : ui::T("chat.continue")).c_str());
    // "Using ChatGPT plan" 은 plan usage 로 요청할 때만 표시 (UI 지침)
    ShowWindow(Item(IDC_CHAT_PLAN), signedIn && planUsage ? SW_SHOW : SW_HIDE);
    ShowWindow(Item(IDC_CHAT_USAGE), signedIn ? SW_SHOW : SW_HIDE);
    // 로그인 전에는 모델 선택을 숨긴다 (빈 목록이 혼란스러웠다, UI 점검 U8)
    ShowWindow(Item(IDC_CHAT_MODEL), signedIn && planUsage ? SW_SHOW : SW_HIDE);
    ShowWindow(Item(IDC_CHAT_MODEL_LABEL), signedIn && planUsage ? SW_SHOW : SW_HIDE);
    SetBusy(streaming_);
    Layout();
}

void ChatWindow::SetObserveNote(const std::wstring& note) {
    observeNote_ = note;
    if (hwnd_) SetWindowTextW(Item(IDC_CHAT_PLAN), (ui::T("chat.using_plan") + observeNote_).c_str());
}

void ChatWindow::SetModels(const std::vector<ai::ModelInfo>& models, int selected) {
    HWND c = Item(IDC_CHAT_MODEL);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (const auto& m : models) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ToWide(m.displayName).c_str()));
    SendMessageW(c, CB_SETCURSEL, selected, 0);
}

void ChatWindow::SetBusy(bool streaming) {
    streaming_ = streaming;
    SetWindowTextW(Item(IDC_CHAT_SEND), (streaming ? ui::T("chat.stop") : ui::T("chat.send")).c_str());
    EnableWindow(Item(IDC_CHAT_SEND), streaming || signedIn_);
}

void ChatWindow::SetStatus(const std::wstring& text) { SetWindowTextW(Item(IDC_CHAT_STATUS), text.c_str()); }

void ChatWindow::ShowWelcome(bool show) {
    welcome_ = show;
    ShowWindow(Item(IDC_CHAT_WELCOME_TITLE), show ? SW_SHOW : SW_HIDE);
    ShowWindow(Item(IDC_CHAT_WELCOME), show ? SW_SHOW : SW_HIDE);
    ShowWindow(Item(IDC_CHAT_GOTIT), show ? SW_SHOW : SW_HIDE);
    Layout();
}

void ChatWindow::ShowLocalSignOut(bool show) {
    localOut_ = show;
    ShowWindow(Item(IDC_CHAT_LOCALOUT), show ? SW_SHOW : SW_HIDE);
    Layout();
}

void ChatWindow::Layout() {
    if (!hwnd_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const UINT dpi = GetDpiForWindow(hwnd_);
    auto S = [dpi](int v) { return ui::Scale(v, dpi); };
    const int m = S(14), row = S(30), gap = S(8), W = rc.right - 2 * m;
    int y = m;
    // 1줄: 계정 ……… [로그인/로그아웃]
    const int signW = S(176);
    MoveWindow(Item(IDC_CHAT_ACCOUNT), m, y, W - signW - gap, row, TRUE);
    MoveWindow(Item(IDC_CHAT_SIGNIN), rc.right - m - signW, y, signW, row, TRUE);
    y += row + gap;
    // 2줄 (로그인·plan usage): Using ChatGPT plan [· 관찰] ……… [Manage usage]
    // 3줄: 모델 [선택]  — 한 줄에 몰면 플랜 문구가 잘린다
    if (signedIn_) {
        const int usageW = S(116), comboW = std::min(S(220), W / 2), labelW = S(44);
        MoveWindow(Item(IDC_CHAT_PLAN), m, y, W - usageW - gap, row, TRUE);
        MoveWindow(Item(IDC_CHAT_USAGE), rc.right - m - usageW, y, usageW, row, TRUE);
        y += row + gap;
        if (planUsage_) {
            MoveWindow(Item(IDC_CHAT_MODEL_LABEL), m, y, labelW, row, TRUE);
            MoveWindow(Item(IDC_CHAT_MODEL), m + labelW + gap, y + S(2), comboW, S(240), TRUE);
            y += row + gap;
        }
    }
    if (welcome_) {  // 첫 안내 (SIWC UI 지침): 제목 + 설명 + Got it
        const int gotW = S(84);
        MoveWindow(Item(IDC_CHAT_WELCOME_TITLE), m, y, W - gotW - gap, S(22), TRUE);
        MoveWindow(Item(IDC_CHAT_WELCOME), m, y + S(22), W - gotW - gap, S(40), TRUE);
        MoveWindow(Item(IDC_CHAT_GOTIT), rc.right - m - gotW, y + S(14), gotW, row, TRUE);
        y += S(62) + gap;
    }
    const int bottom = rc.bottom - m;
    const int statusH = S(36), inputH = S(32);
    int histBottom = bottom - statusH - gap - inputH - gap;
    if (localOut_) histBottom -= row + gap;
    MoveWindow(Item(IDC_CHAT_HISTORY), m, y, W, std::max(S(60), histBottom - y), TRUE);
    int yy = histBottom + gap;
    const int sendW = S(92);
    MoveWindow(Item(IDC_CHAT_INPUT), m, yy, W - sendW - gap, inputH, TRUE);
    MoveWindow(Item(IDC_CHAT_SEND), rc.right - m - sendW, yy, sendW, inputH, TRUE);
    yy += inputH + gap;
    if (localOut_) {
        MoveWindow(Item(IDC_CHAT_LOCALOUT), m, yy, S(180), row, TRUE);
        yy += row + gap;
    }
    MoveWindow(Item(IDC_CHAT_STATUS), m, yy, W, statusH, TRUE);
}

LRESULT CALLBACK ChatWindow::StaticProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<ChatWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->Proc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ChatWindow::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: Layout(); return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, ui::WindowBrush());
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            if (HBRUSH b = ui::OnCtlColor(msg, reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp)))
                return reinterpret_cast<LRESULT>(b);
            break;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {MulDiv(380, static_cast<int>(GetDpiForWindow(hwnd)), 96),
                                  MulDiv(360, static_cast<int>(GetDpiForWindow(hwnd)), 96)};
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            // 새 DPI 의 글꼴로 (제목은 굵게)
            for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
                SendMessageW(c, WM_SETFONT,
                             reinterpret_cast<WPARAM>(ui::UiFont(HIWORD(wp), 0, GetDlgCtrlID(c) == IDC_CHAT_WELCOME_TITLE)), TRUE);
            Layout();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (ui::IsThemeChangeMessage(msg, lp)) {  // Windows 앱 모드 변경 (테마 auto 일 때 따라감)
                ui::RefreshTheme();
                ApplyTheme();
            }
            break;
        case WM_CLOSE:
            Hide();  // 닫기 = 숨김 (대화 이력은 메모리에 유지)
            if (cb_.closed) cb_.closed();
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == IDC_CHAT_MODEL && code == CBN_SELCHANGE) {
                if (cb_.modelChanged) cb_.modelChanged(static_cast<int>(SendMessageW(Item(IDC_CHAT_MODEL), CB_GETCURSEL, 0, 0)));
                return 0;
            }
            if (code != BN_CLICKED && id != IDOK) return 0;
            switch (id) {
                case IDOK:  // 입력창에서 Enter (IsDialogMessage 가 기본 버튼 명령을 보냄)
                case IDC_CHAT_SEND:
                    if (streaming_) {
                        if (cb_.stop) cb_.stop();
                    } else {
                        HWND in = Item(IDC_CHAT_INPUT);
                        std::wstring text(static_cast<size_t>(GetWindowTextLengthW(in)), L'\0');
                        GetWindowTextW(in, text.data(), static_cast<int>(text.size()) + 1);
                        if (!text.empty() && text.find_first_not_of(L" \t") != std::wstring::npos && cb_.send) {
                            SetWindowTextW(in, L"");
                            cb_.send(text);
                        }
                    }
                    return 0;
                case IDC_CHAT_SIGNIN:
                    if (signingIn_) { if (cb_.cancelSignIn) cb_.cancelSignIn(); }
                    else if (signedIn_) { if (cb_.signOut) cb_.signOut(); }
                    else if (cb_.signIn) cb_.signIn();
                    return 0;
                case IDC_CHAT_LOCALOUT: if (cb_.signOutLocal) cb_.signOutLocal(); return 0;
                case IDC_CHAT_GOTIT: if (cb_.gotIt) cb_.gotIt(); return 0;
                case IDC_CHAT_USAGE: if (cb_.manageUsage) cb_.manageUsage(); return 0;
                case IDCANCEL: Hide(); return 0;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace dp
