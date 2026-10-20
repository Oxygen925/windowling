#pragma once
#include <windows.h>

#include <d2d1_1.h>

#include <memory>
#include <string>
#include <vector>

#include "options.h"
#include "csv_logger.h"
#include "diagnostics/metrics.h"
#include "rendering/hit_mask.h"
#include "layered_blitter.h"
#include "platform/windows/sprite_sheet.h"
#include "platform/windows/tray_icon.h"
#include "win_event_tracker.h"
#include "rendering/d3d11/composition_target.h"
#include "cube_renderer.h"
#include "rendering/d3d11/gpu_context.h"
#include "rendering/d3d11/sprite_renderer.h"
#include "rendering/d3d11/text_renderer.h"

namespace dp {

enum class PetState { Idle, Look, Drag, Jump };

struct Pet {
    int id = 0;
    double footX = 0, footY = 0;  // 발 위치(스프라이트 하단 중앙), 가상 화면 물리 픽셀
    double vx = 0;                // 이동 속도 px/s
    int animTick = 0;
    PetState state = PetState::Idle;
    bool facingLeft = false;
    UINT dpi = 96;
    int size = 128;
    double rangeLeft = 0, rangeRight = 0;  // 이동 범위 (rangeRight > rangeLeft 일 때만 적용, --anchor=top-left)

    // small overlay 전용
    HWND hwnd = nullptr;
    std::unique_ptr<CompositionTarget> target;
    LayeredBlitter blitter;           // 방식 C
    bool transparent = true;          // 방식 A: 현재 WS_EX_TRANSPARENT 여부
    RECT lastWindow{};                // 마지막 SetWindowPos 위치
    std::vector<RECT> lastRegion;     // 방식 B: 마지막 Region (변경 시에만 갱신)

    bool contentDirty = true;  // 다시 그려야 함
    bool regionDirty = true;   // 방식 B: 이번 루프에서 콘텐츠가 바뀌어 Region 재계산 필요
    bool posDirty = true;      // 창 위치만 바뀜
    double jumpStart = 0;

    bool pressed = false, dragging = false;
    POINT pressPt{};
    double grabDx = 0, grabDy = 0;

    bool attached = false;  // P0-07 창 추종
    int attachLocalX = 0;

    LONGLONG pendingInputQpc = 0;  // 입력 → Present 지연 측정
    LONGLONG pendingFollowQpc = 0; // WinEvent → Present 지연 측정
};

struct PetLayout {
    RECT window{};          // 화면 좌표
    SpritePlacement sprite; // 창 기준
    RECT cube{};            // 창 기준 (cube 옵션)
    D2D1_RECT_F label{};    // 창 기준 (text 옵션)
};

class App {
public:
    int Run(HINSTANCE instance, const Options& options);

private:
    // 초기화
    bool InitGpu();
    bool CreateAppWindow();
    bool CreatePets();
    bool CreatePetWindow(Pet& pet);
    bool CreateOverlayWindow();
    void DestroyWindows();

    // 루프
    double Now() const;
    void ArmWait(double animDeadline, double otherDeadline, double now, DWORD& timeoutMs, HANDLE* handles,
                 DWORD& handleCount);
    bool suspended() const { return hidden_ || locked_; }
    void SetSuspended(bool hidden, bool locked, const char* reason);
    void Tick(double now, double dt);
    void PollCursor(double now);
    void Render();
    void RenderPass();
    HRESULT RenderPetSmall(Pet& pet);
    HRESULT RenderOverlay();
    void ApplyWindowPositions();
    void UpdateRegions();
    void SampleMetrics(double now);
    bool NeedsCursorPoll() const;
    bool Animating() const;

    // 레이아웃/좌표
    PetLayout ComputeLayout(const Pet& pet) const;
    void UpdatePetDpi(Pet& pet, UINT dpi, const char* reason);
    RECT WorkAreaFor(const Pet& pet) const;
    Pet* PetFromWindowPoint(HWND hwnd, POINT screenPt);
    float FontPx(const Pet& pet) const;

    // 입력/이벤트
    LRESULT PetWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT AppWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK StaticPetWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK StaticAppWndProc(HWND, UINT, WPARAM, LPARAM);
    void ShowMenu(HWND owner);
    void SetTransparent(HWND hwnd, bool& state, bool transparent);
    void MarkContentDirty(Pet& pet);
    void OnMouseDown(Pet& pet, POINT screenPt);
    void OnMouseMove(Pet& pet, POINT screenPt);
    void OnMouseUp(Pet& pet, POINT screenPt);

    // P0-07
    void BeginFollowPick(int delaySec);
    void StartFollow();
    void OnWinEvent(DWORD event, HWND hwnd);
    void SnapToTarget(Pet& pet);
    void DropToWorkArea(Pet& pet);

    // P0-12
    void RecreateGpu(const char* reason);

    void CaptureIfRequested(double now);
    void LogSummary();

    Options opt_;
    HINSTANCE instance_ = nullptr;
    HWND appWnd_ = nullptr;
    HWND overlayWnd_ = nullptr;  // single overlay
    std::unique_ptr<CompositionTarget> overlayTarget_;
    RECT overlayRect_{};
    bool overlayTransparent_ = true;
    std::vector<RECT> overlayRegion_;

    GpuContext gpu_;
    SpriteSheet sheet_;
    SpriteRenderer sprite_;
    CubeRenderer cube_;
    TextRenderer text_;
    TrayIcon tray_;
    WinEventTracker tracker_;
    MetricsSampler sampler_;
    CsvLogger csv_;

    std::vector<std::unique_ptr<Pet>> pets_;
    bool quit_ = false;
    bool overlayDirty_ = true;

    LARGE_INTEGER qpcFreq_{};
    LARGE_INTEGER qpcStart_{};
    HANDLE hiResTimer_ = nullptr;
    double nextAnimTick_ = 0, nextPoll_ = 0, nextSample_ = 0, endTime_ = 0, warmupEnd_ = 0;
    double followPickAt_ = -1;
    double captureAt_ = -1;
    double recreateAt_ = -1;
    double lastTick_ = 0;
    bool loggingActive_ = false;
    bool hidden_ = false;        // 트레이 "숨기기"
    bool locked_ = false;        // 세션 잠금 (WTS)
    double lastPresentPass_ = 0; // 애니메이션 중 직전 Present 시각 (실제 간격 측정)

    // 통계 (종료 시 요약 로그)
    uint64_t clicksOpaque_ = 0, clicksTransparent_ = 0, toggles_ = 0, regionUpdates_ = 0;
    uint64_t gpuRecreates_ = 0, windowMoves_ = 0, ticks_ = 0;
    std::vector<double> followLatency_;
};

}  // namespace dp
