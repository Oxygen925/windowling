#pragma once
#include <windows.h>

#include <cstdint>
#include <vector>

#include "platform/windows/sprite_sheet.h"

namespace dp {

// 창 안에 그려진 스프라이트 사각형(최근접 확대 기준)
struct SpritePlacement {
    int frame = 0;
    int x = 0, y = 0;   // 창 클라이언트 좌표
    int w = 0, h = 0;   // 표시 크기
    bool flipX = false; // 좌우 반전 표시 (왼쪽을 볼 때). Region·hit-test 도 같이 반전해야 한다
};

// 방식 A: 커서 위치가 불투명 픽셀 위인지 (창 클라이언트 좌표)
bool HitOpaque(const SpriteSheet& sheet, const SpritePlacement& p, int px, int py, int threshold);

// 방식 B: 불투명 픽셀 집합을 사각형 목록으로 누적 (여러 펫을 한 Region으로 합칠 수 있게)
void AppendOpaqueRects(const SpriteSheet& sheet, const SpritePlacement& p, int threshold, std::vector<RECT>& out);
// 3D(P5): BGRA 픽셀(행 간격 pitch)의 알파가 threshold 이상인 구간을 사각형 목록으로. 같은 행 구간이 이어지면 합친다
void AppendOpaqueRectsFromBgra(const uint8_t* bgra, int pitch, int width, int height, int threshold, std::vector<RECT>& out);
// 사각형 목록 → HRGN (호출자가 소유, SetWindowRgn에 넘기면 OS 소유)
HRGN CreateRegionFromRects(const std::vector<RECT>& rects);

}  // namespace dp
