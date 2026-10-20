#include "ui/app_window.h"

#include <algorithm>
#include <vector>

#include "ui/ui_common.h"

namespace dp::ui {

namespace {
std::vector<AppWindow*> g_open;  // UI 스레드 전용
}

AppWindow::~AppWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);  // 파생 부분은 이미 없다 → 더는 가상 함수를 부르지 않는다
        DestroyWindow(hwnd_);
        std::erase(g_open, this);
        hwnd_ = nullptr;
    }
}

int AppWindow::S(int v) const { return Scale(v, hwnd_ ? GetDpiForWindow(hwnd_) : GetDpiForSystem()); }

bool AppWindow::Open(const Spec& spec) {
    if (hwnd_) {
        BringToFront();
        return false;
    }
    spec_ = spec;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    if (!GetClassInfoExW(inst, spec.className, &wc)) {
        wc = {sizeof(wc)};
        wc.lpfnWndProc = &AppWindow::StaticProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = AppIcon(false);
        wc.hIconSm = AppIcon(true);
        wc.lpszClassName = spec.className;
        RegisterClassExW(&wc);
    }
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    if (spec.minW) style |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    const DWORD ex = WS_EX_APPWINDOW | (spec.topmost ? WS_EX_TOPMOST : 0);

    // 커서가 있는 모니터에 만든다 → 그 모니터의 DPI 로 크기를 잡는다
    POINT pt{};
    GetCursorPos(&pt);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT& work = mi.rcWork;
    hwnd_ = CreateWindowExW(ex, spec.className, spec.title.c_str(), style, work.left, work.top, 100, 100, nullptr,
                            nullptr, inst, this);
    if (!hwnd_) return false;
    g_open.push_back(this);

    OnCreate();
    const UINT dpi = GetDpiForWindow(hwnd_);
    RECT r{0, 0, Scale(spec.clientW, dpi), Scale(spec.clientH, dpi)};
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, dpi);
    const int w = std::min<int>(r.right - r.left, work.right - work.left);
    const int h = std::min<int>(r.bottom - r.top, work.bottom - work.top);
    SetWindowPos(hwnd_, nullptr, work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2,
                 w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyWindowTheme(hwnd_);
    Relayout();
    if (spec.activate) {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        // [검증] 다른 앱이 전경일 때는 실패할 수 있다 (전경 잠금 규칙) — 그래도 창은 보인다
        SetForegroundWindow(hwnd_);
    } else {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    return true;
}

void AppWindow::Close() {
    if (hwnd_) DestroyWindow(hwnd_);  // WM_NCDESTROY 에서 정리
}

void AppWindow::ApplyTheme() {
    if (hwnd_) ApplyWindowTheme(hwnd_);
}

void AppWindow::BringToFront() {
    if (!hwnd_) return;
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

bool AppWindow::RouteDialogMessage(MSG* msg) {
    for (AppWindow* w : g_open)
        if (w->hwnd_ && (msg->hwnd == w->hwnd_ || IsChild(w->hwnd_, msg->hwnd))) return IsDialogMessageW(w->hwnd_, msg);
    return false;
}

void AppWindow::ApplyThemeAll() {
    for (AppWindow* w : g_open) w->ApplyTheme();
}

void AppWindow::ReloadTextsAll() {
    for (AppWindow* w : g_open) {
        w->OnReloadTexts();
        w->Relayout();
    }
}

HWND AppWindow::Add(const wchar_t* cls, const std::wstring& text, DWORD style, int id, DWORD exStyle) {
    HWND h = Make(hwnd_, cls, text.c_str(), style, id, exStyle);
    if (IsBoldControl(id)) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont(GetDpiForWindow(hwnd_), 0, true)), TRUE);
    return h;
}

void AppWindow::Relayout() {
    if (!hwnd_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    OnLayout(rc.right, rc.bottom);
}

void AppWindow::ResetFonts(UINT dpi) {
    for (HWND c = GetWindow(hwnd_, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont(dpi, 0, IsBoldControl(GetDlgCtrlID(c)))), TRUE);
}

LRESULT CALLBACK AppWindow::StaticProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->Proc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT AppWindow::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!hwnd_) hwnd_ = hwnd;  // CreateWindowExW 가 돌아오기 전 메시지
    switch (msg) {
        case WM_SIZE:
            if (GetWindow(hwnd, GW_CHILD)) OnLayout(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, WindowBrush());
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            if (HBRUSH b = OnCtlColor(msg, reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp)))
                return reinterpret_cast<LRESULT>(b);
            break;
        case WM_GETMINMAXINFO:
            if (spec_.minW) {
                const UINT dpi = GetDpiForWindow(hwnd);
                reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {Scale(spec_.minW, dpi), Scale(spec_.minH, dpi)};
                return 0;
            }
            break;
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            ResetFonts(HIWORD(wp));
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Relayout();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (IsThemeChangeMessage(msg, lp)) {
                RefreshTheme();
                ApplyTheme();
            }
            break;
        case WM_CLOSE:
            OnCloseRequested();
            return 0;
        case WM_COMMAND:
            if (HandleCheckLabel(hwnd, wp)) return 0;
            if (LOWORD(wp) == IDCANCEL && !lp) {  // Esc (IsDialogMessage)
                OnCloseRequested();
                return 0;
            }
            OnCommand(LOWORD(wp), HIWORD(wp), reinterpret_cast<HWND>(lp));
            return 0;
        case WM_NCDESTROY:
            std::erase(g_open, this);
            hwnd_ = nullptr;
            OnClosed();
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
    bool handled = false;
    const LRESULT r = OnMessage(msg, wp, lp, handled);
    return handled ? r : DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace dp::ui
