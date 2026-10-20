#include "platform/windows/observe_signals.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>

#include "platform/windows/strings.h"

namespace dp {

std::string ExeOfWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    std::string exe;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        std::wstring path(32768, L'\0');  // 긴 경로(MAX_PATH 초과)에 설치된 앱도 이름을 얻는다
        DWORD len = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(h, 0, path.data(), &len)) {
            std::wstring name(path.data(), len);
            name = name.substr(name.find_last_of(L"\\/") + 1);
            std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            exe = ToUtf8(name);
        }
        CloseHandle(h);
    }
    return exe;
}

namespace {

bool ShownOnScreen(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    return true;
}

struct EnumCtx {
    HMONITOR monitor;
    const std::vector<std::string>* exes;
    DWORD self;
    bool found = false;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    auto& c = *reinterpret_cast<EnumCtx*>(lp);
    if (!ShownOnScreen(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == c.self) return TRUE;
    RECT wr{}, mr{};
    MONITORINFO mi{sizeof(mi)};
    if (!GetWindowRect(hwnd, &wr) || !GetMonitorInfoW(c.monitor, &mi)) return TRUE;
    if (!IntersectRect(&mr, &wr, &mi.rcMonitor)) return TRUE;
    const std::string exe = ExeOfWindow(hwnd);
    if (!exe.empty() && std::find(c.exes->begin(), c.exes->end(), exe) != c.exes->end()) {
        c.found = true;
        return FALSE;
    }
    return TRUE;
}

}  // namespace

bool ExcludedAppVisibleOn(HMONITOR monitor, const std::vector<std::string>& exesLower) {
    if (exesLower.empty()) return false;
    EnumCtx c{monitor, &exesLower, GetCurrentProcessId()};
    EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&c));
    return c.found;
}

std::string ForegroundExeOn(HMONITOR monitor) {
    HWND fg = GetForegroundWindow();
    if (!fg || !ShownOnScreen(fg)) return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return {};
    if (MonitorFromWindow(fg, MONITOR_DEFAULTTONULL) != monitor) return {};
    return ExeOfWindow(fg);
}

bool NotificationsBusy() {
    QUERY_USER_NOTIFICATION_STATE q{};
    if (FAILED(SHQueryUserNotificationState(&q))) return false;
    return q == QUNS_BUSY || q == QUNS_RUNNING_D3D_FULL_SCREEN || q == QUNS_PRESENTATION_MODE;
}

double IdleSeconds() {
    LASTINPUTINFO li{sizeof(li)};
    if (!GetLastInputInfo(&li)) return 0;
    return static_cast<DWORD>(GetTickCount() - li.dwTime) / 1000.0;
}

std::string UserLocaleName() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) return "en-US";
    return ToUtf8(name);
}

}  // namespace dp
