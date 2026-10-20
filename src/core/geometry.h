#pragma once
#include <algorithm>
#include <cstdint>

namespace dp::core {

// 가상 화면 물리 픽셀 좌표계 (음수 허용, 기준선 §4.1)
struct PointI {
    int32_t x = 0, y = 0;
    friend bool operator==(const PointI&, const PointI&) = default;
};

struct PointD {
    double x = 0, y = 0;
    friend bool operator==(const PointD&, const PointD&) = default;
};

// 반열림 사각형 [left, right) × [top, bottom)
struct RectI {
    int32_t left = 0, top = 0, right = 0, bottom = 0;

    int32_t Width() const { return right - left; }
    int32_t Height() const { return bottom - top; }
    bool Empty() const { return right <= left || bottom <= top; }
    bool Contains(PointI p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }
    friend bool operator==(const RectI&, const RectI&) = default;
};

RectI Intersect(const RectI& a, const RectI& b);
// a 의 면적 중 b 가 덮는 비율 (a 가 비면 0)
double CoverageRatio(const RectI& a, const RectI& b);

// 반열림 구간 [begin, end)
struct IntervalI {
    int32_t begin = 0, end = 0;
    int32_t Length() const { return std::max(0, end - begin); }
    bool Empty() const { return end <= begin; }
    friend bool operator==(const IntervalI&, const IntervalI&) = default;
};

}  // namespace dp::core
