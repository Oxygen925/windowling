#include "rendering/hit_mask.h"

#include <algorithm>
#include <climits>
#include <cstring>

namespace dp {

bool HitOpaque(const SpriteSheet& sheet, const SpritePlacement& p, int px, int py, int threshold) {
    if (p.w <= 0 || p.h <= 0) return false;
    int lx = px - p.x, ly = py - p.y;
    if (lx < 0 || ly < 0 || lx >= p.w || ly >= p.h) return false;
    // 최근접 샘플링: 렌더러(point sampler)와 같은 매핑
    int sx = lx * sheet.FrameWidth() / p.w;
    int sy = ly * sheet.FrameHeight() / p.h;
    if (p.flipX) sx = sheet.FrameWidth() - 1 - sx;
    return sheet.AlphaAt(p.frame, sx, sy) >= threshold;
}

void AppendOpaqueRects(const SpriteSheet& sheet, const SpritePlacement& p, int threshold, std::vector<RECT>& out) {
    const int fw = sheet.FrameWidth(), fh = sheet.FrameHeight();
    if (p.w <= 0 || p.h <= 0 || fw <= 0 || fh <= 0) return;
    auto destStart = [](int s, int dest, int src) { return (s * dest + src - 1) / src; };  // ceil(s*dest/src)
    for (int sy = 0; sy < fh; ++sy) {
        int y0 = destStart(sy, p.h, fh), y1 = destStart(sy + 1, p.h, fh);
        if (y1 <= y0) continue;
        int sx = 0;
        while (sx < fw) {
            while (sx < fw && sheet.AlphaAt(p.frame, sx, sy) < threshold) ++sx;
            if (sx >= fw) break;
            int a = sx;
            while (sx < fw && sheet.AlphaAt(p.frame, sx, sy) >= threshold) ++sx;
            // 반전 표시면 소스 구간 [a, sx) 가 화면에서는 [fw - sx, fw - a) 에 놓인다
            const int ra = p.flipX ? fw - sx : a, rb = p.flipX ? fw - a : sx;
            int x0 = destStart(ra, p.w, fw), x1 = destStart(rb, p.w, fw);
            if (x1 > x0) out.push_back(RECT{p.x + x0, p.y + y0, p.x + x1, p.y + y1});
        }
    }
}

void AppendOpaqueRectsFromBgra(const uint8_t* bgra, int pitch, int w, int h, int threshold, std::vector<RECT>& out) {
    std::vector<std::pair<int, int>> prev, cur;  // 직전 행의 구간 (세로로 같은 구간이면 사각형을 늘린다)
    std::vector<size_t> prevIdx, curIdx;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = bgra + static_cast<size_t>(y) * pitch;
        cur.clear();
        curIdx.clear();
        int x = 0;
        while (x < w) {
            while (x < w && row[x * 4 + 3] < threshold) ++x;
            if (x >= w) break;
            const int a = x;
            while (x < w && row[x * 4 + 3] >= threshold) ++x;
            cur.emplace_back(a, x);
        }
        if (cur == prev) {
            for (size_t k = 0; k < cur.size(); ++k) out[prevIdx[k]].bottom = y + 1;
            curIdx = prevIdx;
        } else {
            for (const auto& [a, b] : cur) {
                curIdx.push_back(out.size());
                out.push_back(RECT{a, y, b, y + 1});
            }
        }
        prev.swap(cur);
        prevIdx.swap(curIdx);
    }
}

HRGN CreateRegionFromRects(const std::vector<RECT>& input) {
    if (input.empty()) return CreateRectRgn(0, 0, 0, 0);
    // 위→아래, 왼쪽→오른쪽 순으로 정렬해 전달 (반전 표시 시 역순으로 생성되므로)
    std::vector<RECT> rects = input;
    std::sort(rects.begin(), rects.end(), [](const RECT& a, const RECT& b) { return a.top != b.top ? a.top < b.top : a.left < b.left; });
    std::vector<BYTE> buf(sizeof(RGNDATAHEADER) + rects.size() * sizeof(RECT));
    auto* data = reinterpret_cast<RGNDATA*>(buf.data());
    data->rdh.dwSize = sizeof(RGNDATAHEADER);
    data->rdh.iType = RDH_RECTANGLES;
    data->rdh.nCount = static_cast<DWORD>(rects.size());
    data->rdh.nRgnSize = static_cast<DWORD>(rects.size() * sizeof(RECT));
    RECT bound{LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};
    for (const RECT& r : rects) {
        bound.left = std::min(bound.left, r.left);
        bound.top = std::min(bound.top, r.top);
        bound.right = std::max(bound.right, r.right);
        bound.bottom = std::max(bound.bottom, r.bottom);
    }
    data->rdh.rcBound = bound;
    std::memcpy(data->Buffer, rects.data(), rects.size() * sizeof(RECT));
    return ExtCreateRegion(nullptr, static_cast<DWORD>(buf.size()), data);
}

}  // namespace dp
