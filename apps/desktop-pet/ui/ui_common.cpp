#include "ui/ui_common.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <cwchar>
#include <map>

namespace dp::ui {

namespace {

std::string g_pref = "auto";
bool g_dark = false;
HBRUSH g_window = nullptr, g_control = nullptr;

const Palette kLight{RGB(249, 249, 249), RGB(26, 26, 26), RGB(96, 96, 96), RGB(255, 255, 255), RGB(26, 26, 26),
                     RGB(0, 95, 184), RGB(204, 204, 204)};
const Palette kDark{RGB(32, 32, 32), RGB(240, 240, 240), RGB(170, 170, 170), RGB(45, 45, 45), RGB(240, 240, 240),
                    RGB(96, 205, 255), RGB(70, 70, 70)};

bool SystemDark() {
    DWORD v = 1, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return false;
    return v == 0;
}

void RefreshBrushes() {
    g_dark = g_pref == "dark" || (g_pref == "auto" && SystemDark());
    if (g_window) DeleteObject(g_window);
    if (g_control) DeleteObject(g_control);
    g_window = CreateSolidBrush(Colors().window);
    g_control = CreateSolidBrush(Colors().control);
}

std::wstring ClassOf(HWND h) {
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 64);
    return cls;
}

}  // namespace

void SetThemePreference(const std::string& pref) {
    g_pref = pref == "light" || pref == "dark" ? pref : "auto";
    RefreshBrushes();
}

void RefreshTheme() { RefreshBrushes(); }

bool IsDark() {
    if (!g_window) RefreshBrushes();
    return g_dark;
}

const Palette& Colors() { return g_dark ? kDark : kLight; }

HBRUSH WindowBrush() {
    if (!g_window) RefreshBrushes();
    return g_window;
}

HBRUSH ControlBrush() {
    if (!g_control) RefreshBrushes();
    return g_control;
}

void ThemeControl(HWND c) {
    const std::wstring cls = ClassOf(c);
    const bool dark = IsDark();
    if (cls == L"Edit" && (GetWindowLongPtrW(c, GWL_STYLE) & ES_MULTILINE)) {
        SetWindowTheme(c, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);  // 여러 줄: 스크롤바도 어둡게
    } else if (cls == L"Edit" || cls == L"ComboBox") {
        SetWindowTheme(c, dark ? L"DarkMode_CFD" : L"Explorer", nullptr);
    } else if (cls == WC_LISTVIEWW) {
        SetWindowTheme(c, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        ListView_SetBkColor(c, Colors().control);
        ListView_SetTextBkColor(c, Colors().control);
        ListView_SetTextColor(c, Colors().controlText);
        if (HWND header = ListView_GetHeader(c)) SetWindowTheme(header, dark ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);
    } else if (cls == L"Button" || cls == L"ListBox" || cls == UPDOWN_CLASSW || cls == L"ScrollBar" ||
               cls == WC_TREEVIEWW) {
        SetWindowTheme(c, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
}

void ApplyWindowTheme(HWND hwnd) {
    const BOOL dark = IsDark();
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) ThemeControl(c);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

HBRUSH OnCtlColor(UINT msg, HDC dc, HWND control) {
    const Palette& p = Colors();
    switch (msg) {
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor(dc, p.controlText);
            SetBkColor(dc, p.control);
            return ControlBrush();
        case WM_CTLCOLORSTATIC: {
            // 읽기 전용 EDIT 도 STATIC 메시지로 온다 → 입력 칸 색
            const bool edit = ClassOf(control) == L"Edit";
            const LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
            const bool subtle = (style & SS_TYPEMASK) == SS_LEFT && GetWindowLongPtrW(control, GWLP_USERDATA) == 1;
            SetTextColor(dc, subtle ? p.subtle : (edit ? p.controlText : p.text));
            SetBkColor(dc, edit ? p.control : p.window);
            return edit ? ControlBrush() : WindowBrush();
        }
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORDLG:
            SetTextColor(dc, p.text);
            SetBkColor(dc, p.window);
            return WindowBrush();
    }
    return nullptr;
}

bool IsThemeChangeMessage(UINT msg, LPARAM lp) {
    return msg == WM_SETTINGCHANGE && lp && std::wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0;
}

HICON AppIcon(bool smallIcon) {
    static HICON big = nullptr, sm = nullptr;
    HICON& h = smallIcon ? sm : big;
    if (!h) {
        const UINT dpi = GetDpiForSystem();
        const int cx = GetSystemMetricsForDpi(smallIcon ? SM_CXSMICON : SM_CXICON, dpi);
        h = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, cx, cx, LR_DEFAULTCOLOR));
        if (!h) h = LoadIconW(nullptr, IDI_APPLICATION);
    }
    return h;
}

int Scale(int v, UINT dpi) { return MulDiv(v, static_cast<int>(dpi), 96); }

HFONT UiFont(UINT dpi, int pointDelta, bool bold) {
    static std::map<long long, HFONT> cache;
    const long long key = (static_cast<long long>(dpi) << 16) | ((pointDelta + 64) << 1) | (bold ? 1 : 0);
    auto& f = cache[key];
    if (!f) {
        NONCLIENTMETRICSW ncm{sizeof(ncm)};
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
        LOGFONTW lf = ncm.lfMessageFont;
        if (pointDelta) lf.lfHeight = lf.lfHeight * (9 + pointDelta) / 9;
        if (bold) lf.lfWeight = FW_SEMIBOLD;
        f = CreateFontIndirectW(&lf);
    }
    return f;
}

HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont(GetDpiForWindow(parent))), TRUE);
    ThemeControl(h);
    return h;
}

HWND MakeCheck(HWND parent, const wchar_t* text, int id, bool radio) {
    HWND box = Make(parent, L"Button", L"", (radio ? BS_AUTORADIOBUTTON : BS_AUTOCHECKBOX) | WS_TABSTOP, id);
    Make(parent, L"Static", text, SS_LEFT | SS_NOTIFY | SS_CENTERIMAGE, id + kLabelOffset);
    return box;
}

bool HandleCheckLabel(HWND parent, WPARAM wp) {
    const int id = LOWORD(wp);
    if (id < kLabelOffset || HIWORD(wp) != STN_CLICKED) return false;
    HWND box = GetDlgItem(parent, id - kLabelOffset);
    if (!box) return false;
    const LONG_PTR style = GetWindowLongPtrW(box, GWL_STYLE) & BS_TYPEMASK;
    if (style == BS_AUTORADIOBUTTON) SendMessageW(box, BM_SETCHECK, BST_CHECKED, 0);
    else SendMessageW(box, BM_SETCHECK, SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id - kLabelOffset, BN_CLICKED), reinterpret_cast<LPARAM>(box));
    return true;
}

}  // namespace dp::ui
