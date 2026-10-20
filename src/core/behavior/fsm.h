#pragma once
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>

#include "core/character/package.h"

namespace dp::core {

// 행동 결정 (FSM 기본값 또는 Lua 훅)
struct BehaviorDecision {
    std::string activity;  // idle / walk / sit / look
    double durationSec = 1.0;
    int walkDir = 0;       // walk 일 때 -1 / +1
};

struct BehaviorContext {
    double now = 0;
    bool attached = false;      // 창 위인가 (false 면 바닥)
    std::string last;           // 직전 행동
};

// Lua 등 스크립트 훅. nullopt 를 돌려주면 기본 FSM 이 결정한다 (기본 행동 상속, 기준선 §6.2)
class BehaviorHooks {
public:
    virtual ~BehaviorHooks() = default;
    virtual std::optional<BehaviorDecision> OnRest(const BehaviorContext& ctx) = 0;
    virtual std::optional<BehaviorDecision> OnEvent(std::string_view event, const BehaviorContext& ctx) = 0;
};

// 훅 결과 검증 (허용된 행동·시간·방향만, 기준선 §7.3 "검증 후 실행" 원칙)
bool IsValidDecision(const BehaviorDecision& d);

// 쉬는 동안의 행동 기계. 물리 상태(낙하·드래그)는 호출자가 resting 으로 알려 준다.
class BehaviorMachine {
public:
    BehaviorMachine(const BehaviorDef& def, uint32_t seed, BehaviorHooks* hooks = nullptr);

    // resting = Grounded/Attached. 결정 시각이 지났으면 다음 행동을 정한다
    void Update(double now, bool resting, bool attached);
    // 이벤트: click / landed / drag_start / drag_end / cursor_near
    void OnEvent(std::string_view event, double now, bool attached);
    // 외부 강제 (일시정지·개발 옵션)
    void Force(const BehaviorDecision& d, double now);

    const std::string& Activity() const { return activity_; }
    int WalkDir() const { return walkDir_; }
    double DecisionAt() const { return until_; }
    uint64_t HookDecisions() const { return hookDecisions_; }
    uint64_t RejectedHookDecisions() const { return rejected_; }

private:
    BehaviorDecision DefaultRest();
    void Apply(const BehaviorDecision& d, double now);

    BehaviorDef def_;
    BehaviorHooks* hooks_;
    std::mt19937 rng_;
    std::string activity_ = "idle";
    int walkDir_ = 0;
    double until_ = 0;
    uint64_t hookDecisions_ = 0, rejected_ = 0;
};

}  // namespace dp::core
