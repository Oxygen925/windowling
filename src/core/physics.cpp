#include "core/physics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dp::core {

const char* ToString(BodyMode m) {
    switch (m) {
        case BodyMode::Falling: return "Falling";
        case BodyMode::Grounded: return "Grounded";
        case BodyMode::Attached: return "Attached";
        case BodyMode::Dragged: return "Dragged";
        case BodyMode::Climbing: return "Climbing";
        case BodyMode::Riding: return "Riding";
    }
    return "?";
}

void Physics::SetWorld(const WorldSnapshot& world) {
    world_ = world;
    surfaces_ = BuildSurfaces(world_);
    windowIndex_.clear();
    for (size_t i = 0; i < world_.windows.size(); ++i) windowIndex_[world_.windows[i].id] = i;
}

const Surface* Physics::SurfaceOf(WindowId owner, double x) const {
    for (const Surface& s : surfaces_) {
        if (s.owner != owner) continue;
        if (owner != kNoWindow) return &s;
        // 바닥은 모니터마다 있으므로 x 가 들어가는 바닥
        for (const IntervalI& iv : s.exposed)
            if (x >= iv.begin && x < iv.end) return &s;
    }
    return nullptr;
}

bool Physics::NeedsStepping(const PetBody& b) {
    // Riding: 아래 펫이 끌리거나 움직이면 바로 따라가도록 (올라타기는 몇 초뿐)
    return b.mode == BodyMode::Falling || b.mode == BodyMode::Climbing || b.mode == BodyMode::Riding || b.walkDir != 0 ||
           b.occludedSince >= 0 || b.climb.has_value();
}

void Physics::BeginDrag(PetBody& b) {
    b.mode = BodyMode::Dragged;
    b.host = kNoWindow;
    b.vy = b.vx = 0;
    b.walkDir = 0;
    b.occludedSince = -1;
    b.climb.reset();
}

void Physics::EndDrag(PetBody& b) {
    b.mode = BodyMode::Falling;  // 놓으면 중력 복구 (기준선 §5.2)
    b.vy = b.vx = 0;
    b.climb.reset();
}

void Physics::Detach(PetBody& b, std::vector<BodyEvent>& events, const char* reason) {
    events.push_back({BodyEvent::Kind::Detached, b.mode == BodyMode::Climbing && b.climb ? b.climb->wall : b.host, reason});
    b.mode = BodyMode::Falling;
    b.host = kNoWindow;
    b.vy = b.vx = 0;
    b.walkDir = 0;
    b.occludedSince = -1;
    b.climb.reset();
}

void Physics::ClampIntoScreen(PetBody& b, std::vector<BodyEvent>& events) {
    const int mi = MonitorIndexFor(world_, b.foot);
    if (mi < 0) return;
    const RectI& work = world_.monitors[mi].work;
    const double half = b.width / 2.0;
    const double x = std::clamp(b.foot.x, work.left + half, std::max(work.left + half, work.right - half));
    const double y = std::clamp(b.foot.y, static_cast<double>(work.top + b.height), static_cast<double>(work.bottom));
    if (x != b.foot.x || y != b.foot.y) {
        b.foot = {x, y};
        b.vx = 0;
        events.push_back({BodyEvent::Kind::Relocated, kNoWindow, "화면 밖 → 가장 가까운 작업 영역"});
    }
}

// 작업 영역의 세로 끝 x 가 가상 화면의 바깥 끝인가 (다른 모니터와 맞닿은 끝은 벽이 아니다)
bool Physics::IsOuterEdge(double x, const RectI& work) const {
    for (const MonitorInfo& m : world_.monitors) {
        if (m.work == work) continue;
        const bool touches = m.work.left == x || m.work.right == x;
        const bool overlapY = m.work.top < work.bottom && work.top < m.work.bottom;
        if (touches && overlapY) return false;
    }
    return true;
}

