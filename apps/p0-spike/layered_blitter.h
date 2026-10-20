#pragma once
#include <windows.h>

#include "rendering/hit_mask.h"
#include "platform/windows/sprite_sheet.h"

namespace dp {

// 방식 C: UpdateLayeredWindow + 32bpp premultiplied DIB. GPU 경로를 쓰지 않는다.
// OS가 알파 기반으로 hit-test 하므로 별도 마스크가 필요 없다.
class LayeredBlitter {
public:
    ~LayeredBlitter() { Release(); }
    bool Resize(int w, int h);
    void Clear();
    void Blit(const SpriteSheet& sheet, const SpritePlacement& p);
    // hwnd를 screenPos(좌상단)로 이동하며 갱신
    bool Present(HWND hwnd, POINT screenPos);
    void Release();

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    uint32_t* bits_ = nullptr;
    int w_ = 0, h_ = 0;
};

}  // namespace dp
