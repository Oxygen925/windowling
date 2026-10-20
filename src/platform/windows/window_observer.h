#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/world.h"

namespace dp {

// P1-02: 시스템 전체 최상위 창 감시 (P1 계획 §3.1)
// - WinEvent 콜백은 "무엇이 바뀌었나"만 표시하고 즉시 반환한다 (병합)
// - 메시지 펌프 1회가 끝난 뒤 Flush() 가 한 번만 창 정보를 다시 읽는다
// - LOCATIONCHANGE 는 해당 창 사각형만 갱신, Z-order 영향 이벤트는 EnumWindows 순서 재구성
// WINEVENT_OUTOFCONTEXT 훅이므로 Start() 를 호출한 스레드가 메시지 루프를 돌려야 한다.
class WindowObserver {
public:
    struct Stats {
        uint64_t rawEvents = 0;       // 훅이 전달한 이벤트
        uint64_t acceptedEvents = 0;  // 최상위 창 필터 통과
        uint64_t zRebuilds = 0;       // EnumWindows 순서 재구성
        uint64_t rectUpdates = 0;     // 사각형만 갱신
        uint64_t fullResyncs = 0;     // 전체 재평가
        uint64_t snapshots = 0;       // 실제로 바뀐 스냅샷 발행
    };

    ~WindowObserver() { Stop(); }
    bool Start();
    void Stop();

    void SetExcludedSurfaceExe(std::vector<std::string> lowerExeNames);
    // WM_DISPLAYCHANGE / WM_DPICHANGED / SPI_SETWORKAREA 수신 시
    void MarkMonitorsDirty() { monitorsDirty_ = true; zDirty_ = true; }
    // 주기적 재동기화 (P1-D6: 30초) 및 세션 복귀 직후
    void RequestFullResync() { fullResync_ = true; }

    // 변경이 있었으면 true 와 새 스냅샷
    bool Flush(core::WorldSnapshot& out);
    bool HasPending() const { return zDirty_ || fullResync_ || monitorsDirty_ || !rectDirty_.empty() || orderStale_; }
    // 위치 변경 후 Z-order 재확인 간격 (드래그 폭주 시 EnumWindows 빈도 제한)
    static constexpr uint64_t kOrderRefreshMs = 100;
    // 위치 변경 뒤 아직 Z-order 를 다시 읽지 않았다 (호출자는 kOrderRefreshMs 뒤 Flush 를 다시 부른다)
    bool OrderStale() const { return orderStale_; }

    // 전경 창이 바뀌었는가 (읽으면 초기화) — 전체화면 판정 계기
    bool TakeForegroundChanged() {
        const bool v = foregroundChanged_;
        foregroundChanged_ = false;
        return v;
    }
    // 커서가 움직였는가 (읽으면 초기화) — 커서 근접 판정 계기 (A4: 200ms 폴링 대신)
    bool TakeCursorMoved() {
        const bool v = cursorMoved_;
        cursorMoved_ = false;
        return v;
    }
    const Stats& GetStats() const { return stats_; }
    static core::WindowId IdOf(HWND hwnd) { return static_cast<core::WindowId>(reinterpret_cast<uintptr_t>(hwnd)); }
    static HWND HwndOf(core::WindowId id) { return reinterpret_cast<HWND>(static_cast<uintptr_t>(id)); }
    // 캐시된 창의 실행 파일 이름 (없으면 빈 문자열)
    std::string ExeOf(core::WindowId id) const;

private:
    struct Entry {
        bool valid = false;  // 표면·가림 계산 대상인가
        core::WindowInfo info;
        std::string rejectReason;
    };

    static void CALLBACK Proc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    void OnEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild);
    Entry Evaluate(HWND hwnd);
    bool ReadBounds(HWND hwnd, core::RectI& out) const;
    std::string ExeForPid(DWORD pid);
    void RefreshMonitors();
    void RebuildOrder(bool reevaluateAll);

    std::vector<HWINEVENTHOOK> hooks_;
    DWORD ownPid_ = 0;
    std::vector<std::string> excludedExe_;

    std::unordered_map<HWND, Entry> cache_;
    std::vector<HWND> order_;  // Z-order 위 → 아래 (유효 창만)
    std::unordered_set<HWND> rectDirty_;
    std::unordered_set<HWND> reevaluate_;
    std::unordered_map<DWORD, std::string> exeByPid_;
    bool zDirty_ = true, fullResync_ = true, monitorsDirty_ = true, foregroundChanged_ = true;
    bool cursorMoved_ = false;
    bool orderStale_ = false;
    uint64_t lastOrderRebuildMs_ = 0;

    core::WorldSnapshot last_;
    Stats stats_;
};

}  // namespace dp