void Physics::ValidateResting(PetBody& b, const PhysicsParams& p, double now, std::vector<BodyEvent>& events) {
    const double halfSupport = b.width * p.footSupportRatio / 2.0;
    if (b.mode == BodyMode::Attached) {
        auto it = windowIndex_.find(b.host);
        if (it == windowIndex_.end()) {  // 창 종료·최소화·cloak·숨김
            Detach(b, events, "host gone");
            return;
        }
        const RectI& r = world_.windows[it->second].bounds;
        // 창 크기 변경: 발이 창 폭 밖이면 가장자리로 클램프 (창이 펫보다 좁으면 아래 지지 판정에서 떨어짐)
        b.localX = std::clamp(b.localX, halfSupport, std::max(halfSupport, r.Width() - halfSupport));
        b.foot = {r.left + b.localX, static_cast<double>(r.top)};
        const Surface* s = SurfaceOf(b.host, b.foot.x);
        const bool supported = s && Supports(*s, b.foot.x - halfSupport, b.foot.x + halfSupport, b.height);
        if (supported) {
            b.occludedSince = -1;
            return;
        }
        // 가림 유예 (P1-D2): 순간적인 가림 변동으로 떨어지지 않도록
        if (b.occludedSince < 0) b.occludedSince = now;
        if (now - b.occludedSince >= p.occlusionGraceSec) Detach(b, events, "occluded");
        return;
    }
    if (b.mode == BodyMode::Grounded) {
        const Surface* s = SurfaceOf(kNoWindow, b.foot.x);
        if (!s) {  // 모니터 분리·작업 영역 변경
            b.mode = BodyMode::Falling;
            b.climb.reset();
            ClampIntoScreen(b, events);
            return;
        }
        b.foot.y = s->y;
    }
}

// 오르는 벽 중 펫 몸통 높이(y0~y1) 부분이 다른 창에 가려지지 않았는가 (화면 끝은 항상 보임)
bool Physics::WallVisible(const PetBody& b, double y0, double y1) const {
    if (!b.climb || b.climb->wall == kNoWindow) return true;
    auto it = windowIndex_.find(b.climb->wall);
    if (it == windowIndex_.end()) return false;
    const RectI& r = world_.windows[it->second].bounds;
    const IntervalI ys{std::max(r.top, static_cast<int32_t>(std::floor(y0))), std::min(r.bottom, static_cast<int32_t>(std::ceil(y1)))};
    if (ys.Empty()) return true;  // 창 상단 위로 몸이 나온 순간 (곧 올라선다)
    const int32_t x = b.climb->side > 0 ? r.left : r.right - 1;
    int32_t shown = 0;
    for (const IntervalI& iv : SideExposure(world_, it->second, x, ys)) shown += iv.Length();
    return shown >= ys.Length() * 0.6;
}

void Physics::ValidateClimb(PetBody& b, const PhysicsParams& p, double now, std::vector<BodyEvent>& events) {
    if (!b.climb) {
        Detach(b, events, "no wall");
        return;
    }
    ClimbPlan& c = *b.climb;
    const double half = b.width / 2.0;
    if (c.wall != kNoWindow) {
        auto it = windowIndex_.find(c.wall);
        if (it == windowIndex_.end()) {
            Detach(b, events, "host gone");
            return;
        }
        // 창이 움직이면 벽을 따라간다 (상단도 다시 읽음)
        const RectI& r = world_.windows[it->second].bounds;
        c.wallX = c.side > 0 ? r.left : r.right;
        c.topY = r.top;
        b.foot.x = c.wallX - c.side * half;
        if (!c.leaping && b.foot.y - b.height * 0.5 > r.bottom) {  // 벽이 몸 아래로 사라짐 (창이 위로 이동·줄어듦)
            Detach(b, events, "wall moved");
            return;
        }
    } else {
        const int mi = MonitorIndexFor(world_, {c.wallX - c.side * half, b.foot.y});
        if (mi < 0) {
            Detach(b, events, "edge gone");
            return;
        }
        const RectI& work = world_.monitors[mi].work;
        const double edge = c.side > 0 ? work.right : work.left;
        if (edge != c.wallX || !IsOuterEdge(edge, work)) {
            Detach(b, events, "edge moved");
            return;
        }
        c.topY = std::max(c.topY, static_cast<double>(work.top + b.height));
    }
    if (WallVisible(b, b.foot.y - b.height, b.foot.y)) {
        b.occludedSince = -1;
        return;
    }
    if (b.occludedSince < 0) b.occludedSince = now;
    if (now - b.occludedSince >= p.occlusionGraceSec) Detach(b, events, "occluded");
}

