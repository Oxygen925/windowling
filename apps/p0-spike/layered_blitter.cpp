#include "layered_blitter.h"

#include <cstring>

#include "diagnostics/metrics.h"

namespace dp {

bool LayeredBlitter::Resize(int w, int h) {
    if (w == w_ && h == h_ && bitmap_) return true;
    Release();
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    dc_ = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap_) return false;
    bits_ = static_cast<uint32_t*>(bits);
    old_ = SelectObject(dc_, bitmap_);
    w_ = w;
    h_ = h;
    return true;
}

void LayeredBlitter::Clear() {
    if (bits_) std::memset(bits_, 0, static_cast<size_t>(w_) * h_ * 4);
}

void LayeredBlitter::Blit(const SpriteSheet& sheet, const SpritePlacement& p) {
    if (!bits_ || p.w <= 0 || p.h <= 0) return;
    const int fw = sheet.FrameWidth(), fh = sheet.FrameHeight(), sheetW = sheet.Width();
    const uint32_t* src = sheet.Pixels();
    for (int y = 0; y < p.h; ++y) {
        int dy = p.y + y;
        if (dy < 0 || dy >= h_) continue;
        const uint32_t* row = src + static_cast<size_t>(y * fh / p.h) * sheetW + p.frame * fw;
        uint32_t* dst = bits_ + static_cast<size_t>(dy) * w_;
        for (int x = 0; x < p.w; ++x) {
            int dx = p.x + x;
            if (dx < 0 || dx >= w_) continue;
            dst[dx] = row[x * fw / p.w];
        }
    }
}

bool LayeredBlitter::Present(HWND hwnd, POINT screenPos) {
    SIZE size{w_, h_};
    POINT srcPos{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    BOOL ok = UpdateLayeredWindow(hwnd, nullptr, &screenPos, &size, dc_, &srcPos, 0, &blend, ULW_ALPHA);
    if (ok) GlobalCounters().presents++;
    return ok != FALSE;
}

void LayeredBlitter::Release() {
    if (dc_) {
        if (old_) SelectObject(dc_, old_);
        DeleteDC(dc_);
    }
    if (bitmap_) DeleteObject(bitmap_);
    dc_ = nullptr;
    bitmap_ = nullptr;
    old_ = nullptr;
    bits_ = nullptr;
    w_ = h_ = 0;
}

}  // namespace dp
