#pragma once
// 화면 관찰 동의 창 (UI-06). MessageBox 대신: 내용을 스크롤해 읽고, "이해했습니다" 를 체크해야 "동의하고 켜기" 가 눌린다.
// 모달 루프 없음 — 답은 콜백으로. 제목 표시줄 X·Esc·취소·앱 종료 전 닫힘 = 동의 안 함.
// 컨트롤 ID 는 시험 스크립트(p4_scenarios.ps1)가 쓰므로 바꾸지 않는다.
#include <functional>
#include <string>

#include "ui/app_window.h"

namespace dp {

enum ConsentControlId {
    IDC_CONSENT_HEADING = 401,
    IDC_CONSENT_BODY = 402,
    IDC_CONSENT_WARNING = 403,
    IDC_CONSENT_CHECK = 404,
    IDC_CONSENT_AGREE = 405,
    // 취소 = IDCANCEL (2)
};

class ConsentWindow : public ui::AppWindow {
public:
    using Answer = std::function<void(bool agreed)>;
    // 이미 열려 있으면 앞으로 가져오기만 한다 (먼저 받은 콜백 유지)
    void Ask(const std::wstring& body, Answer answer);

protected:
    void OnCreate() override;
    void OnLayout(int width, int height) override;
    void OnCommand(int id, int code, HWND control) override;
    void OnCloseRequested() override { Finish(false); }
    void OnClosed() override;
    void OnReloadTexts() override;
    bool IsBoldControl(int id) const override { return id == IDC_CONSENT_HEADING || id == IDC_CONSENT_WARNING; }

private:
    void Finish(bool agreed);
    std::wstring body_;
    Answer answer_;
};

}  // namespace dp