void Physics::StepClimb(PetBody& b, const PhysicsParams& p, double dt, std::vector<BodyEvent>& events) {
    ClimbPlan& c = *b.climb;
    if (c.leaping) {
        // 떠 있는 창의 벽으로 뛰어 붙는다: 위로 쏘아 올려 중력으로 감속, 멈추면 거기서부터 기어오른다
        b.vy += p.gravity * dt;
        b.foot.y += b.vy * dt;
        if (b.vy >= 0 || b.foot.y <= c.leapToY) {
            if (b.foot.y < c.leapToY) b.foot.y = c.leapToY;
            b.vy = 0;
            c.leaping = false;
        }
        return;
    }
    b.foot.y -= p.climbSpeed * dt;
    if (b.foot.y > c.topY) return;
    const double halfSupport = b.width * p.footSupportRatio / 2.0;
    if (c.wall != kNoWindow) {
        // 꼭대기: 창 위로 올라선다 (벽 쪽 끝)
        const RectI& r = world_.windows[windowIndex_.at(c.wall)].bounds;
        const double localX = c.side > 0 ? halfSupport : r.Width() - halfSupport;
        const double x = r.left + localX;
        const Surface* s = SurfaceOf(c.wall, x);
        if (s && Supports(*s, x - halfSupport, x + halfSupport, b.height)) {
            const WindowId host = c.wall;
            b.mode = BodyMode::Attached;
            b.host = host;
            b.localX = localX;
            b.foot = {x, static_cast<double>(r.top)};
            b.vx = b.vy = 0;
            b.walkDir = 0;
            b.occludedSince = -1;
            b.climb.reset();
            events.push_back({BodyEvent::Kind::Landed, host, "climbed"});
        } else {
            b.foot.y = c.topY;
            Detach(b, events, "top blocked");
        }
        return;
    }
    // 화면 끝: 벽을 차고 뛰어내린다
    const int mi = MonitorIndexFor(world_, b.foot);
    const double ceiling = mi >= 0 ? world_.monitors[mi].work.top + b.height : b.foot.y;
    const double h = std::clamp(b.foot.y - ceiling, 0.0, p.hopHeight);
    const int side = c.side;
    b.mode = BodyMode::Falling;
    b.climb.reset();
    b.vy = -std::sqrt(2.0 * p.gravity * h);
    b.vx = -side * p.walkSpeed * 2.0;
    b.occludedSince = -1;
    events.push_back({BodyEvent::Kind::Jumped, kNoWindow, "wall end"});
}

std::vector<BodyEvent> Physics::Revalidate(PetBody& b, const PhysicsParams& p, double now) {
    std::vector<BodyEvent> events;
    if (b.mode == BodyMode::Attached || b.mode == BodyMode::Grounded) ValidateResting(b, p, now, events);
    if (b.mode == BodyMode::Climbing) ValidateClimb(b, p, now, events);
    if (b.mode == BodyMode::Falling) ClampIntoScreen(b, events);
    return events;
}

const Surface* Physics::AdvanceFall(PetBody& b, const PhysicsParams& p, double dt) const {
    const double halfSupport = b.width * p.footSupportRatio / 2.0;
    b.vy = std::min(b.vy + p.gravity * dt, p.maxFallSpeed);
    // 가로 이동 (점프·벽에서 뛰어내림): 작업 영역 가장자리에서 멈춘다
    if (b.vx != 0) {
        double nx = b.foot.x + b.vx * dt;
        const int mi = MonitorIndexFor(world_, {nx, b.foot.y});
        if (mi >= 0) {
            const RectI& work = world_.monitors[mi].work;
            const double lo = work.left + b.width / 2.0, hi = work.right - b.width / 2.0;
            if (nx < lo || nx > hi) {
                nx = std::clamp(nx, lo, std::max(lo, hi));
                b.vx = 0;
            }
        }
        b.foot.x = nx;
    }
    const double y0 = b.foot.y, y1 = y0 + b.vy * dt;
    // [y0, y1] 사이에서 처음 만나는, 발 지지 폭이 들어가는 표면 (올라가는 중에는 착지 없음)
    const Surface* hit = nullptr;
    for (const Surface& s : surfaces_) {
        if (s.y < y0 || s.y > y1) continue;
        if (!Supports(s, b.foot.x - halfSupport, b.foot.x + halfSupport, b.height)) continue;
        if (!hit || s.y < hit->y) hit = &s;
    }
    b.foot.y = hit ? hit->y : y1;
    return hit;
}

