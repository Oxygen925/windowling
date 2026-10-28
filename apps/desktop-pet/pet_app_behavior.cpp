// 행동 (P2: 패키지 FSM + Lua 훅): 결정 갱신, 점프·벽 타기 시작, 행동 → 애니메이션
#include "pet_app.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "diagnostics/log.h"

namespace dp {

void PetApp::UpdateBehavior(double now) {
    double next = kNoDeadline;
    if (nextBugAt_ < 0) nextBugAt_ = now + 150.0;  // 처음 버그는 시작하고 조금 뒤
    if (now >= nextBugAt_) {
        // 쉬는 사냥꾼이 없어 못 나왔으면 조금 뒤 다시
        nextBugAt_ = now + (SpawnBug(now) ? std::uniform_real_distribution<double>(180.0, 360.0)(bugRng_) : 10.0);
    }
    if (const double bugAt = NextBugCheck(now); bugAt < kNoDeadline) next = std::min(next, bugAt);
    for (auto& p : pets_) {
        auto& b = p->body;
        const bool resting = b.mode == core::BodyMode::Grounded || b.mode == core::BodyMode::Attached;
        if (p->hiddenFullscreen) continue;
        if (p->isBug) {
            UpdateBug(*p, now);
            next = std::min(next, std::min(p->bugUntil, std::max(now + 0.05, p->fsm->DecisionAt())));
            continue;
        }
        // 잡기 놀이 되잡기: 잡힌 쪽의 인사가 끝나면 따라가기로 (FSM 이 다음 행동을 고르기 전에)
        if (p->followTarget >= 0 && p->fsm->Activity() == "greet" && p->fsm->DecisionAt() <= now)
            p->fsm->Force({"follow", std::max(1.0, p->followUntil - now), 0}, now);
        if (paused_ || options_.noBehavior) {
            // 자율 결정만 막는다. 클릭 등 이벤트 반응(짧은 sit/look)은 끝날 때까지 두되 이동(walk)은 막는다
            if (p->fsm->Activity() == "walk" || p->fsm->DecisionAt() <= now) p->fsm->Force({"idle", 3600.0, 0}, now);
        } else if (options_.forceWalk && resting) {
            if (p->fsm->Activity() != "walk" || p->fsm->DecisionAt() <= now) p->fsm->Force({"walk", 3600.0, 1}, now);
        } else if (!options_.forceActivity.empty() && resting) {
            if (p->fsm->Activity() != options_.forceActivity || p->fsm->DecisionAt() <= now)
                p->fsm->Force({options_.forceActivity, 2.0, options_.forceActivity == "jump" ? (p->facingLeft ? -1 : 1) : 0}, now);
        } else {
            p->fsm->Update(now, resting, b.mode == core::BodyMode::Attached);
        }
        const bool newDecision = p->fsm->DecisionAt() != p->appliedDecisionAt;
        // 점프·벽 타기: 새 결정일 때 한 번만 물리에 맡긴다 (안 되면 잠깐 가만히)
        if (resting && newDecision && !p->speaking && (p->fsm->Activity() == "jump" || p->fsm->Activity() == "climb"))
            StartJumpOrClimb(*p, now);
        if (resting && newDecision && !p->speaking && p->fsm->Activity() == "follow" && p->followTarget < 0) StartFollow(*p, now);
        if (resting && newDecision && !p->speaking && p->fsm->Activity() == "train") StartTrain(*p, now);
        if (resting && newDecision && !p->speaking && p->fsm->Activity() == "visit") StartVisit(*p, now);
        if (resting && newDecision && !p->speaking && p->fsm->Activity() == "race") StartRace(*p, now);
        if (resting && newDecision && !p->speaking && p->fsm->Activity() == "stack") StartStack(*p, now);
        // 올라탄 펫: 시간이 되면 뛰어내림. 머리에 누가 있는 펫은 가만히 (carry)
        if (p->mountId >= 0 && b.mode == core::BodyMode::Riding && now >= p->rideUntil) HopOff(*p, now);
        if (p->riderId >= 0 && resting && p->fsm->Activity() != "carry") p->fsm->Force({"carry", 2.0, 0}, now);
        if (p->fsm->Activity() != "follow" && p->fsm->Activity() != "greet") p->followTarget = -1;
        if (b.climb && (p->fsm->Activity() != "climb" || p->speaking) && b.mode != core::BodyMode::Climbing) {
            b.climb.reset();  // 벽으로 가던 중 다른 행동으로 바뀜
            b.walkDir = 0;
        }
        const bool walking = resting && p->fsm->Activity() == "walk";
        if (b.climb || (resting && p->fsm->Activity() == "follow")) {
            // 벽까지·따라가는 방향은 물리·UpdateSocial 이 정한다
        } else if (!walking) {
            b.walkDir = 0;
        } else if (b.walkDir == 0 || newDecision) {
            b.walkDir = p->fsm->WalkDir();
        }
        p->appliedDecisionAt = p->fsm->DecisionAt();
        if (p->speaking) b.walkDir = 0;  // 말하는 동안은 멈춘다 (P3)
        if (b.walkDir != 0) p->facingLeft = b.walkDir < 0;
        if (b.mode == core::BodyMode::Climbing && b.climb) p->facingLeft = b.climb->side < 0;  // 그림은 벽이 오른쪽
        if (p->fsm->Activity() != p->loggedActivity && resting) {
            log::Info("[pet{}] act={} dir={} for={:.1f}s hooks={} rejected={}", p->id, p->fsm->Activity(), p->fsm->WalkDir(),
                      p->fsm->DecisionAt() - now, p->fsm->HookDecisions(), p->fsm->RejectedHookDecisions());
            p->loggedActivity = p->fsm->Activity();
        }
        if (resting) next = std::min(next, p->fsm->DecisionAt());
        if (p->mountId >= 0) next = std::min(next, p->rideUntil);  // 올라탄 동안은 resting 이 아니라 따로 깨운다
        LogLua(*p);
    }
    UpdateSocial(now);
    SweepBugs();
    if (next < kNoDeadline) deadlines_.Set(kDlBehavior, std::max(next, now + 0.001));
    else deadlines_.Clear(kDlBehavior);
}

// Lua 런타임 오류(명령 수·메모리 제한 포함)와 비활성화를 기록한다
void PetApp::LogLua(PetActor& p) {
    if (!p.lua) return;
    if (p.lua->Errors() != p.loggedLuaErrors) {
        log::Warn("[pet{}] lua error count={} last={}", p.id, p.lua->Errors(), p.lua->LastError());
        p.loggedLuaErrors = p.lua->Errors();
    }
    if (p.lua->Disabled() && !p.loggedLuaDisabled) {
        log::Warn("[pet{}] lua disabled errors={} → 기본 FSM", p.id, p.lua->Errors());
        p.loggedLuaDisabled = true;
    }
}

std::string PetApp::ActionFor(const PetActor& p) const {
    const auto& pkg = p.asset->pkg;
    if (p.isBug) return "bug";
    switch (p.body.mode) {
        case core::BodyMode::Dragged: return "drag";
        case core::BodyMode::Falling: return p.body.vy < 0 && pkg.HasClip("jump") ? "jump" : "fall";  // 올라가는 동안만 jump
        case core::BodyMode::Riding: return pkg.HasClip("ride") ? "ride" : "sit";  // 다른 펫 머리 위
        case core::BodyMode::Climbing:  // 말하기·커서 반응보다 우선. 떠 있는 벽에 뛰어 붙는 동안은 jump
            if (p.body.climb && p.body.climb->leaping && pkg.HasClip("jump")) return "jump";
            return pkg.HasClip("climb") ? "climb" : "fall";
        default: break;
    }
    if (p.speaking) return "talk";  // 없으면 AnimationPlayer 가 idle 로 대체
    const std::string& act = p.fsm->Activity();
    // 벽 타기 결정 중 바닥·창 위에 있으면 아직 벽으로 걸어가는 중 (오르는 모습은 Climbing 일 때만). 점프는 순간이라 그대로 서 있음
    const bool moving = (act == "climb" || act == "follow") && p.body.walkDir != 0;  // 벽으로·친구에게 걸어가는 중
    const std::string rest = pkg.HasClip("rest") ? "rest" : "idle";
    if (act == "greet") return pkg.HasClip("talk") ? "talk" : "look";  // 인사: 손 흔들기 (talk)
    if (act == "highfive") return pkg.HasClip("highfive") ? "highfive" : pkg.HasClip("talk") ? "talk" : "look";
    if (act == "lean") return pkg.HasClip("lean") ? "lean" : "sit";
    if (act == "peek") return pkg.HasClip("peek") ? "peek" : "look";  // 친구 화면 들여다보기
    if (p.cursorNear && act != "walk" && !moving) return "look";
    if (act == "climb" || act == "follow") return moving ? "walk" : rest;
    if (act == "jump" || act == "rest" || act == "carry" || act == "stack") return rest;  // carry = 머리에 친구를 태우고 가만히
    return act;
}

void PetApp::StartJumpOrClimb(PetActor& pet, double now) {
    auto& b = pet.body;
    const core::PhysicsParams params = ParamsFor(pet);
    if (pet.fsm->Activity() == "jump") {
        // 60% 는 닿는 높이의 창 위를 노리고, 없거나 나머지는 그 자리·걷는 방향으로 작게 뛴다
        const bool toWindow = std::bernoulli_distribution(0.6)(pet.rng);
        const double pick = std::uniform_real_distribution<double>(0.0, 1.0)(pet.rng);
        auto plan = physics_.PlanJump(b, params, pet.fsm->WalkDir(), toWindow, pick);
        if (toWindow) {
            const auto& st = physics_.LastPlanStats();
            log::Info("[pet{}] jump plan candidates={} height={} reach={} room={} blocked={} ok={}", pet.id, st.candidates,
                      st.height, st.reach, st.room, st.blocked, st.ok);
        }
        if (plan) {
            core::Physics::StartJump(b, *plan);
            if (plan->vx != 0) pet.facingLeft = plan->vx < 0;
            log::Info("[pet{}] jump to={} vx={:.0f} vy={:.0f} foot=({:.0f},{:.0f})", pet.id,
                      plan->toWindow ? Hex(plan->target) : std::string("hop"), plan->vx, plan->vy, b.foot.x, b.foot.y);
            return;
        }
    } else if (auto plan = physics_.PlanClimb(b, params); [&] {
                   const auto& st = physics_.LastPlanStats();
                   log::Info("[pet{}] climb plan windows={} height={} path={} room={} side_hidden={} ok={}", pet.id, st.candidates,
                             st.height, st.reach, st.room, st.blocked, st.ok);
                   return plan.has_value();
               }()) {
        core::Physics::StartClimb(b, *plan);
        // 벽까지 걸어갈 시간만큼 결정을 늘린다 (도중에 다른 행동으로 바뀌지 않게)
        const double walkSec = std::abs(plan->startX - b.foot.x) / std::max(1.0, params.walkSpeed);
        pet.fsm->Force({"climb", std::min(60.0, walkSec + 1.0), 0}, now);
        log::Info("[pet{}] climb wall={} side={} start_x={:.0f} leap={} foot=({:.0f},{:.0f})", pet.id,
                  plan->wall == core::kNoWindow ? std::string("screen") : Hex(plan->wall), plan->side, plan->startX,
                  plan->leapToY > 0, b.foot.x, b.foot.y);
        return;
    }
    pet.fsm->Force({"rest", 1.0, 0}, now);  // 뛸 자리·오를 벽이 없음 (rest 애니메이션이 없으면 idle)
}

}  // namespace dp
