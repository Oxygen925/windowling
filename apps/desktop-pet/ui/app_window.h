#pragma once
// 앱 창(동의·설정·캐릭터 관리·환영) 공통 틀: 창 클래스 등록, 크기(DPI), 가운데 배치, 테마·글꼴·DPI 변경,
// Tab·Enter·Esc(IsDialogMessage) 처리. 모달 루프를 돌리지 않는다 — 앱 메시지 루프가 RouteDialogMessage 를 부른다.
#include <windows.h>

#include <string>

namespace dp::ui {

class AppWindow {
public:
    virtual ~AppWindow();
    AppWindow() = default;
    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    HWND Hwnd() const { return hwnd_; }
    bool IsOpen() const { return hwnd_ != nullptr; }
    void Close();           // 창을 없앤다 (OnClosed 호출)
    void ApplyTheme();      // 테마를 바꾼 뒤
    void BringToFront();

    // 앱 메시지 루프에서: 열린 앱 창의 Tab·Enter·Esc 처리. 처리했으면 true
    static bool RouteDialogMessage(MSG* msg);
    // 열린 모든 앱 창에 테마 다시 적용 / 문구 다시 읽기
    static void ApplyThemeAll();
    static void ReloadTextsAll();

protected:
    struct Spec {
        const wchar_t* className;   // 창마다 다른 클래스 (시험 스크립트가 찾는 이름)
        std::wstring title;
        int clientW, clientH;       // 96 DPI 기준
        int minW = 0, minH = 0;     // 96 DPI 기준, 0 = 크기 고정
        bool topmost = false;
        bool activate = true;       // false: 포커스를 빼앗지 않는다 (환영 창)
    };
    // 없으면 만들고, 있으면 앞으로. 만들었으면 true
    bool Open(const Spec& spec);

    virtual void OnCreate() = 0;                       // 컨트롤 만들기
    virtual void OnLayout(int width, int height) = 0;  // 클라이언트 크기
    virtual void OnCommand(int id, int code, HWND control) = 0;
    virtual void OnCloseRequested() { Close(); }       // 제목 표시줄 X·Esc
    virtual void OnClosed() {}
    virtual void OnReloadTexts() {}
    virtual bool IsBoldControl(int /*id*/) const { return false; }
    // 그 밖의 메시지. 처리했으면 handled = true
    virtual LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
        (void)msg, (void)wp, (void)lp;
        handled = false;
        return 0;
    }

    HWND Item(int id) const { return GetDlgItem(hwnd_, id); }
    int S(int v) const;
    // 컨트롤 만들기 (글꼴·테마 적용). bold 이면 IsBoldControl 이 그 id 에 true 를 돌려줘야 DPI 변경 뒤에도 유지된다
    HWND Add(const wchar_t* cls, const std::wstring& text, DWORD style, int id, DWORD exStyle = 0);
    void Relayout();

    HWND hwnd_ = nullptr;

private:
    static LRESULT CALLBACK StaticProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Proc(HWND, UINT, WPARAM, LPARAM);
    void ResetFonts(UINT dpi);
    Spec spec_{};
};

}  // namespace dp::ui
