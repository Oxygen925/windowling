#include "win_event_tracker.h"

#include "diagnostics/log.h"

namespace dp {

namespace {
WinEventTracker* g_active = nullptr;  // 훅 콜백은 컨텍스트 인자가 없으므로 단일 인스턴스로 라우팅
}

bool WinEventTracker::Start(HWND target, Callback callback) {
    Stop();
    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    if (!pid) return false;
    target_ = target;
    callback_ = std::move(callback);
    g_active = this;

    // 대상 프로세스로 범위를 좁혀 훅 단계에서 이벤트 양을 줄인다.
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    const std::pair<DWORD, DWORD> ranges[] = {
        {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},  // DESTROY, SHOW, HIDE
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
    };
    for (auto [lo, hi] : ranges) {
        HWINEVENTHOOK h = SetWinEventHook(lo, hi, nullptr, &WinEventTracker::Proc, pid, 0, flags);
        if (h) hooks_.push_back(h);
        else log::Warn("SetWinEventHook(0x{:X}-0x{:X}) 실패 err={}", lo, hi, GetLastError());
    }
    log::Info("WinEvent 추종 시작 hwnd=0x{:X} pid={} hooks={}", reinterpret_cast<uintptr_t>(target), pid, hooks_.size());
    return !hooks_.empty();
}

void WinEventTracker::Stop() {
    for (HWINEVENTHOOK h : hooks_) UnhookWinEvent(h);
    hooks_.clear();
    if (target_) log::Info("WinEvent 추종 종료 raw={} accepted={}", raw_, accepted_);
    target_ = nullptr;
    callback_ = nullptr;
    if (g_active == this) g_active = nullptr;
}

void CALLBACK WinEventTracker::Proc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    WinEventTracker* self = g_active;
    if (!self) return;
    self->raw_++;
    // 캐럿·커서·자식 객체 이벤트 제외: 창 자체(OBJID_WINDOW, CHILDID_SELF)만
    if (hwnd != self->target_ || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    self->accepted_++;
    if (self->callback_) self->callback_(event, hwnd);
}

}  // namespace dp