const Surface* Physics::SimulateJump(const PetBody& body, const PhysicsParams& p, const JumpPlan& plan, double& landX) const {
    PetBody b = body;
    StartJump(b, plan);
    const double dt = p.stepSec > 0 ? p.stepSec : 1.0 / 60.0;
    for (int i = 0; i < static_cast<int>(6.0 / dt); ++i) {
        if (const Surface* hit = AdvanceFall(b, p, dt)) {
            landX = b.foot.x;
            return hit;
        }
    }
    return nullptr;
}

std::optional<JumpPlan> Physics::AimAt(const PetBody& b, const PhysicsParams& p, const Surface& s, double x) const {
    const int mi = MonitorIndexFor(world_, b.foot);
    if (mi < 0 || p.gravity <= 0) return std::nullopt;
    const double g = p.gravity;
    const double ceiling = world_.monitors[mi].work.top + b.height;  // 발이 이보다 위면 머리가 화면 밖
    const double high = std::min(b.foot.y, static_cast<double>(s.y));  // 출발·도착 중 높은 쪽
    const double extra = std::min(b.height * 0.35, high - ceiling);
    if (extra < 4) return std::nullopt;
    const double apex = high - extra;
    const double vy0 = std::sqrt(2.0 * g * (b.foot.y - apex));
    const double t = vy0 / g + std::sqrt(2.0 * (s.y - apex) / g);
    return JumpPlan{(x - b.foot.x) / t, -vy0, s.owner, true, x};
}

std::optional<JumpPlan> Physics::AimAtPoint(const PetBody& b, const PhysicsParams& p, double x, double y) const {
    const int mi = MonitorIndexFor(world_, b.foot);
    if (mi < 0 || p.gravity <= 0) return std::nullopt;
    const double g = p.gravity;
    const double ceiling = world_.monitors[mi].work.top + b.height;
    const double high = std::min(b.foot.y, y);
    const double extra = std::min(b.height * 0.4, high - ceiling);
    if (extra < 4) return std::nullopt;
    const double apex = high - extra;
    const double vy0 = std::sqrt(2.0 * g * (b.foot.y - apex));
    const double t = vy0 / g + std::sqrt(2.0 * (y - apex) / g);
    return JumpPlan{(x - b.foot.x) / t, -vy0, kNoWindow, false, x};
}

void Physics::JumpCandidates(const PetBody& b, const PhysicsParams& p, const Surface& s,
                             const std::function<std::optional<double>(double lo, double hi)>& chooseX,
                             const std::function<void(JumpPlan& plan, double x)>& visit) const {
    const double halfSupport = b.width * p.footSupportRatio / 2.0;
    const double margin = 8.0;  // 고정 스텝 오차로 가장자리에서 떨어지지 않게
    const double rise = b.foot.y - s.y;  // + 위로, - 아래로
    stats_.candidates += static_cast<int>(s.exposed.size());
    if (rise > p.jumpHeight || -rise > p.jumpHeight * 1.5) {
        stats_.height += static_cast<int>(s.exposed.size());
        return;
    }
    for (size_t k = 0; k < s.exposed.size(); ++k) {
        const double lo = s.exposed[k].begin + halfSupport + margin, hi = s.exposed[k].end - halfSupport - margin;
        if ((k < s.headroom.size() && s.headroom[k] < b.height) || lo > hi) {
            stats_.room++;
            continue;
        }
        const std::optional<double> x = chooseX(lo, hi);
        if (!x) {
            stats_.reach++;
            continue;
        }
        auto plan = AimAt(b, p, s, *x);
        if (!plan) {
            stats_.room++;
            continue;
        }
        // 같은 스텝으로 날아 보고, 처음 내려앉는 면이 목표일 때만 (자기 창·중간 창에 걸리면 버림)
        double landX = 0;
        const Surface* hit = SimulateJump(b, p, *plan, landX);
        if (!hit || hit->owner != s.owner) {
            stats_.blocked++;
            continue;
        }
        stats_.ok++;
        plan->landX = landX;
        visit(*plan, *x);
    }
}

