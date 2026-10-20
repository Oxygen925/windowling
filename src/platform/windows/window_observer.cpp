#include "platform/windows/window_observer.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cwctype>

#include "diagnostics/log.h"
#include "platform/windows/strings.h"

namespace dp {

namespace {

WindowObserver* g_observer = nullptr;  // WinEvent 콜백에는 사용자 인자가 없어 단일 인스턴스로 라우팅

constexpr int32_t kMinSurfaceWidth = 64;
constexpr int32_t kMinSurfaceHeight = 32;

bool IsShellClass(const wchar_t* cls) {
    return !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
           !wcscmp(cls, L"Shell_SecondaryTrayWnd");
}

std::string LowerFileName(const std::wstring& path) {
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return ToUtf8(name);
}

}  // namespace

bool WindowObserver::Start() {
    Stop();
    ownPid_ = GetCurrentProcessId();
    g_observer = this;
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    const std::pair<DWORD, DWORD> ranges[] = {
        {EVENT_OBJECT_CREATE, EVENT_OBJECT_REORDER},           // CREATE, DESTROY, SHOW, HIDE, REORDER
        {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
    };
    for (auto [lo, hi] : ranges) {
        HWINEVENTHOOK h = SetWinEventHook(lo, hi, nullptr, &WindowObserver::Proc, 0, 0, flags);
        if (h) hooks_.push_back(h);
        else log::Warn("SetWinEventHook(0x{:X}-0x{:X}) 실패 err={}", lo, hi, GetLastError());
    }
    zDirty_ = fullResync_ = monitorsDirty_ = true;
    log::Info("WindowObserver 시작 hooks={}", hooks_.size());
    return hooks_.size() == std::size(ranges);
}

void WindowObserver::Stop() {
    for (HWINEVENTHOOK h : hooks_) UnhookWinEvent(h);
    if (!hooks_.empty())
        log::Info("WindowObserver 종료 raw={} accepted={} z={} rect={} resync={} snapshots={}", stats_.rawEvents,
                  stats_.acceptedEvents, stats_.zRebuilds, stats_.rectUpdates, stats_.fullResyncs, stats_.snapshots);
    hooks_.clear();
    if (g_observer == this) g_observer = nullptr;
}

void WindowObserver::SetExcludedSurfaceExe(std::vector<std::string> lowerExeNames) {
    excludedExe_ = std::move(lowerExeNames);
    fullResync_ = true;
}

void CALLBACK WindowObserver::Proc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (g_observer) g_observer->OnEvent(event, hwnd, idObject, idChild);
}

void WindowObserver::OnEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild) {
    stats_.rawEvents++;
    if (event == EVENT_OBJECT_REORDER) {  // 발생 대상·범위가 일정하지 않아 순서 재구성만 요청 [검증]
        zDirty_ = true;
        return;
    }
    if (idObject == OBJID_CURSOR) {  // 시스템 커서 이동 (LOCATIONCHANGE): 근접 판정만 다시 하게 표시
        cursorMoved_ = true;
        return;
    }
    // 가장 싼 필터부터: 창 자체 이벤트, 최상위 창
    if (!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    if (event == EVENT_OBJECT_LOCATIONCHANGE) {
        if (cache_.contains(hwnd)) {
            rectDirty_.insert(hwnd);
            // 활성화 없는 SetWindowPos(HWND_TOP) 등은 Z-order 이벤트 없이 위치 이벤트만 온다 (시나리오 W6b에서 확인)
            // → 위치가 바뀐 창이 있으면 순서도 다시 읽되, 드래그 폭주를 막기 위해 간격을 둔다 (Flush 참고)
            orderStale_ = true;
            stats_.acceptedEvents++;
        }
        return;  // 캐시에 없는 창의 이동은 무시 (생성·표시 이벤트가 따로 온다)
    }
    if (event != EVENT_OBJECT_DESTROY && GetAncestor(hwnd, GA_ROOT) != hwnd) return;
    stats_.acceptedEvents++;
    if (event == EVENT_SYSTEM_MOVESIZESTART || event == EVENT_SYSTEM_MOVESIZEEND) {
        rectDirty_.insert(hwnd);
        return;
    }
    if (event == EVENT_SYSTEM_FOREGROUND) foregroundChanged_ = true;
    reevaluate_.insert(hwnd);
    zDirty_ = true;
}

bool WindowObserver::ReadBounds(HWND hwnd, core::RectI& out) const {
    RECT r{};
    // 그림자 제외 실제 프레임 (P0에서 GetWindowRect 와 8px 차이 확인). 비사각형 창은 사각형 근사 (P1-D1)
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))) && !GetWindowRect(hwnd, &r))
        return false;
    out = {r.left, r.top, r.right, r.bottom};
    return true;
}

std::string WindowObserver::ExeForPid(DWORD pid) {
    if (auto it = exeByPid_.find(pid); it != exeByPid_.end()) return it->second;
    std::string exe;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH];
        DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &len)) exe = LowerFileName(std::wstring(path, len));
        CloseHandle(h);
    }
    exeByPid_[pid] = exe;  // 실패(권한)도 캐시해 반복 시도하지 않는다
    return exe;
}

