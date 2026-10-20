#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dp {

// 가로 배열 스프라이트 시트. WIC로 디코딩해 premultiplied BGRA(32bppPBGRA)로 보관한다.
// CPU 사본은 hit-test 마스크(A/B)와 UpdateLayeredWindow 경로(C)에서 공유한다.
class SpriteSheet {
public:
    bool Load(const std::wstring& path, int frameWidth);

    int Width() const { return width_; }
    int Height() const { return height_; }
    int FrameWidth() const { return frameWidth_; }
    int FrameHeight() const { return height_; }
    int FrameCount() const { return frameWidth_ ? width_ / frameWidth_ : 0; }
    const uint32_t* Pixels() const { return pixels_.data(); }

    // 프레임 내부 좌표의 알파 (범위 밖이면 0)
    uint8_t AlphaAt(int frame, int x, int y) const;

private:
    int width_ = 0, height_ = 0, frameWidth_ = 0;
    std::vector<uint32_t> pixels_;
};

}  // namespace dp
