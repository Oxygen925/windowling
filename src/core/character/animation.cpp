#include "core/character/animation.h"

#include <cmath>
#include <limits>

namespace dp::core {

namespace {
const std::string kIdle = "idle";
}

void AnimationPlayer::Play(const std::string& action, double now) {
    if (action == action_) return;
    action_ = action;
    startedAt_ = now;
}

const std::string& AnimationPlayer::ResolvedClip(const SpriteDef& sprite) const {
    return sprite.clips.contains(action_) ? action_ : kIdle;
}

int AnimationPlayer::FrameAt(const SpriteDef& sprite, double now) const {
    auto it = sprite.clips.find(ResolvedClip(sprite));
    if (it == sprite.clips.end() || it->second.frames.empty()) return 0;
    const AnimationClip& c = it->second;
    const auto n = static_cast<long long>(c.frames.size());
    long long i = static_cast<long long>(std::floor(std::max(0.0, now - startedAt_) * c.fps));
    i = c.loop ? i % n : std::min(i, n - 1);
    return c.frames[static_cast<size_t>(i)];
}

double AnimationPlayer::NextChangeAt(const SpriteDef& sprite, double now) const {
    constexpr double kNever = std::numeric_limits<double>::infinity();
    auto it = sprite.clips.find(ResolvedClip(sprite));
    if (it == sprite.clips.end() || it->second.frames.size() <= 1) return kNever;
    const AnimationClip& c = it->second;
    const double t = std::max(0.0, now - startedAt_);
    const double idx = std::floor(t * c.fps);
    if (!c.loop && idx >= static_cast<double>(c.frames.size()) - 1) return kNever;
    return startedAt_ + (idx + 1) / c.fps;
}

}  // namespace dp::core
