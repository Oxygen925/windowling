#pragma once
// 앱 창(대화창·설정·캐릭터 관리·환영·동의) 공통: 테마(밝게/어둡게), 글꼴, DPI, 컨트롤 만들기·칠하기.
// 다크 모드는 문서화된 API 만 쓴다 (UI 계획 §2.4): 제목 표시줄 DWMWA_USE_IMMERSIVE_DARK_MODE, 배경·글자 WM_CTLCOLOR*,
// 컨트롤 SetWindowTheme("DarkMode_Explorer"/"DarkMode_CFD"). 팝업 메뉴는 밝게 남는다 (한계).
#include <windows.h>

#include <string>

namespace dp::ui {

struct Palette {
    COLORREF window, text, subtle, control, controlText, accent, border;
};

// "auto" | "light" | "dark". auto 는 Windows 앱 모드(AppsUseLightTheme)
void SetThemePreference(const std::string& pref);
void RefreshTheme();  // Windows 앱 모드가 바뀐 뒤 (같은 설정으로 다시 판단)
bool IsDark();
const Palette& Colors();
HBRUSH WindowBrush();
HBRUSH ControlBrush();

// 창 단위
void ApplyWindowTheme(HWND hwnd);        // 제목 표시줄 + 모든 자식 컨트롤 테마 다시 적용 + 다시 그리기
void ThemeControl(HWND control);         // 컨트롤 하나 (클래스 이름으로 테마 선택)
// WM_CTLCOLOR* 처리. 처리했으면 브러시, 아니면 nullptr (호출자가 DefWindowProc)
HBRUSH OnCtlColor(UINT msg, HDC dc, HWND control);
bool IsThemeChangeMessage(UINT msg, LPARAM lp);  // WM_SETTINGCHANGE("ImmersiveColorSet")

// 앱 아이콘 (리소스 1, UI-02). smallIcon = 16px 계열(트레이·제목 표시줄)
HICON AppIcon(bool smallIcon);

// 글꼴 (DPI 별, 프로세스 공유)
HFONT UiFont(UINT dpi, int pointDelta = 0, bool bold = false);
int Scale(int v, UINT dpi);

// 컨트롤 만들기: 글꼴·테마 적용
HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0);
// 체크박스: 테마가 켜진 체크박스는 다크에서 글자색을 바꿀 수 없어, 상자와 글자(SS_NOTIFY 레이블)를 따로 만든다.
// 레이블 클릭은 상자를 토글한다 (HandleCheckLabel). 레이블 ID = 상자 ID + kLabelOffset
inline constexpr int kLabelOffset = 5000;
HWND MakeCheck(HWND parent, const wchar_t* text, int id, bool radio = false);
bool HandleCheckLabel(HWND parent, WPARAM wp);  // WM_COMMAND 에서: 레이블 클릭이면 토글하고 true

}  // namespace dp::ui
