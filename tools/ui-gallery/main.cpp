// 확인용 (UI 계획 UI-04): 설정·캐릭터 관리 창에 쓸 컨트롤을 한 창에 모아 밝게/어둡게 그려 PNG 로 저장한다.
//   ui-gallery <light|dark> <out.png>
#include <windows.h>

#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <string>

#include "ui/ui_common.h"

using namespace dp;

namespace {

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (HBRUSH b = ui::OnCtlColor(msg, reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp))) return reinterpret_cast<LRESULT>(b);
    if (msg == WM_ERASEBKGND) {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc, ui::WindowBrush());
        return 1;
    }
    if (msg == WM_COMMAND && ui::HandleCheckLabel(h, wp)) return 0;
    return DefWindowProcW(h, msg, wp, lp);
}

void Save(HWND h, const std::wstring& path) {
    RECT r;
    GetWindowRect(h, &r);
    const int w = r.right - r.left, hh = r.bottom - r.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -hh, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    PrintWindow(h, mem, PW_RENDERFULLCONTENT);
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    wic->CreateStream(&stream);
    stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    enc->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(w, hh);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    auto* px = static_cast<BYTE*>(bits);
    for (int i = 0; i < w * hh; ++i) px[i * 4 + 3] = 255;  // PrintWindow 는 알파를 채우지 않는다
    frame->WritePixels(hh, w * 4, w * hh * 4, px);
    frame->Commit();
    enc->Commit();
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    ui::SetThemePreference(argc > 1 && std::wstring(argv[1]) == L"dark" ? "dark" : "light");
    const std::wstring out = argc > 2 ? argv[2] : L"gallery.png";
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DesktopPet.UiGallery";
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"UI gallery — 설정 미리보기", WS_OVERLAPPEDWINDOW, 100, 100, 640, 460,
                             nullptr, nullptr, wc.hInstance, nullptr);
    const UINT dpi = GetDpiForWindow(h);
    auto S = [dpi](int v) { return ui::Scale(v, dpi); };
    SetWindowPos(h, nullptr, 0, 0, S(640), S(430), SWP_NOMOVE | SWP_NOZORDER);
    auto place = [&](HWND c, int x, int y, int w, int hh) { MoveWindow(c, S(x), S(y), S(w), S(hh), FALSE); };
    // 왼쪽 목록 (설정 영역)
    HWND nav = ui::Make(h, L"ListBox", L"", LBS_NOTIFY | WS_TABSTOP, 100);
    for (const wchar_t* s : {L"일반·표시", L"AI 대화", L"화면 관찰", L"TTS·전체화면"}) SendMessageW(nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(nav, LB_SETCURSEL, 0, 0);
    place(nav, 12, 12, 150, 380);
    HWND title = ui::Make(h, L"Static", L"일반·표시", SS_LEFT, 101);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(ui::UiFont(dpi, 4, true)), TRUE);
    place(title, 180, 12, 300, 28);
    HWND lbl = ui::Make(h, L"Static", L"크기 배율", SS_LEFT, 102);
    place(lbl, 180, 52, 120, 22);
    HWND edit = ui::Make(h, L"Edit", L"1.0", ES_AUTOHSCROLL | WS_TABSTOP, 103, WS_EX_CLIENTEDGE);
    place(edit, 310, 50, 80, 24);
    HWND spin = ui::Make(h, UPDOWN_CLASSW, L"", UDS_ALIGNRIGHT | UDS_SETBUDDYINT | UDS_ARROWKEYS, 104);
    SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(edit), 0);
    HWND lbl2 = ui::Make(h, L"Static", L"언어", SS_LEFT, 105);
    place(lbl2, 180, 88, 120, 22);
    HWND combo = ui::Make(h, L"ComboBox", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 106);
    for (const wchar_t* s : {L"자동 (Windows)", L"한국어", L"English"}) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    place(combo, 310, 86, 160, 200);
    HWND c1 = ui::MakeCheck(h, L"커서가 가까이 오면 반응", 107);
    SendMessageW(c1, BM_SETCHECK, BST_CHECKED, 0);
    place(c1, 180, 124, 18, 22);
    place(GetDlgItem(h, 107 + ui::kLabelOffset), 202, 124, 260, 22);
    HWND r1 = ui::MakeCheck(h, L"테마: 자동", 108, true);
    place(r1, 180, 154, 18, 22);
    place(GetDlgItem(h, 108 + ui::kLabelOffset), 202, 154, 120, 22);
    SendMessageW(r1, BM_SETCHECK, BST_CHECKED, 0);
    HWND r2 = ui::MakeCheck(h, L"어둡게", 109, true);
    place(r2, 330, 154, 18, 22);
    place(GetDlgItem(h, 109 + ui::kLabelOffset), 352, 154, 100, 22);
    HWND hint = ui::Make(h, L"Static", L"바꾼 값은 '적용'을 누르면 바로 반영됩니다.", SS_LEFT, 110);
    SetWindowLongPtrW(hint, GWLP_USERDATA, 1);  // 보조 글자색
    place(hint, 180, 186, 420, 22);
    HWND lv = ui::Make(h, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP, 111, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col{LVCF_TEXT | LVCF_WIDTH};
    col.cx = S(140);
    col.pszText = const_cast<LPWSTR>(L"캐릭터");
    ListView_InsertColumn(lv, 0, &col);
    col.pszText = const_cast<LPWSTR>(L"상태");
    col.cx = S(120);
    ListView_InsertColumn(lv, 1, &col);
    const wchar_t* rows[][2] = {{L"Test Blue", L"2마리"}, {L"Test Fox (3D)", L"띄우지 않음"}, {L"broken-pack", L"거부됨"}};
    for (int i = 0; i < 3; ++i) {
        LVITEMW it{LVIF_TEXT};
        it.iItem = i;
        it.pszText = const_cast<LPWSTR>(rows[i][0]);
        ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, i, 1, const_cast<LPWSTR>(rows[i][1]));
    }
    ListView_SetItemState(lv, 0, LVIS_SELECTED, LVIS_SELECTED);
    place(lv, 180, 216, 300, 110);
    HWND apply = ui::Make(h, L"Button", L"적용", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK);
    place(apply, 420, 340, 90, 30);
    HWND cancel = ui::Make(h, L"Button", L"닫기", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);
    place(cancel, 516, 340, 90, 30);
    ui::ApplyWindowTheme(h);
    ShowWindow(h, SW_SHOWNOACTIVATE);
    UpdateWindow(h);
    const ULONGLONG until = GetTickCount64() + 700;
    MSG m;
    while (GetTickCount64() < until) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(10);
    }
    Save(h, out);
    DestroyWindow(h);
    return 0;
}