std::string WindowObserver::ExeOf(core::WindowId id) const {
    auto it = cache_.find(HwndOf(id));
    return it == cache_.end() ? std::string() : it->second.info.exe;
}

WindowObserver::Entry WindowObserver::Evaluate(HWND hwnd) {
    Entry e;
    e.info.id = IdOf(hwnd);
    auto reject = [&](const char* why) {
        e.valid = false;
        e.rejectReason = why;
        return e;
    };
    if (!IsWindow(hwnd)) return reject("destroyed");
    if (!IsWindowVisible(hwnd)) return reject("invisible");
    if (IsIconic(hwnd)) return reject("minimized");
    DWORD cloaked = 0;
    // 다른 가상 데스크탑의 창·일시 중단된 UWP 창도 cloak 로 표시된다고 가정 [검증]
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return reject("cloaked");
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ownPid_) return reject("own");
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    if (IsShellClass(cls)) return reject("shell");
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return reject("toolwindow");
    if ((ex & WS_EX_TRANSPARENT) && (ex & WS_EX_LAYERED)) return reject("overlay");  // 다른 앱의 클릭 통과 오버레이
    if (!ReadBounds(hwnd, e.info.bounds)) return reject("no bounds");
    if (e.info.bounds.Width() < kMinSurfaceWidth || e.info.bounds.Height() < kMinSurfaceHeight) return reject("small");
    e.info.exe = ExeForPid(pid);
    e.info.landable = std::find(excludedExe_.begin(), excludedExe_.end(), e.info.exe) == excludedExe_.end();
    e.valid = true;
    return e;
}

void WindowObserver::RefreshMonitors() {
    last_.monitors.clear();
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* out = reinterpret_cast<std::vector<core::MonitorInfo>*>(lp);
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(mon, &mi);
            UINT dx = 96, dy = 96;
            GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
            out->push_back({{mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom},
                            {mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom},
                            dx});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&last_.monitors));
    monitorsDirty_ = false;
}

void WindowObserver::RebuildOrder(bool reevaluateAll) {
    std::vector<HWND> all;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {  // 최상위 창을 Z-order 위 → 아래로 열거
        reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&all));

    std::unordered_map<HWND, Entry> next;
    next.reserve(all.size());
    order_.clear();
    for (HWND h : all) {
        auto it = cache_.find(h);
        Entry e;
        if (reevaluateAll || it == cache_.end() || reevaluate_.contains(h)) {
            e = Evaluate(h);
        } else {
            e = std::move(it->second);
            if (e.valid && rectDirty_.contains(h) && !ReadBounds(h, e.info.bounds)) e.valid = false;
        }
        if (e.valid) order_.push_back(h);
        next.emplace(h, std::move(e));
    }
    cache_.swap(next);  // 사라진 창은 자연히 제거
    reevaluate_.clear();
    rectDirty_.clear();
    zDirty_ = false;
    orderStale_ = false;
    lastOrderRebuildMs_ = GetTickCount64();
    stats_.zRebuilds++;
}

bool WindowObserver::Flush(core::WorldSnapshot& out) {
    if (!HasPending()) return false;
    const bool monitorsRefreshed = monitorsDirty_;
    if (monitorsDirty_) RefreshMonitors();
    if (fullResync_) {
        RebuildOrder(true);
        fullResync_ = false;
        stats_.fullResyncs++;
    } else if (zDirty_) {
        RebuildOrder(false);
    } else if (orderStale_ && GetTickCount64() - lastOrderRebuildMs_ >= kOrderRefreshMs) {
        RebuildOrder(false);  // 위치 변경과 함께 바뀌었을 수 있는 Z-order 확인 (최대 kOrderRefreshMs 간격)
    } else {
        // 위치만 바뀜: 순서 유지, 사각형만 다시 읽는다 (드래그 중 고빈도 경로)
        for (HWND h : rectDirty_) {
            auto it = cache_.find(h);
            if (it == cache_.end() || !it->second.valid) continue;
            if (!ReadBounds(h, it->second.info.bounds)) it->second.valid = false;
            stats_.rectUpdates++;
        }
        rectDirty_.clear();
    }

    std::vector<core::WindowInfo> windows;
    windows.reserve(order_.size());
    for (HWND h : order_) {
        const Entry& e = cache_[h];
        if (e.valid) windows.push_back(e.info);
    }
    const bool changed = windows.size() != last_.windows.size() ||
                         !std::equal(windows.begin(), windows.end(), last_.windows.begin(), [](auto& a, auto& b) {
                             return a.id == b.id && a.bounds == b.bounds && a.landable == b.landable;
                         });
    if (!changed && !monitorsRefreshed && stats_.snapshots > 0) return false;
    last_.windows = std::move(windows);
    last_.version++;
    stats_.snapshots++;
    out = last_;
    return true;
}

}  // namespace dp
