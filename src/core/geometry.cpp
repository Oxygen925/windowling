#include "core/geometry.h"

namespace dp::core {

RectI Intersect(const RectI& a, const RectI& b) {
    RectI r{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    if (r.Empty()) return {};
    return r;
}

double CoverageRatio(const RectI& a, const RectI& b) {
    if (a.Empty()) return 0.0;
    const RectI i = Intersect(a, b);
    const double area = static_cast<double>(a.Width()) * a.Height();
    return i.Empty() ? 0.0 : static_cast<double>(i.Width()) * i.Height() / area;
}

}  // namespace dp::core
