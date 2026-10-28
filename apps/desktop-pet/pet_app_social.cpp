// 펫끼리 상호작용: 인사(고정 문구·AI)·하이파이브·부딪치면 튕기기·붙어 앉기·따라가기(잡기 놀이·기차놀이).
// 판단은 core/behavior/social, 실행은 여기서
#include "pet_app.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "diagnostics/log.h"
#include "ui/text.h"

namespace dp {

namespace {
bool Resting(const PetActor& p) { return p.body.mode == core::BodyMode::Grounded || p.body.mode == core::BodyMode::Attached; }
// 상호작용 중이라 다른 상호작용·인사를 받지 않는 행동
bool Busy(const PetActor& p) {
    const std::string& a = p.fsm->Activity();
    return p.isBug || a == "jump" || a == "climb" || a == "greet" || a == "highfive" || a == "lean" || a == "peek" || p.pairWith >= 0 ||
           p.raceWith >= 0 || p.mountId >= 0 || p.riderId >= 0 || p.stackTarget >= 0 || a == "carry" ||
           p.body.climb.has_value();
}
}  // namespace

void PetApp::SayFixed(PetActor& pet, const char* keyPrefix, int count, double now, double seconds) {
    const int n = std::uniform_int_distribution<int>(1, count)(pet.rng);
    if (!pet.bubble) pet.bubble = std::make_unique<SpeechBubble>();
    pet.bubble->SetText(ui::T(std::string(keyPrefix) + std::to_string(n)));  // 고정 문구 (TTS 로 읽지 않음)
    pet.bubble->hideAt = now + seconds;
}

void PetApp::StartFollowing(PetActor& pet, PetActor& target, FollowMode mode, double seconds, double now) {
    pet.followTarget = target.id;
    pet.followMode = mode;
    pet.followUntil = now + seconds;
    pet.followJumpAt = now;
    pet.fsm->Force({"follow", seconds + 0.5, 0}, now);
    static const char* kModes[] = {"tag", "train", "lean", "visit"};
    log::Info("[pet{}] follow target=pet{} mode={} tag_back={}", pet.id, target.id, kModes[static_cast<int>(mode)], pet.tagBack);
}

void PetApp::StartFollow(PetActor& pet, double now) {
    // 가장 가까운 다른 펫 (끌리거나 숨은 펫 제외). 없으면 쉼
    PetActor* best = nullptr;
    double bestD = 0;
    for (auto& o : pets_) {
        if (o.get() == &pet || o->isBug || o->hiddenFullscreen || o->body.mode == core::BodyMode::Dragged) continue;
        const double d = std::hypot(o->body.foot.x - pet.body.foot.x, o->body.foot.y - pet.body.foot.y);
        if (!best || d < bestD) best = o.get(), bestD = d;
    }
    if (!best) {
        pet.fsm->Force({"rest", 1.0, 0}, now);
        return;
    }
    StartFollowing(pet, *best, FollowMode::Tag, 12.0, now);
}

// 기차놀이: 같은 면의 쉬고 있는 펫(train 을 가진) 최대 3마리가 앞 펫을 줄지어 따라온다. 없으면 혼자 걷는다
void PetApp::StartTrain(PetActor& leader, double now) {
    std::vector<PetActor*> followers;
    for (auto& o : pets_) {
        PetActor& f = *o;
        if (&f == &leader || !Resting(f) || f.hiddenFullscreen || f.speaking || Busy(f) || f.followTarget >= 0) continue;
        if (!f.asset->pkg.behavior.activities.contains("train")) continue;
        const bool same = f.body.mode == leader.body.mode &&
                          (f.body.mode == core::BodyMode::Attached ? f.body.host == leader.body.host
                                                                   : std::abs(f.body.foot.y - leader.body.foot.y) < 2.0);
        if (same) followers.push_back(&f);
    }
    std::sort(followers.begin(), followers.end(), [&](PetActor* x, PetActor* y) {
        return std::abs(x->body.foot.x - leader.body.foot.x) < std::abs(y->body.foot.x - leader.body.foot.x);
    });
    if (followers.size() > 3) followers.resize(3);
    // 따라올 펫이 있는 쪽의 반대로 걸어 줄이 생기게
    int dir = std::bernoulli_distribution(0.5)(leader.rng) ? 1 : -1;
    if (!followers.empty()) dir = followers.front()->body.foot.x < leader.body.foot.x ? 1 : -1;
    leader.fsm->Force({"walk", 8.0, dir}, now);
    PetActor* ahead = &leader;
    for (PetActor* f : followers) {
        StartFollowing(*f, *ahead, FollowMode::Train, 9.0, now);
        ahead = f;
    }
    log::Info("[pet{}] train lead followers={} dir={}", leader.id, followers.size(), dir);
}

void PetApp::UpdateSocial(double now) {
    if (pets_.size() < 2 || paused_ || options_.noBehavior || options_.forceWalk || !options_.forceActivity.empty()) return;
    UpdatePairs(now);
    for (auto& p : pets_)  // 경주가 끝나지 않고 너무 길어지면 (떨어지거나 끌려감) 그냥 끝
        if (p->raceWith >= 0 && now >= p->raceUntil) p->raceWith = -1;
    std::vector<core::SocialPet> social;
    for (auto& p : pets_) {
        const bool bubbling = p->bubble && p->bubble->Active();
        const auto& beh = p->asset->pkg.behavior;
        core::SocialPet s{p->id, p->body.foot, p->body.width, p->body.mode, p->body.host,
                          Resting(*p) && !p->hiddenFullscreen && !p->speaking && !bubbling && !Busy(*p), beh.greet};
        s.bumps = beh.bump;
        s.leans = beh.lean;
        s.walkDir = p->body.walkDir;
        s.sitting = p->fsm->Activity() == "sit";
        social.push_back(s);
    }
    auto socialOf = [&](int id) -> const core::SocialPet* {
        for (const auto& s : social)
            if (s.id == id) return &s;
        return nullptr;
    };
    // 따라가기 (잡기 놀이·기차놀이·붙어 앉으러 가기)
    for (auto& up : pets_) {
        PetActor& me = *up;
        if (me.followTarget < 0 || me.fsm->Activity() != "follow") continue;
        auto& b = me.body;
        if (!Resting(me)) continue;  // 점프 중
        PetActor* t = PetById(me.followTarget);
        const core::SocialPet* ms = socialOf(me.id);
        const core::SocialPet* ts = socialOf(me.followTarget);
        if (!t || !ms || !ts || now >= me.followUntil || me.speaking) {
            log::Info("[pet{}] follow end reason={}", me.id, !t ? "gone" : now >= me.followUntil ? "timeout" : "busy");
            me.followTarget = -1;
            me.tagBack = false;
            b.walkDir = 0;
            me.fsm->Force({"rest", 1.0, 0}, now);
            continue;
        }
        // 닿았다고 볼 거리: 잡기 = 거의 붙음, 기차 = 한 칸 띄움, 붙어 앉기 = 옆자리, 들여다보기 = 노트북 쪽 옆자리(그 지점까지)
        const double reach = me.followMode == FollowMode::Train ? 1.25
                           : me.followMode == FollowMode::Lean  ? 0.85
                           : me.followMode == FollowMode::Visit ? 0.06
                                                                : 0.9;
        core::SocialPet goal = *ts;
        if (me.followMode == FollowMode::Visit) goal.foot.x += (t->facingLeft ? -1.0 : 1.0) * (me.body.width + t->body.width) / 2.0;
        const core::FollowStep step = core::DecideFollow(*ms, goal, reach);
        switch (step.kind) {
            case core::FollowStep::Kind::Wait: b.walkDir = 0; break;
            case core::FollowStep::Kind::Walk: b.walkDir = step.dir; break;
            case core::FollowStep::Kind::Jump: {
                std::optional<core::JumpPlan> plan;
                if (now >= me.followJumpAt) {
                    plan = physics_.PlanJumpTo(b, ParamsFor(me), t->body.mode == core::BodyMode::Attached ? t->body.host : core::kNoWindow,
                                               t->body.foot.x);
                    me.followJumpAt = now + 1.5;
                }
                if (plan) {
                    core::Physics::StartJump(b, *plan);
                    if (plan->vx != 0) me.facingLeft = plan->vx < 0;
                    log::Info("[pet{}] follow jump to={} vx={:.0f}", me.id, Hex(plan->target), plan->vx);
                } else {
                    b.walkDir = step.dir;  // 닿지 않으면 그쪽으로 걷는다 (가장자리에서 내려가거나 돌아섬)
                }
                break;
            }
            case core::FollowStep::Kind::Caught:
                b.walkDir = 0;
                if (me.followMode == FollowMode::Train) {
                    me.facingLeft = t->body.foot.x < b.foot.x;  // 앞 펫 바로 뒤에서 기다린다 (줄 유지)
                } else if (me.followMode == FollowMode::Lean) {
                    LeanTogether(me, *t, now);
                } else if (me.followMode == FollowMode::Visit) {
                    me.followTarget = -1;
                    const bool coding = Resting(*t) && t->fsm->Activity() == "idle" && t->pairWith < 0 && !t->speaking;
                    if (coding) StartPairSession(me, *t, now);
                    else GreetPair(me, *t, now);  // 그새 코딩을 멈췄으면 인사만
                } else {
                    Caught(me, *t, now);
                }
                break;
        }
        if (b.walkDir != 0) me.facingLeft = b.walkDir < 0;
    }
    // 걷다 마주치면 서로 튕겨 물러남 (쌍마다 15초에 한 번)
    if (auto pair = core::FindBump(social, bumpBook_, now, 15.0)) {
        PetActor* a = PetById(pair->first);
        PetActor* c = PetById(pair->second);
        if (a && c && a->followTarget != c->id && c->followTarget != a->id) BumpPair(*a, *c, now);
    }
    // 둘 다 앉아 있으면 한쪽이 다가가 붙어 앉음 (쌍마다 2분에 한 번)
    if (auto pair = core::FindLean(social, leanBook_, now, 120.0)) {
        PetActor* a = PetById(pair->first);
        PetActor* c = PetById(pair->second);
        if (a && c) {
            leanBook_.Mark(a->id, c->id, now);
            StartFollowing(*c, *a, FollowMode::Lean, 8.0, now);
        }
    }
    // 인사: 같은 면에서 마주치면 둘 다 멈춰 마주 보고 말풍선 (쌍마다 90초에 한 번)
    if (auto pair = core::FindGreeting(social, greetBook_, now, 90.0)) {
        PetActor* a = PetById(pair->first);
        PetActor* c = PetById(pair->second);
        if (a && c && a->followTarget != c->id && c->followTarget != a->id) GreetPair(*a, *c, now);
    }
}

// 잡기 놀이에서 잡음: 둘 다 멈춰 마주 보고 말풍선 (AI 대화를 켰으면 한마디씩), 잡힌 쪽이 한 번 되잡으러 온다
void PetApp::Caught(PetActor& me, PetActor& t, double now) {
    if (t.isBug) return CatchBug(me, t, now);
    auto& b = me.body;
    log::Info("[pet{}] caught pet{}", me.id, t.id);
    const bool chaseBack = !me.tagBack && t.asset->pkg.behavior.activities.contains("follow") && !t.speaking;
    me.followTarget = -1;
    me.tagBack = false;
    me.fsm->Force({"greet", 1.6, 0}, now);
    me.facingLeft = t.body.foot.x < b.foot.x;
    const bool targetFree = !t.speaking && !(t.bubble && t.bubble->Active());
    if (targetFree) {
        t.body.walkDir = 0;
        t.fsm->Force({"greet", 1.6, 0}, now);
        t.facingLeft = b.foot.x < t.body.foot.x;
    }
    greetBook_.Mark(me.id, t.id, now);
    if (targetFree && TryPetChat(me, t, now, ai::PetChatSituation::Caught)) return;  // 되잡기는 하지 않는다
    SayFixed(me, "pets.caught_", 2, now, 1.6);
    if (!targetFree) return;
    SayFixed(t, "pets.caught_by_", 2, now, 1.6);
    if (chaseBack) {  // 인사 끝난 뒤 되잡으러 (UpdateBehavior 가 greet → follow 로 바꾼다)
        t.tagBack = true;
        t.followTarget = me.id;
        t.followMode = FollowMode::Tag;
        t.followUntil = now + 1.6 + 10.0;
        t.followJumpAt = now + 1.6;
    }
}

// 붙어 앉기: 옆자리에 닿으면 둘 다 서로 쪽으로 기대 앉는다
void PetApp::LeanTogether(PetActor& me, PetActor& t, double now) {
    me.followTarget = -1;
    for (PetActor* x : {&me, &t}) {
        x->body.walkDir = 0;
        x->fsm->Force({"lean", 6.0, 0}, now);
    }
    me.facingLeft = t.body.foot.x < me.body.foot.x;
    t.facingLeft = me.body.foot.x < t.body.foot.x;
    if (std::bernoulli_distribution(0.5)(me.rng)) SayFixed(me, "pets.lean_", 2, now, 2.0);
    log::Info("[pet{}] lean with pet{}", me.id, t.id);
}

// 부딪침: 둘 다 상대를 보며 뒤로 폴짝 (착지하면 쉼 → 다음 행동)
void PetApp::BumpPair(PetActor& a, PetActor& c, double now) {
    bumpBook_.Mark(a.id, c.id, now);
    for (auto [me, other] : {std::pair{&a, &c}, std::pair{&c, &a}}) {
        const int away = me->body.foot.x < other->body.foot.x ? -1 : 1;
        me->followTarget = -1;
        if (auto plan = physics_.PlanJump(me->body, ParamsFor(*me), away, false)) {
            core::Physics::StartJump(me->body, *plan);
            me->facingLeft = away > 0;  // 상대 쪽을 본 채로
        }
    }
    if (std::bernoulli_distribution(0.5)(a.rng)) SayFixed(a, "pets.bump_", 2, now, 1.2);
    log::Info("[pet{}] bump pet{}", a.id, c.id);
}

void PetApp::GreetPair(PetActor& a, PetActor& c, double now) {
    a.facingLeft = c.body.foot.x < a.body.foot.x;
    c.facingLeft = a.body.foot.x < c.body.foot.x;
    greetBook_.Mark(a.id, c.id, now);
    for (PetActor* x : {&a, &c}) {
        x->body.walkDir = 0;
        x->followTarget = -1;
    }
    // 둘 다 하이파이브 그림이 있으면 반쯤은 하이파이브 (짧게, AI 대화 없이)
    if (a.asset->pkg.HasClip("highfive") && c.asset->pkg.HasClip("highfive") && std::bernoulli_distribution(0.5)(a.rng)) {
        for (PetActor* x : {&a, &c}) x->fsm->Force({"highfive", 1.4, 0}, now);
        SayFixed(a, "pets.highfive_", 2, now, 1.4);
        log::Info("[pet{}] highfive pet{}", a.id, c.id);
        return;
    }
    for (PetActor* x : {&a, &c}) x->fsm->Force({"greet", 2.4, 0}, now);
    if (!TryPetChat(a, c, now, ai::PetChatSituation::Met)) {  // 설정으로 켰으면 고정 문구 대신 AI 로 한마디씩
        SayFixed(a, "pets.greet_", 4, now, 2.4);
        SayFixed(c, "pets.reply_", 4, now, 2.4);
    }
    log::Info("[pet{}] greet pet{}", a.id, c.id);
}

// ---- 페어 프로그래밍 ----

// 같은 면에서 코딩(idle) 중인 가장 가까운 친구의 노트북 쪽 옆자리로 걸어간다. 없으면 쉼
void PetApp::StartVisit(PetActor& me, double now) {
    PetActor* best = nullptr;
    double bestD = 0;
    for (auto& o : pets_) {
        PetActor& c = *o;
        if (&c == &me || !Resting(c) || c.hiddenFullscreen || c.speaking || Busy(c) || c.followTarget >= 0) continue;
        if (c.fsm->Activity() != "idle") continue;
        const bool same = c.body.mode == me.body.mode &&
                          (c.body.mode == core::BodyMode::Attached ? c.body.host == me.body.host
                                                                   : std::abs(c.body.foot.y - me.body.foot.y) < 2.0);
        const double d = std::abs(c.body.foot.x - me.body.foot.x);
        if (same && (!best || d < bestD)) best = &c, bestD = d;
    }
    if (!best) {
        me.fsm->Force({"rest", 1.0, 0}, now);
        return;
    }
    StartFollowing(me, *best, FollowMode::Visit, 10.0, now);
}

// 옆에서 화면을 들여다보며 한마디 (AI 를 켰으면 AI, 아니면 고정 문구) → 코딩하던 펫이 돌아보며 "어때?" → 리뷰 한마디
void PetApp::StartPairSession(PetActor& v, PetActor& c, double now) {
    v.pairWith = c.id;
    v.pairCoder = false;
    c.pairWith = v.id;
    c.pairCoder = true;
    v.body.walkDir = 0;
    v.facingLeft = c.body.foot.x < v.body.foot.x;
    v.fsm->Force({"peek", 4.5, 0}, now);
    c.fsm->Force({"idle", 9.0, 0}, now);  // 계속 코딩
    v.pairStage = 1;
    v.pairAt = now + 4.5;
    v.pairAi = TryPetChat(v, c, now, ai::PetChatSituation::PeekCode);  // AI 는 c 의 답까지 한 번에
    if (!v.pairAi) SayFixed(v, "pets.peek_", 4, now, 4.0);
    log::Info("[pet{}] pair with pet{} ai={}", v.id, c.id, v.pairAi);
}

void PetApp::EndPair(PetActor& v) {
    if (PetActor* c = PetById(v.pairWith); c && c->pairWith == v.id) c->pairWith = -1;
    v.pairWith = -1;
    v.pairStage = 0;
}

void PetApp::UpdatePairs(double now) {
    for (auto& up : pets_) {
        PetActor& v = *up;
        if (v.pairWith < 0 || v.pairCoder) continue;
        PetActor* c = PetById(v.pairWith);
        if (!c || !Resting(v) || !Resting(*c) || v.speaking || c->speaking) {  // 끌려가거나 사용자와 대화
            EndPair(v);
            continue;
        }
        if (now < v.pairAt) continue;
        if (v.pairStage == 1 && !v.pairAi) {  // 코딩하던 펫이 돌아보며 묻는다
            c->facingLeft = v.body.foot.x < c->body.foot.x;
            c->fsm->Force({"greet", 2.0, 0}, now);
            SayFixed(*c, "pets.review_ask_", 2, now, 1.6);
            v.fsm->Force({"peek", 1.2, 0}, now);
            v.pairStage = 2;
            v.pairAt = now + 1.2;
        } else if (v.pairStage == 2) {  // 리뷰 한마디
            v.fsm->Force({"greet", 2.0, 0}, now);
            SayFixed(v, "pets.review_", 3, now, 2.0);
            EndPair(v);
        } else {
            EndPair(v);  // AI 대화로 대신했으면 여기서 끝
        }
    }
}

// ---- 벽 타기 경주 ----

// 같은 면의 race 를 가진 친구와 각자 가까운 쪽 화면 끝으로 달려가 오른다. 먼저 꼭대기에 닿아 뛰어내리는 쪽이 이긴다
void PetApp::StartRace(PetActor& me, double now) {
    PetActor* rival = nullptr;
    double bestD = 0;
    for (auto& o : pets_) {
        PetActor& r = *o;
        if (&r == &me || !Resting(r) || r.hiddenFullscreen || r.speaking || Busy(r) || r.followTarget >= 0) continue;
        if (!r.asset->pkg.behavior.activities.contains("race") || !r.asset->pkg.HasClip("climb")) continue;
        const double d = std::abs(r.body.foot.x - me.body.foot.x);
        if (!rival || d < bestD) rival = &r, bestD = d;
    }
    if (!rival || !me.asset->pkg.HasClip("climb")) {
        me.fsm->Force({"rest", 1.0, 0}, now);
        return;
    }
    const int sideMe = me.body.foot.x < rival->body.foot.x ? -1 : 1;
    auto planMe = physics_.PlanClimbEdge(me.body, ParamsFor(me), sideMe);
    auto planRival = physics_.PlanClimbEdge(rival->body, ParamsFor(*rival), -sideMe);
    if (!planMe || !planRival) {  // 걸어서 닿는 끝이 없음 (창 위 등)
        me.fsm->Force({"rest", 1.0, 0}, now);
        return;
    }
    for (auto [pet, plan, other] : {std::tuple{&me, *planMe, rival}, std::tuple{rival, *planRival, &me}}) {
        core::Physics::StartClimb(pet->body, plan);
        const double walkSec = std::abs(plan.startX - pet->body.foot.x) / std::max(1.0, ParamsFor(*pet).walkSpeed);
        pet->fsm->Force({"climb", std::min(60.0, walkSec + 1.0), 0}, now);
        pet->followTarget = -1;
        pet->raceWith = other->id;
        pet->raceUntil = now + 60.0;
    }
    SayFixed(me, "pets.race_start_", 2, now, 2.0);
    log::Info("[pet{}] race with pet{} sides={}/{}", me.id, rival->id, sideMe, -sideMe);
}

void PetApp::OnClimbTop(PetActor& pet) {
    if (pet.raceWith < 0) return;
    PetActor* other = PetById(pet.raceWith);
    pet.raceWith = -1;
    if (!other || other->raceWith != pet.id) return;  // 상대가 먼저 끝났거나 경주가 아님
    other->raceWith = -1;
    const double now = Now();
    SayFixed(pet, "pets.race_win_", 2, now, 2.5);
    SayFixed(*other, "pets.race_lose_", 2, now, 2.5);
    log::Info("[pet{}] race won against pet{}", pet.id, other->id);
}

// ---- 올라타기 (탑 쌓기) ----

namespace {
// 맨 아래 펫 (base) 과 맨 위 펫 (top), 쌓인 수
struct Stack {
    PetActor* base = nullptr;
    PetActor* top = nullptr;
    int height = 0;
};
}  // namespace

// 가까운 펫(서 있는, 걷지 않는) 의 탑 맨 위 머리로 뛴다. 탑은 3층까지
void PetApp::StartStack(PetActor& me, double now) {
    auto stackOf = [&](PetActor* p) {
        Stack s{p, p, 1};
        for (PetActor* r = PetById(p->riderId); r && s.height < 8; r = PetById(r->riderId)) s.top = r, ++s.height;
        return s;
    };
    const core::PhysicsParams params = ParamsFor(me);
    Stack best;
    std::optional<core::JumpPlan> bestPlan;
    double bestD = 0;
    for (auto& o : pets_) {
        PetActor& base = *o;
        if (&base == &me || base.mountId >= 0 || !Resting(base) || base.hiddenFullscreen || base.speaking || base.body.walkDir != 0)
            continue;
        if (base.riderId < 0 && Busy(base)) continue;  // 이미 태우고 있으면 carry 라 Busy 지만 위에 쌓을 수 있다
        const Stack s = stackOf(&base);
        if (s.height >= 3) continue;
        const core::PointD head = core::RiderFoot(s.top->body, s.top->asset->pkg.behavior.headHeight);
        const double d = std::abs(head.x - me.body.foot.x);
        if (d > params.jumpReach || me.body.foot.y - head.y > params.jumpHeight) continue;
        auto plan = physics_.AimAtPoint(me.body, params, head.x, head.y);
        if (plan && (!best.base || d < bestD)) best = s, bestPlan = plan, bestD = d;
    }
    if (!best.base) {
        log::Info("[pet{}] stack no target", me.id);
        me.fsm->Force({"rest", 1.0, 0}, now);
        return;
    }
    best.base->fsm->Force({"carry", 4.0, 0}, now);  // 뛰어오는 동안 가만히
    core::Physics::StartJump(me.body, *bestPlan);
    me.stackTarget = best.top->id;
    me.facingLeft = bestPlan->vx < 0;
    log::Info("[pet{}] stack jump onto pet{} height={}", me.id, best.top->id, best.height);
}

void PetApp::MountOn(PetActor& r, PetActor& m, double now) {
    r.stackTarget = -1;
    r.mountId = m.id;
    m.riderId = r.id;
    r.body.mode = core::BodyMode::Riding;
    r.body.vx = r.body.vy = 0;
    r.body.walkDir = 0;
    r.body.host = core::kNoWindow;
    r.body.foot = core::RiderFoot(m.body, m.asset->pkg.behavior.headHeight);
    r.rideUntil = now + std::uniform_real_distribution<double>(6.0, 10.0)(r.rng);
    if (r.hwnd) SetWindowPos(r.hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);  // 아래 펫 위에 그려지게
    SayFixed(r, "pets.stack_", 2, now, 2.0);
    log::Info("[pet{}] mounted pet{} until={:.1f}s", r.id, m.id, r.rideUntil - now);
}

void PetApp::Dismount(PetActor& r) {
    if (PetActor* m = PetById(r.mountId); m && m->riderId == r.id) m->riderId = -1;
    r.mountId = -1;
    if (r.body.mode == core::BodyMode::Riding) {
        r.body.mode = core::BodyMode::Falling;
        r.body.vx = r.body.vy = 0;
    }
}

void PetApp::HopOff(PetActor& r, double now) {
    Dismount(r);
    const core::PhysicsParams params = ParamsFor(r);
    const int dir = std::bernoulli_distribution(0.5)(r.rng) ? 1 : -1;
    r.body.vy = -std::sqrt(2.0 * params.gravity * r.body.height * 0.35);
    r.body.vx = dir * params.walkSpeed * 1.3;
    r.facingLeft = dir < 0;
    r.fsm->Force({"rest", 1.0, 0}, now);
    log::Info("[pet{}] hop off", r.id);
}

void PetApp::SyncRiders() {
    // 아래 펫이 서 있거나(또는 올라타 있거나) 하면 그 머리 위로, 아니면 (끌림·낙하·벽) 떨어진다. 탑은 3층이라 3번이면 아래부터 다 맞춘다
    for (int pass = 0; pass < 3; ++pass)
        for (auto& up : pets_) {
            PetActor& r = *up;
            if (r.mountId < 0) continue;
            PetActor* m = PetById(r.mountId);
            const bool mountOk = m && m->riderId == r.id &&
                                 (Resting(*m) || m->body.mode == core::BodyMode::Riding) && !m->hiddenFullscreen;
            if (r.body.mode != core::BodyMode::Riding || !mountOk) {  // 내가 끌렸거나 아래가 움직임
                log::Info("[pet{}] dismount reason={}", r.id, r.body.mode != core::BodyMode::Riding ? "self" : "mount");
                Dismount(r);
                continue;
            }
            r.body.foot = core::RiderFoot(m->body, m->asset->pkg.behavior.headHeight);
        }
    for (auto& up : pets_)  // 태우던 펫이 사라졌거나 내렸으면 머리를 비운다 (carry 를 풀게)
        if (up->riderId >= 0)
            if (PetActor* r = PetById(up->riderId); !r || r->mountId != up->id) up->riderId = -1;
}

void PetApp::UpdateStacks(const std::vector<double>& prevFootY, double now) {
    for (size_t k = 0; k < pets_.size() && k < prevFootY.size(); ++k) {
        PetActor& me = *pets_[k];
        if (me.stackTarget < 0) continue;
        PetActor* top = PetById(me.stackTarget);
        const bool flying = me.body.mode == core::BodyMode::Falling;
        if (!top || top->riderId >= 0 || (!Resting(*top) && top->body.mode != core::BodyMode::Riding)) {
            if (!flying) me.stackTarget = -1;  // 목표가 사라짐 → 그냥 착지
            continue;
        }
        const core::PointD head = core::RiderFoot(top->body, top->asset->pkg.behavior.headHeight);
        if (core::CrossesHead(prevFootY[k], me.body.foot.y, me.body.foot.x, head.x, head.y, top->body.width * 0.35)) {
            MountOn(me, *top, now);
        } else if (!flying) {
            me.stackTarget = -1;  // 다른 곳에 내려앉음 (빗나감)
            log::Info("[pet{}] stack missed", me.id);
        }
    }
    SyncRiders();
}

// ---- 버그 잡기 ----

size_t PetApp::RealPetCount() const {
    return static_cast<size_t>(std::count_if(pets_.begin(), pets_.end(), [](const auto& p) { return !p->isBug; }));
}

PetActor* PetApp::LastRealPet() {
    for (auto it = pets_.rbegin(); it != pets_.rend(); ++it)
        if (!(*it)->isBug) return it->get();
    return nullptr;
}

namespace {
// 버그를 쫓을 수 있는 펫: bug_hunt 를 켰고 bug 그림이 있음
bool Hunter(const PetActor& p) { return !p.isBug && p.asset->pkg.behavior.bugHunt && p.asset->pkg.HasClip("bug"); }
}  // namespace

double PetApp::NextBugCheck(double now) {
    for (auto& p : pets_)
        if (Hunter(*p)) return std::max(now, nextBugAt_);
    return kNoDeadline;  // 사냥할 펫이 없으면 깨우지 않는다
}

// 사냥꾼 하나 근처 위쪽에서 버그가 떨어진다. 한 번에 하나, 25초 안에 못 잡으면 도망친다
bool PetApp::SpawnBug(double now) {
    if (std::any_of(pets_.begin(), pets_.end(), [](const auto& p) { return p->isBug; })) return false;
    // 다른 놀이 중이 아닌 사냥꾼만 쫓아간다. 아무도 없으면 나오지 않는다 (아무도 안 쫓는 버그는 그냥 도망치니까)
    std::vector<PetActor*> hunters;
    for (auto& p : pets_)
        if (Hunter(*p) && !p->hiddenFullscreen && Resting(*p) && !p->speaking && !Busy(*p) && p->followTarget < 0)
            hunters.push_back(p.get());
    if (hunters.empty()) return false;
    PetActor& h = *hunters[std::uniform_int_distribution<size_t>(0, hunters.size() - 1)(bugRng_)];
    PetActor* bug = CreatePet(h.asset, 0, 1, nextBugId_++);
    if (!bug) return false;
    bug->isBug = true;
    bug->bugUntil = now + 25.0;
    const auto& world = physics_.World();
    const int mi = core::MonitorIndexFor(world, h.body.foot);
    const core::RectI work = mi >= 0 ? world.monitors[mi].work : core::RectI{0, 0, 1920, 1040};
    const double off = std::uniform_real_distribution<double>(200.0, 400.0)(bugRng_) * (std::bernoulli_distribution(0.5)(bugRng_) ? 1 : -1);
    const double half = bug->body.width / 2.0;
    bug->body.foot = {std::clamp(h.body.foot.x + off, work.left + half, work.right - half), work.top + bug->body.height + 20.0};
    bug->body.mode = core::BodyMode::Falling;
    log::Info("[pet{}] bug appears near pet{}", bug->id, h.id);
    SayFixed(h, "pets.bug_spot_", 2, now, 1.8);
    for (PetActor* p : hunters) StartFollowing(*p, *bug, FollowMode::Tag, 22.0, now);
    return true;
}

// 버그: 재빠르게 이리저리 (가끔 멈칫), 시간이 다 되면 도망
void PetApp::UpdateBug(PetActor& bug, double now) {
    auto& b = bug.body;
    if (bug.bugDead) return;
    if (now >= bug.bugUntil) {
        bug.bugDead = true;
        log::Info("[pet{}] bug escaped", bug.id);
        for (auto& p : pets_)
            if (p->followTarget == bug.id) {
                SayFixed(*p, "pets.bug_escape_", 2, now, 1.8);
                break;
            }
        return;
    }
    const bool resting = b.mode == core::BodyMode::Grounded || b.mode == core::BodyMode::Attached;
    if (!resting) {
        b.walkDir = 0;
        return;
    }
    if (bug.fsm->DecisionAt() <= now) {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (u(bug.rng) < 0.8) bug.fsm->Force({"walk", 0.8 + 1.4 * u(bug.rng), u(bug.rng) < 0.5 ? -1 : 1}, now);
        else bug.fsm->Force({"idle", 0.4, 0}, now);
        b.walkDir = bug.fsm->Activity() == "walk" ? bug.fsm->WalkDir() : 0;
    }
    if (b.walkDir != 0) bug.facingLeft = b.walkDir < 0;
}

void PetApp::CatchBug(PetActor& me, PetActor& bug, double now) {
    me.followTarget = -1;
    me.tagBack = false;
    me.body.walkDir = 0;
    if (bug.bugDead) return;
    bug.bugDead = true;
    me.facingLeft = bug.body.foot.x < me.body.foot.x;
    me.fsm->Force({"greet", 1.8, 0}, now);
    SayFixed(me, "pets.bug_caught_", 2, now, 1.8);
    log::Info("[pet{}] caught bug{}", me.id, bug.id);
}

void PetApp::SweepBugs() {
    for (size_t i = pets_.size(); i-- > 0;)
        if (pets_[i]->isBug && pets_[i]->bugDead) RemovePet(pets_[i].get());
}

}  // namespace dp
