#include "platform/windows/tray_icon.h"

#include <cwchar>

#include "diagnostics/log.h"

namespace dp {

UINT TrayIcon::TaskbarCreatedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
}

bool TrayIcon::Add(HWND owner, UINT callbackMessage, const wchar_t* tooltip, HICON icon) {
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = callbackMessage;
    data_.hIcon = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(data_.szTip, tooltip, _TRUNCATE);
    return Readd();
}

bool TrayIcon::Readd() {
    if (!data_.hWnd) return false;
    Shell_NotifyIconW(NIM_DELETE, &data_);
    if (!Shell_NotifyIconW(NIM_ADD, &data_)) {
        log::Warn("Shell_NotifyIcon(NIM_ADD) 실패 err={}", GetLastError());
        added_ = false;
        return false;
    }
    data_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data_);
    added_ = true;
    return true;
}

void TrayIcon::SetTip(const wchar_t* tooltip) {
    wcsncpy_s(data_.szTip, tooltip, _TRUNCATE);
    if (added_) Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::ShowBalloon(const wchar_t* title, const wchar_t* text) {
    if (!added_) return;
    NOTIFYICONDATAW d = data_;
    d.uFlags = NIF_INFO;
    d.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(d.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(d.szInfo, text, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &d);
}

void TrayIcon::Remove() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
}

}  // namespace dp
