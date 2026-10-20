#pragma once
// 설정 창 (UI-07). 왼쪽 목록으로 4개 영역(일반·표시 / AI 대화 / 화면 관찰 / 음성·전체화면 예외)을 나눈다 (탭은 다크 모드가 안 됨).
// "적용" = 값 검사(숫자 → 설정 파일 범위 검사) → 콜백(PetApp 이 설정 파일 저장 + 즉시 반영). 화면 관찰 켜고 끄기는 여기 없다 (동의 필요).
// 컨트롤 ID 는 시험 스크립트가 쓰므로 바꾸지 않는다.
#include <functional>
#include <string>

#include "core/config.h"
#include "ui/app_window.h"

namespace dp {

enum SettingsControlId {
    IDC_SET_NAV = 501,
    IDC_SET_APPLY = 502,
    IDC_SET_OPEN_FILE = 504,
    IDC_SET_STATUS = 505,
    // 일반·표시 (6xx)
    IDC_SET_LANGUAGE_L = 601, IDC_SET_LANGUAGE, IDC_SET_THEME_L, IDC_SET_THEME, IDC_SET_SCALE_L, IDC_SET_SCALE,
    IDC_SET_FRAME_L, IDC_SET_FRAME, IDC_SET_PROXIMITY, IDC_SET_STARTUP, IDC_SET_UPDATE, IDC_SET_WIPE, IDC_SET_WIPE_NOTE,
    // AI 대화 (7xx)
    IDC_SET_AI_ENABLED = 701, IDC_SET_HOTKEY_L, IDC_SET_HOTKEY, IDC_SET_HOTKEY_HINT, IDC_SET_BUBBLE_L, IDC_SET_BUBBLE,
    IDC_SET_HISTORY_L, IDC_SET_HISTORY, IDC_SET_FIRST_L, IDC_SET_FIRST, IDC_SET_REQUEST_L, IDC_SET_REQUEST, IDC_SET_AI_NOTE,
    // 화면 관찰 (8xx)
    IDC_SET_OBS_NOTE = 801, IDC_SET_INTERVAL_L, IDC_SET_INTERVAL_MIN, IDC_SET_INTERVAL_SEP, IDC_SET_INTERVAL_MAX,
    IDC_SET_PER_HOUR_L, IDC_SET_PER_HOUR, IDC_SET_PER_DAY_L, IDC_SET_PER_DAY, IDC_SET_AWAY_L, IDC_SET_AWAY, IDC_SET_SEND_APP,
    IDC_SET_BEHAVIORS_L, IDC_SET_BEH_SIT, IDC_SET_BEH_LOOK, IDC_SET_BEH_IDLE, IDC_SET_BEH_WALK, IDC_SET_EXCLUDE_L,
    IDC_SET_EXCLUDE,
    // 음성·전체화면 예외 (9xx)
    IDC_SET_TTS_ENABLED = 901, IDC_SET_READ_REPLIES, IDC_SET_ENGINE_L, IDC_SET_ENGINE, IDC_SET_VOICE_L, IDC_SET_VOICE,
    IDC_SET_HTTP_URL_L, IDC_SET_HTTP_URL, IDC_SET_HTTP_MODEL_L, IDC_SET_HTTP_MODEL, IDC_SET_HTTP_VOICE_L, IDC_SET_HTTP_VOICE,
    IDC_SET_CMD_PATH_L, IDC_SET_CMD_PATH, IDC_SET_CMD_ARGS_L, IDC_SET_CMD_ARGS, IDC_SET_RATE_L, IDC_SET_RATE,
    IDC_SET_VOLUME_L, IDC_SET_VOLUME, IDC_SET_FULLSCREEN_L, IDC_SET_SHOW_L, IDC_SET_SHOW, IDC_SET_HIDE_L, IDC_SET_HIDE,
    // 닫기 = IDCANCEL (2)
};

struct SettingsResult {
    bool ok = false;
    std::wstring message;
};

struct SettingsCallbacks {
    // 검사를 통과한 값 + 시작 프로그램 등록 여부 → 저장·반영
    std::function<SettingsResult(const core::Config& draft, bool startup)> apply;
    std::function<void()> openFile;
    std::function<bool()> startupEnabled;
    std::function<void()> wipe;  // 모든 데이터 지우기 (P6-04, 확인은 PetApp 이 한다)
};

class SettingsWindow : public ui::AppWindow {
public:
    enum Page { kGeneral = 0, kAi, kObserve, kTts, kPageCount };
    // 없으면 current 값으로 만들고, 있으면 그 영역으로 넘기고 앞으로
    void Show(const core::Config& current, Page page, SettingsCallbacks callbacks);

protected:
    void OnCreate() override;
    void OnLayout(int width, int height) override;
    void OnCommand(int id, int code, HWND control) override;
    void OnReloadTexts() override;
    bool IsBoldControl(int id) const override;
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;  // 왼쪽 목록 그리기

private:
    void SelectPage(int page);
    void UpdateVisibility();
    void FillCombos();
    void Fill(const core::Config& c);
    bool Read(core::Config& out, int& badId) const;  // 숫자로 못 바꾸는 칸이 있으면 false
    void Apply();
    void SetStatus(const std::wstring& text);

    core::Config base_;
    SettingsCallbacks cb_;
    int page_ = kGeneral;
};

}  // namespace dp
