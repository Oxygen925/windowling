#pragma once
#include <windows.h>

#include <shellapi.h>

namespace dp {

// Shell_NotifyIconW (NOTIFYICON_VERSION_4). 탐색기 재시작 시 TaskbarCreated 브로드캐스트로 재등록한다.
// 브로드캐스트를 받으려면 owner는 HWND_MESSAGE가 아닌 숨김 최상위 창이어야 한다.
class TrayIcon {
public:
    ~TrayIcon() { Remove(); }
    bool Add(HWND owner, UINT callbackMessage, const wchar_t* tooltip, HICON icon = nullptr);
    bool Readd();
    void Remove();
    void SetTip(const wchar_t* tooltip);
    void ShowBalloon(const wchar_t* title, const wchar_t* text);  // 알림 (NIF_INFO)
    static UINT TaskbarCreatedMessage();

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

}  // namespace dp
