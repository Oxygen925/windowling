#pragma once
// 환영 창 (UI-09): 첫 실행 때(포커스를 빼앗지 않음) 또는 트레이 "도움말" 에서. 소개·조작·바로가기(캐릭터 관리·설정).
#include <functional>

#include "ui/app_window.h"

namespace dp {

enum WelcomeControlId {
    IDC_WEL_HEADING = 1101,
    IDC_WEL_INTRO,
    IDC_WEL_CONTROLS_L,
    IDC_WEL_CONTROLS,
    IDC_WEL_MORE,
    IDC_WEL_CHARACTERS,
    IDC_WEL_SETTINGS,
    IDC_WEL_START,
    IDC_WEL_NOCHAR,  // 캐릭터가 하나도 없을 때 안내 (P6-05: 배포판에는 캐릭터가 없다)
};

struct WelcomeCallbacks {
    std::function<void()> openCharacters;
    std::function<void()> openSettings;
};

class WelcomeWindow : public ui::AppWindow {
public:
    void Show(WelcomeCallbacks callbacks, bool activate, bool noCharacters);

protected:
    void OnCreate() override;
    void OnLayout(int width, int height) override;
    void OnCommand(int id, int code, HWND control) override;
    void OnReloadTexts() override;
    bool IsBoldControl(int id) const override { return id == IDC_WEL_HEADING || id == IDC_WEL_CONTROLS_L || id == IDC_WEL_NOCHAR; }

private:
    WelcomeCallbacks cb_;
    bool noCharacters_ = false;
};

}  // namespace dp
