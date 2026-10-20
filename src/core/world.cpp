#include "core/world.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dp::core {

std::vector<IntervalI> SubtractIntervals(IntervalI a, std::vector<IntervalI> cuts) {
    std::vector<IntervalI> out;
    if (a.Empty()) return out;
    std::sort(cuts.begin(), cuts.end(), [](const IntervalI& l, const IntervalI& r) { return l.begin < r.begin; });
    int32_t cursor = a.begin;
    for (const IntervalI& c : cuts) {
        if (c.Empty() || c.end <= cursor) continue;
        if (c.begin >= a.end) break;
        if (c.begin > cursor) out.push_back({cursor, std::min(c.begin, a.end)});
        cursor = std::max(cursor, c.end);
        if (cursor >= a.end) break;
    }
    if (cursor < a.end) out.push_back({cursor, a.end});
    return out;
}

std::vector<IntervalI> TopExposure(const WorldSnapshot& world, size_t index) {
    const RectI& w = world.windows[index].bounds;
    std::vector<IntervalI> cuts;
    for (size_t i = 0; i < index; ++i) {  // 위에 있는 창들
        const RectI& o = world.windows[i].bounds;
        if (o.top <= w.top && w.top < o.bottom) cuts.push_back({o.left, o.right});
    }
    return SubtractIntervals({w.left, w.right}, std::move(cuts));
}

namespace {

// 모니터 작업 영역 중 x 범위가 겹치고 y 가 들어가는 영역 (창 상단 위 공간 판정용)
const MonitorInfo* MonitorContainingTop(const WorldSnapshot& world, int32_t x, int32_t y) {
    for (const auto& m : world.monitors)
        if (x >= m.bounds.left && x < m.bounds.right && y >= m.bounds.top && y < m.bounds.bottom) return &m;
    return nullptr;
}

}  // namespace

std::vector<Surface> BuildSurfaces(const WorldSnapshot& world, int32_t minHeadroom) {
    std::vector<Surface> out;
    for (size_t i = 0; i < world.windows.size(); ++i) {
        const WindowInfo& win = world.windows[i];
        if (!win.landable) continue;
        auto exposed = TopExposure(world, i);
        // 표면 위 공간: 노출 구간마다 그 지점의 모니터 작업 영역 상단까지 남은 높이 (P1-D6 최대화 창 규칙은 펫 키와 비교)
        std::vector<IntervalI> roomy;
        std::vector<int32_t> room;
        for (const IntervalI& iv : exposed) {
            const int32_t mid = iv.begin + iv.Length() / 2;
            const MonitorInfo* m = MonitorContainingTop(world, mid, win.bounds.top);
            if (!m) continue;  // 화면 밖 상단
            const int32_t headroom = win.bounds.top - m->work.top;
            if (headroom < std::max<int32_t>(1, minHeadroom)) continue;
            // 작업 영역 밖(작업 표시줄 아래 등)은 제외
            if (win.bounds.top > m->work.bottom) continue;
            const IntervalI clipped{std::max(iv.begin, m->work.left), std::min(iv.end, m->work.right)};
            if (clipped.Empty()) continue;
            roomy.push_back(clipped);
            room.push_back(headroom);
        }
        if (!roomy.empty()) out.push_back({win.id, win.bounds.top, std::move(roomy), std::move(room)});
    }
    for (const auto& m : world.monitors)
        out.push_back({kNoWindow, m.work.bottom, {{m.work.left, m.work.right}}, {m.work.bottom - m.work.top}});
    return out;
}

bool Supports(const std::vector<IntervalI>& exposed, double x0, double x1) {
    for (const IntervalI& iv : exposed)
        if (x0 >= iv.begin && x1 <= iv.end) return true;
    return false;
}

bool Supports(const Surface& s, double x0, double x1, int32_t height) {
    for (size_t i = 0; i < s.exposed.size(); ++i) {
        const IntervalI& iv = s.exposed[i];
        const int32_t room = i < s.headroom.size() ? s.headroom[i] : std::numeric_limits<int32_t>::max();
        if (x0 >= iv.begin && x1 <= iv.end && room >= height) return true;
    }
    return false;
}

int MonitorIndexFor(const WorldSnapshot& world, PointD p) {
    int best = -1;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < world.monitors.size(); ++i) {
        const RectI& b = world.monitors[i].bounds;
        const double dx = p.x < b.left ? b.left - p.x : (p.x >= b.right ? p.x - b.right + 1 : 0);
        const double dy = p.y < b.top ? b.top - p.y : (p.y > b.bottom ? p.y - b.bottom : 0);  // 바닥선(bottom) 포함
        const double d = std::hypot(dx, dy);
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

}  // namespace dp::core