std::optional<JumpPlan> Physics::PlanJump(const PetBody& b, const PhysicsParams& p, int dir, bool toWindow, double pick) const {
    if (b.mode != BodyMode::Grounded && b.mode != BodyMode::Attached) return std::nullopt;
    const int mi = MonitorIndexFor(world_, b.foot);
    if (mi < 0 || p.gravity <= 0) return std::nullopt;
    const double ceiling = world_.monitors[mi].work.top + b.height;
    if (toWindow) {
        // 닿는 다른 창의 상단 노출 구간 (바닥은 제외 — 내려가기는 가장자리에서 걸어 내려가면 된다)
        struct Cand { JumpPlan plan; double score; };
        std::vector<Cand> cands;
        stats_ = {};
        for (const Surface& s : surfaces_) {
            if (s.owner == kNoWindow || s.owner == b.host) continue;
            const double rise = b.foot.y - s.y;
            JumpCandidates(
                b, p, s,
                [&](double lo, double hi) -> std::optional<double> {  // 지금 x 에서 가장 가까운 곳, 방향·거리 제한
                    const double x = std::clamp(b.foot.x, lo, hi), dx = x - b.foot.x;
                    if ((dir != 0 && dx * dir < 0) || std::abs(dx) > p.jumpReach) return std::nullopt;
                    return x;
                },
                [&](JumpPlan& plan, double x) { cands.push_back({plan, std::abs(x - b.foot.x) + std::abs(rise) * 0.2}); });
        }
        if (!cands.empty()) {
            std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.score < y.score; });
            const size_t n = std::min<size_t>(3, cands.size());
            const size_t i = std::min(n - 1, static_cast<size_t>(std::clamp(pick, 0.0, 0.999) * n));
            return cands[i].plan;
        }
    }
    // 제자리·앞으로 작게 뛰기
    const double h = std::min(p.hopHeight, b.foot.y - ceiling);
    if (h < 4) return std::nullopt;
    return JumpPlan{dir * p.walkSpeed * 1.5, -std::sqrt(2.0 * p.gravity * h), kNoWindow, false, b.foot.x};
}

std::optional<JumpPlan> Physics::PlanJumpTo(const PetBody& b, const PhysicsParams& p, WindowId target, double targetX) const {
    if (b.mode != BodyMode::Grounded && b.mode != BodyMode::Attached) return std::nullopt;
    std::optional<JumpPlan> best;
    double bestD = std::numeric_limits<double>::max();
    stats_ = {};
    for (const Surface& s : surfaces_) {
        if (s.owner != target) continue;
        JumpCandidates(
            b, p, s,
            [&](double lo, double hi) -> std::optional<double> {  // 목표 x 쪽으로, 다만 한 번에 닿는 거리까지
                const double x = std::clamp(std::clamp(targetX, lo, hi), b.foot.x - p.jumpReach, b.foot.x + p.jumpReach);
                if (x < lo || x > hi) return std::nullopt;
                return x;
            },
            [&](JumpPlan& plan, double) {
                const double d = std::abs(plan.landX - targetX);
                if (d < bestD) {
                    bestD = d;
                    best = plan;
                }
            });
    }
    return best;
}

void Physics::StartJump(PetBody& b, const JumpPlan& plan) {
    b.mode = BodyMode::Falling;
    b.host = kNoWindow;
    b.vx = plan.vx;
    b.vy = plan.vy;
    b.walkDir = 0;
    b.occludedSince = -1;
    b.climb.reset();
}

