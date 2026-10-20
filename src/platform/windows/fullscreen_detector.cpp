#include "platform/windows/fullscreen_detector.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <format>

#include "diagnostics/log.h"
#include "platform/windows/strings.h"

namespace dp {

namespace {

std::string ExeOfWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    std::string exe;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH];
        DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &len)) {
            std::wstring name(path, len);
            name = name.substr(name.find_last_of(L"\\/") + 1);
            std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            exe = ToUtf8(name);
        }
        CloseHandle(h);
    }
    return exe;
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
    return !s.empty() && std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

bool FullscreenDetector::AnyHidden() const {
    return std::any_of(hidden_.begin(), hidden_.end(), [](bool b) { return b; });
}

bool FullscreenDetector::Update(const core::Config& config, const std::vector<core::MonitorInfo>& monitors) {
    std::vector<bool> next(monitors.size(), false);
    std::string describe = "none";

    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    wchar_t cls[64] = {};
    if (fg) GetClassNameW(fg, cls, 64);
    const bool shell = !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd");
    if (fg && pid != GetCurrentProcessId() && !shell && IsWindowVisible(fg) && !IsIconic(fg)) {
        RECT wr{};
        if (FAILED(DwmGetWindowAttribute(fg, DWMWA_EXTENDED_FRAME_BOUNDS, &wr, sizeof(wr)))) GetWindowRect(fg, &wr);
        const core::RectI r{wr.left, wr.top, wr.right, wr.bottom};
        const std::string exe = ExeOfWindow(fg);
        for (size_t i = 0; i < monitors.size(); ++i) {
            const double cover = core::CoverageRatio(monitors[i].bounds, r);
            bool hide = cover >= config.fullscreenCoverage;
            const char* why = "coverage";
            if (Contains(config.alwaysShowExe, exe)) {
                hide = false;
                why = "always_show";
            } else if (Contains(config.alwaysHideExe, exe) && cover > 0) {
                hide = true;
                why = "always_hide";
            }
            next[i] = hide;
            if (hide || cover >= config.fullscreenCoverage)
                describe = std::format("exe={} monitor={} cover={:.3f} hide={} ({})", exe, i, cover, hide, why);
        }
    }

    // 보조 신호 기록 (판정에는 쓰지 않음)
    QUERY_USER_NOTIFICATION_STATE quns{};
    if (SUCCEEDED(SHQueryUserNotificationState(&quns)) && static_cast<int>(quns) != lastQuns_) {
        lastQuns_ = static_cast<int>(quns);
        log::Info("SHQueryUserNotificationState={} (보조 신호, 판정 미사용)", lastQuns_);
    }

    const bool changed = next != hidden_;
    hidden_ = std::move(next);
    if (changed) {
        describe_ = describe;
        log::Info("전체화면 판정 변경: {}", describe_);
    }
    return changed;
}

}  // namespace dp
