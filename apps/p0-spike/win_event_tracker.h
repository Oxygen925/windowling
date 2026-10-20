#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace dp {

// P0-07 단일 창 추종 spike.
// WINEVENT_OUTOFCONTEXT 훅은 설치한 스레드의 메시지 루프로 콜백이 전달된다 (UI 스레드에서 Start 호출).
class WinEventTracker {
public:
    using Callback = std::function<void(DWORD event, HWND hwnd)>;

    ~WinEventTracker() { Stop(); }
    bool Start(HWND target, Callback callback);
    void Stop();
    HWND Target() const { return target_; }
    bool Active() const { return target_ != nullptr; }

    uint64_t RawEvents() const { return raw_; }        // 훅이 전달한 이벤트
    uint64_t AcceptedEvents() const { return accepted_; }  // 필터 통과 이벤트

private:
    static void CALLBACK Proc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD thread,
                              DWORD time);
    std::vector<HWINEVENTHOOK> hooks_;
    HWND target_ = nullptr;
    Callback callback_;
    uint64_t raw_ = 0, accepted_ = 0;
};

}  // namespace dp