std::optional<ClimbPlan> Physics::PlanClimb(const PetBody& b, const PhysicsParams& p) const {
    if (b.mode != BodyMode::Grounded && b.mode != BodyMode::Attached) return std::nullopt;
    const Surface* s = SurfaceOf(b.mode == BodyMode::Attached ? b.host : kNoWindow, b.foot.x);
    const int mi = MonitorIndexFor(world_, b.foot);
    if (!s || mi < 0) return std::nullopt;
    const double half = b.width / 2.0, halfSupport = b.width * p.footSupportRatio / 2.0;
    // 지금 서 있는 면에서 x 까지 끊기지 않고 걸어갈 수 있는가
    auto walkable = [&](double x) {
        return Supports(*s, std::min(b.foot.x, x) - halfSupport, std::max(b.foot.x, x) + halfSupport, b.height);
    };
    std::optional<ClimbPlan> best;
    stats_ = {};
    auto consider = [&](const ClimbPlan& c) {
        stats_.ok++;
        if (!best || std::abs(c.startX - b.foot.x) < std::abs(best->startX - b.foot.x)) best = c;
    };
    // 창 옆면: 펫 키 이상 높고, 벽이 발 근처까지 내려오고, 그 옆면이 보이고, 꼭대기에 올라설 자리가 있을 때
    for (size_t i = 0; i < world_.windows.size(); ++i) {
        const WindowInfo& w = world_.windows[i];
        const RectI& r = w.bounds;
        if (!w.landable || w.id == b.host) continue;
        stats_.candidates++;
        if (r.top > b.foot.y - b.height || r.bottom < b.foot.y - p.jumpHeight) {
            stats_.height++;
            continue;
        }
        // 벽이 발 근처까지 내려오지 않으면 거기까지 뛰어 붙는다 (몸이 벽에 걸칠 높이)
        const double leapToY = r.bottom < b.foot.y - b.height * 0.5 ? r.bottom + b.height * 0.4 : 0;
        if (leapToY > 0 && leapToY - r.top < b.height * 0.5) {  // 붙어도 오를 벽이 거의 없음
            stats_.height++;
            continue;
        }
        for (int side : {+1, -1}) {
            ClimbPlan c{w.id, side, static_cast<double>(side > 0 ? r.left : r.right), 0, static_cast<double>(r.top), leapToY};
            c.startX = c.wallX - side * half;
            if (!walkable(c.startX)) {
                stats_.reach++;
                continue;
            }
            const double topX = side > 0 ? r.left + halfSupport : r.right - halfSupport;
            const Surface* top = SurfaceOf(w.id, topX);
            if (!top || !Supports(*top, topX - halfSupport, topX + halfSupport, b.height)) {
                stats_.room++;
                continue;
            }
            const IntervalI ys{r.top, std::min(r.bottom, static_cast<int32_t>(b.foot.y))};
            int32_t shown = 0;
            for (const IntervalI& iv : SideExposure(world_, i, side > 0 ? r.left : r.right - 1, ys)) shown += iv.Length();
            if (ys.Empty() || shown < ys.Length() * 0.9) {
                stats_.blocked++;
                continue;
            }
            consider(c);
        }
    }
    // 화면 바깥 끝: 남은 높이의 60% 까지 오르고 뛰어내린다
    for (int side : {-1, +1})
        if (auto c = PlanClimbEdge(b, p, side)) consider(*c);
    return best;
}

std::optional<ClimbPlan> Physics::PlanClimbEdge(const PetBody& b, const PhysicsParams& p, int side) const {
    if (b.mode != BodyMode::Grounded && b.mode != BodyMode::Attached) return std::nullopt;
    const Surface* s = SurfaceOf(b.mode == BodyMode::Attached ? b.host : kNoWindow, b.foot.x);
    const int mi = MonitorIndexFor(world_, b.foot);
    if (!s || mi < 0) return std::nullopt;
    const RectI& work = world_.monitors[mi].work;
    const double edge = side < 0 ? work.left : work.right;
    if (!IsOuterEdge(edge, work)) return std::nullopt;
    const double half = b.width / 2.0, halfSupport = b.width * p.footSupportRatio / 2.0;
    ClimbPlan c{kNoWindow, side, edge, edge - side * half, 0};
    c.topY = b.foot.y - 0.6 * (b.foot.y - (work.top + b.height));
    const bool walkable =
        Supports(*s, std::min(b.foot.x, c.startX) - halfSupport, std::max(b.foot.x, c.startX) + halfSupport, b.height);
    if (b.foot.y - c.topY < b.height || !walkable) return std::nullopt;
    return c;
}

