#include "core/behavior/social.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dp::core {

namespace {
std::pair<int, int> Key(int a, int b) { return a < b ? std::pair{a, b} : std::pair{b, a}; }
bool Resting(BodyMode m) { return m == BodyMode::Grounded || m == BodyMode::Attached; }
}  // namespace

bool SameSurface(const SocialPet& a, const SocialPet& b) {
    if (!Resting(a.mode) || a.mode != b.mode) return false;
    if (a.mode == BodyMode::Attached) return a.host == b.host;
    return std::abs(a.foot.y - b.foot.y) < 2.0;  // 바닥: 같은 모니터 작업 영역 하단
}

bool GreetBook::Ready(int a, int b, double now, double cooldownSec) const {
    auto it = last_.find(Key(a, b));
    return it == last_.end() || now - it->second >= cooldownSec;
}

void GreetBook::Mark(int a, int b, double now) { last_[Key(a, b)] = now; }

void GreetBook::Forget(int id) {
    std::erase_if(last_, [id](const auto& kv) { return kv.first.first == id || kv.first.second == id; });
}

std::optional<std::pair<int, int>> FindGreeting(const std::vector<SocialPet>& pets, const GreetBook& book, double now,
                                                double cooldownSec, double distFactor) {
    std::optional<std::pair<int, int>> best;
    double bestGap = std::numeric_limits<double>::max();
    for (size_t i = 0; i < pets.size(); ++i) {
        const SocialPet& a = pets[i];
        if (!a.available || !a.greets) continue;
        for (size_t j = i + 1; j < pets.size(); ++j) {
            const SocialPet& b = pets[j];
            if (!b.available || !b.greets || !SameSurface(a, b)) continue;
            const double gap = std::abs(a.foot.x - b.foot.x);
            if (gap > (a.width + b.width) / 2.0 * distFactor) continue;
            if (!book.Ready(a.id, b.id, now, cooldownSec)) continue;
            if (gap < bestGap) {
                bestGap = gap;
                best = std::pair{a.id, b.id};
            }
        }
    }
    return best;
}

std::optional<std::pair<int, int>> FindBump(const std::vector<SocialPet>& pets, const GreetBook& book, double now,
                                            double cooldownSec, double distFactor) {
    for (size_t i = 0; i < pets.size(); ++i) {
        const SocialPet& a = pets[i];
        if (!a.available || !a.bumps) continue;
        for (size_t j = i + 1; j < pets.size(); ++j) {
            const SocialPet& b = pets[j];
            if (!b.available || !b.bumps || !SameSurface(a, b)) continue;
            const double dx = b.foot.x - a.foot.x;
            if (std::abs(dx) > (a.width + b.width) / 2.0 * distFactor) continue;
            const int toB = dx >= 0 ? 1 : -1;
            const bool approaching = a.walkDir == toB || b.walkDir == -toB;
            if (approaching && book.Ready(a.id, b.id, now, cooldownSec)) return std::pair{a.id, b.id};
        }
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> FindLean(const std::vector<SocialPet>& pets, const GreetBook& book, double now,
                                            double cooldownSec, double maxFactor) {
    std::optional<std::pair<int, int>> best;
    double bestGap = std::numeric_limits<double>::max();
    for (size_t i = 0; i < pets.size(); ++i) {
        const SocialPet& a = pets[i];
        if (!a.available || !a.leans || !a.sitting) continue;
        for (size_t j = i + 1; j < pets.size(); ++j) {
            const SocialPet& b = pets[j];
            if (!b.available || !b.leans || !b.sitting || !SameSurface(a, b)) continue;
            const double gap = std::abs(a.foot.x - b.foot.x);
            if (gap > (a.width + b.width) / 2.0 * maxFactor || !book.Ready(a.id, b.id, now, cooldownSec)) continue;
            if (gap < bestGap) {
                bestGap = gap;
                best = std::pair{a.id, b.id};
            }
        }
    }
    return best;
}

FollowStep DecideFollow(const SocialPet& me, const SocialPet& target, double catchFactor) {
    if (!Resting(target.mode)) return {FollowStep::Kind::Wait, 0};  // 공중·벽·끌림: 내려올 때까지
    const double dx = target.foot.x - me.foot.x;
    const int dir = dx >= 0 ? 1 : -1;
    if (SameSurface(me, target)) {
        if (std::abs(dx) <= (me.width + target.width) / 2.0 * catchFactor) return {FollowStep::Kind::Caught, dir};
        return {FollowStep::Kind::Walk, dir};
    }
    return {FollowStep::Kind::Jump, dir};
}

}  // namespace dp::core
