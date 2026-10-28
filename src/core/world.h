#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/geometry.h"

namespace dp::core {

// 플랫폼 창 핸들 대신 쓰는 불투명 ID (HWND ↔ WindowId 매핑은 dp_platform_win 만 가진다)
using WindowId = uint64_t;
constexpr WindowId kNoWindow = 0;

struct WindowInfo {
    WindowId id = kNoWindow;
    RectI bounds;          // DWMWA_EXTENDED_FRAME_BOUNDS (비사각형 창은 사각형 근사, P1-D1)
    std::string exe;       // 소문자 실행 파일 이름
    bool landable = true;  // 사용자 제외 목록 등으로 착지 대상에서 빠진 창은 false (가림 계산에는 계속 사용)
};

struct MonitorInfo {
    RectI bounds;
    RectI work;            // 작업 표시줄 제외 영역 (바닥 = work.bottom)
    uint32_t dpi = 96;
};

// 한 시점의 데스크탑 상태. windows 는 Z-order 위 → 아래.
struct WorldSnapshot {
    std::vector<WindowInfo> windows;
    std::vector<MonitorInfo> monitors;
    uint64_t version = 0;  // 갱신마다 증가
};

// 착지 가능한 수평 표면 (창 상단 또는 모니터 바닥)
struct Surface {
    WindowId owner = kNoWindow;     // kNoWindow 면 모니터 바닥
    int32_t y = 0;                  // 발이 닿는 높이
    std::vector<IntervalI> exposed; // 노출 구간 (가려진 부분 제외, 1D 구간 빼기)
    std::vector<int32_t> headroom;  // exposed[i] 위로 작업 영역 상단까지 남은 높이 (펫 키와 비교 — 크기가 다른 펫 혼합)
};

// a 에서 cuts 를 뺀 구간들 (정렬·병합된 결과)
std::vector<IntervalI> SubtractIntervals(IntervalI a, std::vector<IntervalI> cuts);

// 창 index 의 상단 노출 구간: Z-order상 위에 있는 창 중 상단 선을 세로로 덮는 창의 가로 범위를 뺀다
std::vector<IntervalI> TopExposure(const WorldSnapshot& world, size_t index);

// 창 index 의 세로선 x 중 ys 구간의 노출 부분: Z-order상 위에 있는 창 중 x 를 가로로 덮는 창의 세로 범위를 뺀다 (벽 타기)
std::vector<IntervalI> SideExposure(const WorldSnapshot& world, size_t index, int32_t x, IntervalI ys);

// 착지 가능한 표면 목록 (P1-D1, P1-D6)
//  - 창 상단: 노출 구간마다 그 위 작업 영역까지의 여유 높이(headroom)를 기록한다. 펫 키보다 작으면 그 펫은 착지 불가
//    (P2 보완: 펫마다 키가 다를 수 있어 표면을 한 키로 거르지 않는다). minHeadroom 보다 작은 구간은 아예 뺀다
//  - 모니터 바닥: 작업 영역 하단 (headroom = 작업 영역 높이)
std::vector<Surface> BuildSurfaces(const WorldSnapshot& world, int32_t minHeadroom = 1);

// 구간 목록 중 [x0, x1) 을 완전히 포함하는 구간이 있는가 (발 지지 폭 판정)
bool Supports(const std::vector<IntervalI>& exposed, double x0, double x1);
// 위 + 그 구간의 여유 높이가 height 이상인가 (펫 키별 착지 판정)
bool Supports(const Surface& s, double x0, double x1, int32_t height);

// 점이 속한 모니터 (없으면 가장 가까운 모니터 index, 모니터가 없으면 -1)
int MonitorIndexFor(const WorldSnapshot& world, PointD p);

}  // namespace dp::core
