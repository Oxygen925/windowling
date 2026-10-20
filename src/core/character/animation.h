#pragma once
#include <string>

#include "core/character/package.h"

namespace dp::core {

// 행동 이름 → 현재 프레임. 패키지에 없는 행동은 idle 로 대체 (기준선 §5.1)
class AnimationPlayer {
public:
    // 행동이 바뀌면 처음 프레임부터
    void Play(const std::string& action, double now);
    const std::string& Action() const { return action_; }
    // 실제로 재생 중인 클립 이름 (대체되었으면 idle)
    const std::string& ResolvedClip(const SpriteDef& sprite) const;
    int FrameAt(const SpriteDef& sprite, double now) const;
    // 다음 프레임이 바뀌는 시각 (프레임이 1개뿐이거나 비반복 클립이 끝났으면 무한대)
    double NextChangeAt(const SpriteDef& sprite, double now) const;

private:
    std::string action_ = "idle";
    double startedAt_ = 0;
};

}  // namespace dp::core
