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
    return b.mode == BodyMode::Falling || b.walkDir != 0 || b.occludedSince >= 0;
}

void Physics::BeginDrag(PetBody& b) {
    b.mode = BodyMode::Dragged;
    b.host = kNoWindow;
    b.vy = 0;
    b.walkDir = 0;
    b.occludedSince = -1;
}

void Physics::EndDrag(PetBody& b) {
    b.mode = BodyMode::Falling;  // 놓으면 중력 복구 (기준선 §5.2)
    b.vy = 0;
}

void Physics::Detach(PetBody& b, std::vector<BodyEvent>& events, const char* reason) {
    events.push_back({BodyEvent::Kind::Detached, b.host, reason});
    b.mode = BodyMode::Falling;
    b.host = kNoWindow;
    b.vy = 0;
    b.walkDir = 0;
    b.occludedSince = -1;
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
        events.push_back({BodyEvent::Kind::Relocated, kNoWindow, "화면 밖 → 가장 가까운 작업 영역"});
    }
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
            ClampIntoScreen(b, events);
            return;
        }
        b.foot.y = s->y;
    }
}

std::vector<BodyEvent> Physics::Revalidate(PetBody& b, const PhysicsParams& p, double now) {
    std::vector<BodyEvent> events;
    if (b.mode == BodyMode::Attached || b.mode == BodyMode::Grounded) ValidateResting(b, p, now, events);
    if (b.mode == BodyMode::Falling) ClampIntoScreen(b, events);
    return events;
}

std::vector<BodyEvent> Physics::Step(PetBody& b, const PhysicsParams& p, double dt, double now) {
    std::vector<BodyEvent> events;
    const double halfSupport = b.width * p.footSupportRatio / 2.0;

    switch (b.mode) {
        case BodyMode::Dragged:
            return events;

        case BodyMode::Falling: {
            b.vy = std::min(b.vy + p.gravity * dt, p.maxFallSpeed);
            const double y0 = b.foot.y, y1 = y0 + b.vy * dt;
            // [y0, y1] 사이에서 처음 만나는, 발 지지 폭이 들어가는 표면
            const Surface* hit = nullptr;
            for (const Surface& s : surfaces_) {
                if (s.y < y0 || s.y > y1) continue;
                if (!Supports(s, b.foot.x - halfSupport, b.foot.x + halfSupport, b.height)) continue;
                if (!hit || s.y < hit->y) hit = &s;
            }
            if (hit) {
                b.foot.y = hit->y;
                b.vy = 0;
                if (hit->owner == kNoWindow) {
                    b.mode = BodyMode::Grounded;
                    b.host = kNoWindow;
                } else {
                    b.mode = BodyMode::Attached;
                    b.host = hit->owner;
                    b.localX = b.foot.x - world_.windows[windowIndex_.at(hit->owner)].bounds.left;
                }
                b.occludedSince = -1;
                events.push_back({BodyEvent::Kind::Landed, b.host, ""});
            } else {
                b.foot.y = y1;
                ClampIntoScreen(b, events);  // 바닥이 없는 곳(모니터 사이 틈 등)으로 떨어지지 않게
            }
            return events;
        }

        case BodyMode::Grounded:
        case BodyMode::Attached: {
            ValidateResting(b, p, now, events);
            if (b.mode == BodyMode::Falling || b.walkDir == 0) return events;
            // 걷기: 노출 구간 끝에서 방향 전환 (창 측면 충돌, 기준선 §4.2)
            const double nx = b.foot.x + b.walkDir * p.walkSpeed * dt;
            const Surface* s = SurfaceOf(b.mode == BodyMode::Attached ? b.host : kNoWindow, b.foot.x);
            if (s && Supports(*s, nx - halfSupport, nx + halfSupport, b.height)) {
                if (b.mode == BodyMode::Attached) b.localX += nx - b.foot.x;
                b.foot.x = nx;
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
