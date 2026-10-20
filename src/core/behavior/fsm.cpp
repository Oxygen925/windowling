#include "core/behavior/fsm.h"

#include <array>

namespace dp::core {

bool IsValidDecision(const BehaviorDecision& d) {
    static const std::array<std::string_view, 4> allowed = {"idle", "walk", "sit", "look"};
    bool known = false;
    for (auto a : allowed) known |= d.activity == a;
    if (!known) return false;
    if (!(d.durationSec >= 0.1 && d.durationSec <= 60.0)) return false;  // NaN 도 거부
    if (d.activity == "walk") return d.walkDir == -1 || d.walkDir == 1;
    return d.walkDir == 0;
}

BehaviorMachine::BehaviorMachine(const BehaviorDef& def, uint32_t seed, BehaviorHooks* hooks)
    : def_(def), hooks_(hooks), rng_(seed) {}

BehaviorDecision BehaviorMachine::DefaultRest() {
    double total = 0;
    for (const auto& [_, a] : def_.activities) total += a.weight;
    std::uniform_real_distribution<double> u(0.0, 1.0);
    BehaviorDecision d{"idle", 3.0, 0};
    if (total <= 0) return d;
    double pick = u(rng_) * total;
    for (const auto& [name, a] : def_.activities) {
        if (a.weight <= 0) continue;
        d.activity = name;
        d.durationSec = a.minSec + (a.maxSec - a.minSec) * u(rng_);
        if (pick < a.weight) break;
        pick -= a.weight;
    }
    d.walkDir = d.activity == "walk" ? (u(rng_) < 0.5 ? -1 : 1) : 0;
    return d;
}

void BehaviorMachine::Apply(const BehaviorDecision& d, double now) {
    activity_ = d.activity;
    walkDir_ = d.walkDir;
    until_ = now + d.durationSec;
}

void BehaviorMachine::Force(const BehaviorDecision& d, double now) { Apply(d, now); }

void BehaviorMachine::Update(double now, bool resting, bool attached) {
    if (!resting) {  // 낙하·드래그 중에는 행동 없음 (애니메이션은 물리 상태가 정한다)
        activity_ = "idle";
        walkDir_ = 0;
        until_ = now;  // 착지하면 바로 다시 결정
        return;
    }
    if (now < until_) return;
    BehaviorContext ctx{now, attached, activity_};
    if (hooks_) {
        if (auto d = hooks_->OnRest(ctx)) {
            if (IsValidDecision(*d)) {
                hookDecisions_++;
                Apply(*d, now);
                return;
            }
            rejected_++;
        }
    }
    Apply(DefaultRest(), now);
}

void BehaviorMachine::OnEvent(std::string_view event, double now, bool attached) {
    BehaviorContext ctx{now, attached, activity_};
    if (hooks_) {
        if (auto d = hooks_->OnEvent(event, ctx)) {
            if (IsValidDecision(*d)) {
                hookDecisions_++;
                Apply(*d, now);
                return;
            }
            rejected_++;
        }
    }
    // 기본 반응
    if (event == "click") Apply({"look", 1.0, 0}, now);
    else if (event == "landed") Apply({"idle", 1.0, 0}, now);  // 착지 직후 잠깐 가만히
}

}  // namespace dp::core