void Physics::StartClimb(PetBody& b, const ClimbPlan& plan) {
    b.climb = plan;
    const double dx = plan.startX - b.foot.x;
    b.walkDir = dx > 0.5 ? 1 : dx < -0.5 ? -1 : 0;
}

std::vector<BodyEvent> Physics::Step(PetBody& b, const PhysicsParams& p, double dt, double now) {
    std::vector<BodyEvent> events;
    const double halfSupport = b.width * p.footSupportRatio / 2.0;

    switch (b.mode) {
        case BodyMode::Dragged:
        case BodyMode::Riding:
            return events;

        case BodyMode::Climbing:
            ValidateClimb(b, p, now, events);
            if (b.mode == BodyMode::Climbing) StepClimb(b, p, dt, events);
            return events;

        case BodyMode::Falling: {
            const Surface* hit = AdvanceFall(b, p, dt);
            if (hit) {
                b.vy = b.vx = 0;
                if (hit->owner == kNoWindow) {
                    b.mode = BodyMode::Grounded;
                    b.host = kNoWindow;
                } else {
                    b.mode = BodyMode::Attached;
                    b.host = hit->owner;
                    b.localX = b.foot.x - world_.windows[windowIndex_.at(hit->owner)].bounds.left;
                }
                b.occludedSince = -1;
                b.climb.reset();
                events.push_back({BodyEvent::Kind::Landed, b.host, ""});
            } else {
                ClampIntoScreen(b, events);  // 바닥이 없는 곳(모니터 사이 틈 등)으로 떨어지지 않게
            }
            return events;
        }

        case BodyMode::Grounded:
        case BodyMode::Attached: {
            ValidateResting(b, p, now, events);
            if (b.mode == BodyMode::Falling) return events;
            if (b.climb) {
                // 벽 타기: 시작점까지 걸어간 뒤 오르기 시작
                const ClimbPlan& c = *b.climb;
                const double dx = c.startX - b.foot.x;
                if (std::abs(dx) <= p.walkSpeed * dt + 0.5) {
                    if (c.wall != kNoWindow && !windowIndex_.contains(c.wall)) {  // 가는 동안 창이 사라짐
                        b.climb.reset();
                        b.walkDir = 0;
                        return events;
                    }
                    b.foot.x = c.startX;
                    b.mode = BodyMode::Climbing;
                    b.host = kNoWindow;
                    b.walkDir = 0;
                    b.vy = b.vx = 0;
                    b.occludedSince = -1;
                    if (c.leapToY > 0 && c.leapToY < b.foot.y - 1) {
                        b.climb->leaping = true;
                        b.vy = -std::sqrt(2.0 * p.gravity * (b.foot.y - c.leapToY));
                    }
                    events.push_back({BodyEvent::Kind::Climbing, c.wall, c.wall == kNoWindow ? "screen edge" : "window side"});
                    return events;
                }
                b.walkDir = dx > 0 ? 1 : -1;
            }
            if (b.walkDir == 0) return events;
            // 걷기: 노출 구간 끝에서 방향 전환 (창 측면 충돌, 기준선 §4.2)
            const double nx = b.foot.x + b.walkDir * p.walkSpeed * dt;
            const Surface* s = SurfaceOf(b.mode == BodyMode::Attached ? b.host : kNoWindow, b.foot.x);
            if (s && Supports(*s, nx - halfSupport, nx + halfSupport, b.height)) {
                if (b.mode == BodyMode::Attached) b.localX += nx - b.foot.x;
                b.foot.x = nx;
            } else if (b.climb) {  // 벽까지 길이 끊김 (창이 움직였다) → 벽 타기 취소
                b.climb.reset();
                b.walkDir = 0;
                events.push_back({BodyEvent::Kind::Turned, b.host, "climb path blocked"});
            } else {
                b.walkDir = -b.walkDir;
                events.push_back({BodyEvent::Kind::Turned, b.host, "edge"});
            }
            return events;
        }
    }
    return events;
}

}  // namespace dp::core
