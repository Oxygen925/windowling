#pragma once
// 펫 말풍선 창 (P3-07, P3-R8). 항상 클릭 통과: WS_EX_LAYERED + WS_EX_TRANSPARENT + NOREDIRECTIONBITMAP
// (P0 E1 방식 A 의 창 스타일. 토글하지 않으므로 A 의 경쟁 구간 문제가 없다). 텍스트가 바뀔 때만 Present.
#include <windows.h>

#include <memory>
#include <string>

#include "rendering/d3d11/bubble_renderer.h"
#include "rendering/d3d11/composition_target.h"
#include "rendering/d3d11/gpu_context.h"

namespace dp {

class SpeechBubble {
public:
    static void RegisterClass(HINSTANCE instance);
    ~SpeechBubble() { Destroy(); }
    void SetText(const std::wstring& text);
    void Hide();
    bool Active() const { return active_; }        // 표시 중이어야 하는 상태 (전체화면 숨김과 별개)
    bool WindowVisible() const { return shown_; }
    // 펫 창 사각형 기준으로 배치·그리기. batch 가 있으면 이동을 묶는다. 실패(Device Lost)면 HRESULT
    HRESULT Update(HINSTANCE instance, GpuContext& gpu, BubbleRenderer& renderer, const RECT& petRect, const RECT& work,
                   float dpiScale, HDWP& batch, double now);
    void SuspendWindow();  // 전체화면·사용자 숨김: 창만 숨기고 상태 유지
    void ReleaseGpu();
    void Destroy();
    HWND Hwnd() const { return hwnd_; }
    double hideAt = 0;  // >0 이면 이 시각에 Hide
    double redrawAt = -1;  // 갱신 묶기로 미룬 다시 그리기 시각 (호출자가 이 시각에 깨운다)
    static constexpr double kMinRedrawSec = 0.1;  // 스트리밍 중 최대 10fps (조사 R6)
    static uint64_t DrawCount();  // 진단: 전체 말풍선 그리기 횟수

private:
    HWND hwnd_ = nullptr;
    std::unique_ptr<CompositionTarget> target_;
    std::wstring text_;
    bool active_ = false, shown_ = false, dirty_ = false;
    RECT rect_{};
    SIZE size_{};
    float measuredScale_ = 0;
    double lastDrawAt_ = -1;
    bool deferred_ = false;
};

}  // namespace dp
