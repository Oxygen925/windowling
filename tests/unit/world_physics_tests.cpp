#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "core/physics.h"
#include "core/world.h"

using namespace dp::core;

namespace {

// 2560x1440 모니터, 작업 표시줄 48px
MonitorInfo Qhd() { return {{0, 0, 2560, 1440}, {0, 0, 2560, 1392}, 96}; }

WindowInfo Win(WindowId id, RectI r) { return {id, r, "app.exe", true}; }

PetBody NewPet(double x, double y) {
    PetBody b;
    b.foot = {x, y};
    b.width = b.height = 128;
    return b;
}

// 떨어지는 동안 스텝을 반복 (최대 n 회)
void FallUntilRest(Physics& ph, PetBody& b, const PhysicsParams& p, double& now, int n = 600) {
    for (int i = 0; i < n && b.mode == BodyMode::Falling; ++i) {
        now += 1.0 / 60.0;
        ph.Step(b, p, 1.0 / 60.0, now);
    }
}

}  // namespace

TEST_CASE("SubtractIntervals", "[world]") {
    CHECK(SubtractIntervals({0, 100}, {}) == std::vector<IntervalI>{{0, 100}});
    CHECK(SubtractIntervals({0, 100}, {{20, 30}, {50, 60}}) == std::vector<IntervalI>{{0, 20}, {30, 50}, {60, 100}});
    CHECK(SubtractIntervals({0, 100}, {{-10, 200}}).empty());
    CHECK(SubtractIntervals({0, 100}, {{40, 60}, {50, 120}}) == std::vector<IntervalI>{{0, 40}});
    CHECK(SubtractIntervals({0, 100}, {{100, 120}}) == std::vector<IntervalI>{{0, 100}});  // 인접(반열림)
}

TEST_CASE("TopExposure respects z-order", "[world]") {
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(1, {300, 100, 700, 600}),    // 위 (상단선 y=400 을 세로로 덮음)
                 Win(2, {100, 400, 1000, 900})};  // 아래
    CHECK(TopExposure(w, 1) == std::vector<IntervalI>{{100, 300}, {700, 1000}});
    CHECK(TopExposure(w, 0) == std::vector<IntervalI>{{300, 700}});

    SECTION("아래 창은 위 창의 노출을 가리지 않는다") {
        std::swap(w.windows[0], w.windows[1]);
        CHECK(TopExposure(w, 0) == std::vector<IntervalI>{{100, 1000}});
    }
}

TEST_CASE("BuildSurfaces maximized window and floor", "[world]") {
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(1, {0, 0, 2560, 1392}),      // 최대화: 위 공간 없음 → 착지 불가 (P1-D6)
                 Win(2, {500, 300, 1500, 900})};
    auto s = BuildSurfaces(w, 128);
    // 창 2 는 최대화 창에 가려져 노출 없음, 창 1 은 공간 없음 → 바닥만
    REQUIRE(s.size() == 1);
    CHECK(s[0].owner == kNoWindow);
    CHECK(s[0].y == 1392);

    SECTION("최대화 창이 없으면 창 2 상단이 표면") {
        w.windows.erase(w.windows.begin());
        auto s2 = BuildSurfaces(w, 128);
        REQUIRE(s2.size() == 2);
        CHECK(s2[0].owner == 2);
        CHECK(s2[0].y == 300);
    }
    SECTION("제외 목록 창은 표면이 아니지만 가림에는 쓰인다") {
        w.windows[0] = Win(1, {400, 200, 800, 1000});
        w.windows[0].landable = false;
        auto s3 = BuildSurfaces(w, 128);
        REQUIRE(s3.size() == 2);
        CHECK(s3[0].owner == 2);
        CHECK(s3[0].exposed == std::vector<IntervalI>{{800, 1500}});  // 창1(y 200~1000)이 창2 상단선 y=300 의 [400,800) 을 가림
    }
}

TEST_CASE("Physics baseline 4.2 scenarios", "[physics]") {
    PhysicsParams p;
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(7, {400, 500, 1400, 1000})};
    ph.SetWorld(w);
    double now = 0;

    PetBody b = NewPet(900, 100);
    FallUntilRest(ph, b, p, now);
    REQUIRE(b.mode == BodyMode::Attached);  // 창 상단 착지
    CHECK(b.host == 7);
    CHECK(b.foot.y == 500);
    CHECK(b.localX == 500);

    SECTION("창 이동: 상대 좌표 유지") {
        w.windows[0].bounds = {200, 600, 1200, 1100};
        ph.SetWorld(w);
        ph.Revalidate(b, p, now);
        CHECK(b.mode == BodyMode::Attached);
        CHECK(b.foot.x == 700);
        CHECK(b.foot.y == 600);
    }
    SECTION("창 종료·최소화: 부착 해제 후 바닥으로 낙하") {
        w.windows.clear();
        ph.SetWorld(w);
        auto ev = ph.Revalidate(b, p, now);
        CHECK(b.mode == BodyMode::Falling);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].reason == "host gone");
        FallUntilRest(ph, b, p, now);
        CHECK(b.mode == BodyMode::Grounded);
        CHECK(b.foot.y == 1392);
    }
    SECTION("창 크기 변경: 발이 폭 밖이면 가장자리로 클램프") {
        w.windows[0].bounds = {400, 500, 700, 1000};  // 폭 300 → localX 500 은 밖
        ph.SetWorld(w);
        ph.Revalidate(b, p, now);
        CHECK(b.mode == BodyMode::Attached);
        CHECK(b.localX == 300 - 32);  // 폭 - 발 지지 폭/2 (128*0.5/2)
    }
    SECTION("가림: 유예 시간 동안은 유지, 지나면 낙하 (P1-D2)") {
        w.windows.insert(w.windows.begin(), Win(8, {800, 300, 1100, 800}));  // 위로 올라온 창이 발 위치를 덮음
        ph.SetWorld(w);
        ph.Revalidate(b, p, now);
        CHECK(b.mode == BodyMode::Attached);  // 유예 중
        CHECK(b.occludedSince == now);
        CHECK(Physics::NeedsStepping(b));      // 유예 만료 판정을 위해 스텝 필요
        now += 0.2;
        ph.Step(b, p, 1.0 / 60.0, now);
        CHECK(b.mode == BodyMode::Attached);
        now += 0.1;
        ph.Step(b, p, 1.0 / 60.0, now);
        CHECK(b.mode == BodyMode::Falling);
    }
    SECTION("순간 가림은 낙하하지 않는다") {
        w.windows.insert(w.windows.begin(), Win(8, {800, 300, 1100, 800}));
        ph.SetWorld(w);
        ph.Revalidate(b, p, now);
        w.windows.erase(w.windows.begin());
        ph.SetWorld(w);
        now += 0.1;
        ph.Revalidate(b, p, now);
        CHECK(b.mode == BodyMode::Attached);
        CHECK(b.occludedSince < 0);
    }
    SECTION("걷기: 노출 구간 끝에서 방향 전환") {
        b.walkDir = 1;
        for (int i = 0; i < 60 * 20; ++i) ph.Step(b, p, 1.0 / 60.0, now += 1.0 / 60.0);
        CHECK(b.mode == BodyMode::Attached);
        CHECK(b.foot.x <= 1400 - 32);
        CHECK(b.foot.x >= 400 + 32);
    }
    SECTION("드래그 후 놓으면 낙하") {
        Physics::BeginDrag(b);
        b.foot = {2000, 200};
        Physics::EndDrag(b);
        FallUntilRest(ph, b, p, now);
        CHECK(b.mode == BodyMode::Grounded);
    }
}

TEST_CASE("Physics landing needs foot support width", "[physics]") {
    PhysicsParams p;  // 발 지지 폭 64px
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(1, {300, 100, 700, 600}), Win(2, {100, 400, 1000, 900})};  // 창2 노출: [100,300) [700,1000)
    ph.SetWorld(w);
    double now = 0;

    SECTION("노출 구간 안쪽이면 착지") {
        PetBody b = NewPet(850, 0);
        FallUntilRest(ph, b, p, now);
        CHECK(b.host == 2);
    }
    SECTION("발이 구간 경계에 걸치면 착지하지 않고 더 아래로") {
        PetBody b = NewPet(690, 50);  // 창1 [300,700) 에서 발 [658,722) → 창1 밖으로 삐져나옴
        FallUntilRest(ph, b, p, now);
        CHECK(b.host != 1);
    }
}

TEST_CASE("Physics negative coordinates and offscreen (D10 data substitute)", "[physics]") {
    PhysicsParams p;
    Physics ph;
    WorldSnapshot w;
    w.monitors = {{{-1920, 0, 0, 1080}, {-1920, 0, 0, 1040}, 96}, Qhd()};
    ph.SetWorld(w);
    double now = 0;

    SECTION("왼쪽(음수) 모니터 바닥에 착지") {
        PetBody b = NewPet(-500, 100);
        FallUntilRest(ph, b, p, now);
        CHECK(b.mode == BodyMode::Grounded);
        CHECK(b.foot.y == 1040);
    }
    SECTION("화면 밖이면 가장 가까운 작업 영역으로") {
        PetBody b = NewPet(5000, -300);
        auto ev = ph.Revalidate(b, p, now);
        CHECK_FALSE(ev.empty());
        CHECK(b.foot.x <= 2560 - 64);
        FallUntilRest(ph, b, p, now);
        CHECK(b.mode == BodyMode::Grounded);
        CHECK(b.foot.y == 1392);
    }
}

TEST_CASE("Mixed pet sizes use their own headroom", "[physics]") {
    // 창 상단 위 공간 180px: 128px 펫은 착지, 256px 펫은 지나쳐 바닥으로 (P2 보완: 큰 펫 키로 표면을 거르지 않음)
    PhysicsParams p;
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(7, {400, 180, 1400, 900})};
    ph.SetWorld(w);
    REQUIRE(ph.Surfaces().size() == 2);
    CHECK(ph.Surfaces()[0].headroom == std::vector<int32_t>{180});
    double now = 0;
    PetBody small = NewPet(700, 0), big = NewPet(1100, 0);
    small.foot.y = 100;
    big.width = big.height = 256;
    big.foot.y = 100;
    FallUntilRest(ph, small, p, now);
    FallUntilRest(ph, big, p, now);
    CHECK(small.mode == BodyMode::Attached);
    CHECK(small.host == 7);
    CHECK(big.mode == BodyMode::Grounded);
    CHECK(big.foot.y == 1392);

    SECTION("창이 위로 올라가 공간이 부족해지면 작은 펫도 유예 후 낙하") {
        w.windows[0].bounds = {400, 100, 1400, 900};  // 위 공간 100 < 128
        ph.SetWorld(w);
        ph.Revalidate(small, p, now);
        CHECK(small.mode == BodyMode::Attached);  // 유예 중
        now += p.occlusionGraceSec + 0.01;
        auto ev = ph.Revalidate(small, p, now);
        CHECK(small.mode == BodyMode::Falling);
        REQUIRE_FALSE(ev.empty());
    }
    SECTION("큰 펫도 공간이 충분한 창에는 착지") {
        w.windows.push_back(Win(8, {1500, 600, 2400, 1000}));  // 위 공간 600
        ph.SetWorld(w);
        PetBody big2 = NewPet(1900, 100);
        big2.width = big2.height = 256;
        FallUntilRest(ph, big2, p, now);
        CHECK(big2.mode == BodyMode::Attached);
        CHECK(big2.host == 8);
    }
    SECTION("최대화 창(위 공간 없음)은 크기와 관계없이 착지 불가") {
        w.windows = {Win(9, {0, 0, 2560, 1392})};
        ph.SetWorld(w);
        CHECK(ph.Surfaces().size() == 1);  // 바닥만
    }
}

// ---- 점프·벽 타기 ----
namespace {
PhysicsParams JumpParams() {
    PhysicsParams p;
    p.jumpHeight = 320;  // 키 128 × 2.5
    p.hopHeight = 56;
    p.climbSpeed = 120;
    return p;
}
}  // namespace

TEST_CASE("Jump: hop lands back on the same surface", "[physics][jump]") {
    const PhysicsParams p = JumpParams();
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(7, {400, 900, 1400, 1300})};
    ph.SetWorld(w);
    double now = 0;
    PetBody b = NewPet(900, 100);
    FallUntilRest(ph, b, p, now);
    REQUIRE(b.mode == BodyMode::Attached);

    auto plan = ph.PlanJump(b, p, 0, false);
    REQUIRE(plan);
    CHECK_FALSE(plan->toWindow);
    CHECK(plan->vx == 0);
    CHECK(plan->vy < 0);
    Physics::StartJump(b, *plan);
    CHECK(b.mode == BodyMode::Falling);
    CHECK(Physics::NeedsStepping(b));
    double minY = b.foot.y;
    for (int i = 0; i < 600 && b.mode == BodyMode::Falling; ++i) {
        ph.Step(b, p, 1.0 / 60.0, now += 1.0 / 60.0);
        minY = std::min(minY, b.foot.y);
    }
    CHECK(b.mode == BodyMode::Attached);
    CHECK(b.host == 7);
    CHECK(b.foot.x == 900);
    CHECK(minY < 900 - 40);  // 실제로 떴다
    CHECK(b.vx == 0);

    SECTION("앞으로 뛰면 가로로 이동") {
        auto fwd = ph.PlanJump(b, p, +1, false);
        REQUIRE(fwd);
        Physics::StartJump(b, *fwd);
        FallUntilRest(ph, b, p, now);
        CHECK(b.foot.x > 900);
    }
}

TEST_CASE("Jump: onto a higher window top within reach", "[physics][jump]") {
    const PhysicsParams p = JumpParams();
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(9, {1000, 1100, 1500, 1300}),   // 높은 창: 상단 1100 (바닥보다 292 위)
                 Win(8, {200, 600, 600, 900})};      // 너무 높은 창: 상단 600 (792 위, 닿지 않음)
    ph.SetWorld(w);
    double now = 0;
    PetBody b = NewPet(900, 1392);
    b.mode = BodyMode::Grounded;

    auto plan = ph.PlanJump(b, p, 0, true);
    REQUIRE(plan);
    CHECK(plan->toWindow);
    CHECK(plan->target == 9);
    CHECK(plan->vx > 0);
    Physics::StartJump(b, *plan);
    FallUntilRest(ph, b, p, now);
    CHECK(b.mode == BodyMode::Attached);
    CHECK(b.host == 9);
    CHECK(b.foot.y == 1100);

    SECTION("반대 방향만 허용하면 목표 없음 → 작게 뛰기") {
        PetBody c = NewPet(900, 1392);
        c.mode = BodyMode::Grounded;
        auto hop = ph.PlanJump(c, p, -1, true);
        REQUIRE(hop);
        CHECK_FALSE(hop->toWindow);
        CHECK(hop->vx < 0);
    }
    SECTION("쉬는 중이 아니면 계획 없음") {
        PetBody c = NewPet(900, 500);  // 공중 (Falling)
        CHECK_FALSE(ph.PlanJump(c, p, 0, true));
    }
}

TEST_CASE("Climb: walk to a window side, climb, and stand on top", "[physics][climb]") {
    const PhysicsParams p = JumpParams();
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(5, {1200, 700, 1800, 1392})};  // 바닥까지 내려온 창 (왼쪽 옆면 x=1200)
    ph.SetWorld(w);
    double now = 0;
    PetBody b = NewPet(900, 1392);
    b.mode = BodyMode::Grounded;

    auto plan = ph.PlanClimb(b, p);
    REQUIRE(plan);
    CHECK(plan->wall == 5);
    CHECK(plan->side == +1);
    CHECK(plan->startX == 1200 - 64);
    Physics::StartClimb(b, *plan);
    CHECK(b.walkDir == 1);

    bool climbed = false, climbing = false;
    for (int i = 0; i < 3000 && !climbed; ++i) {
        for (const auto& e : ph.Step(b, p, 1.0 / 60.0, now += 1.0 / 60.0)) {
            if (e.kind == BodyEvent::Kind::Climbing) climbing = true;
            if (e.kind == BodyEvent::Kind::Landed && e.reason == "climbed") climbed = true;
        }
        if (b.mode == BodyMode::Climbing) REQUIRE(b.foot.x == 1200 - 64);  // 벽에 붙어 있다
    }
    CHECK(climbing);
    REQUIRE(climbed);
    CHECK(b.mode == BodyMode::Attached);
    CHECK(b.host == 5);
    CHECK(b.foot.y == 700);
    CHECK(b.localX == 32);  // 벽 쪽 끝 (발 지지 폭 / 2)
    CHECK_FALSE(b.climb);

    SECTION("오르는 중 창이 닫히면 낙하") {
        PetBody c = NewPet(900, 1392);
        c.mode = BodyMode::Grounded;
        Physics::StartClimb(c, *ph.PlanClimb(c, p));
        for (int i = 0; i < 600 && c.mode != BodyMode::Climbing; ++i) ph.Step(c, p, 1.0 / 60.0, now += 1.0 / 60.0);
        REQUIRE(c.mode == BodyMode::Climbing);
        WorldSnapshot gone = w;
        gone.windows.clear();
        ph.SetWorld(gone);
        auto ev = ph.Revalidate(c, p, now);
        CHECK(c.mode == BodyMode::Falling);
        REQUIRE_FALSE(ev.empty());
        CHECK(ev[0].reason == "host gone");
        CHECK_FALSE(c.climb);
        FallUntilRest(ph, c, p, now);
        CHECK(c.mode == BodyMode::Grounded);
    }
    SECTION("오르는 중 창이 움직이면 벽을 따라간다") {
        PetBody c = NewPet(900, 1392);
        c.mode = BodyMode::Grounded;
        Physics::StartClimb(c, *ph.PlanClimb(c, p));
        for (int i = 0; i < 600 && c.mode != BodyMode::Climbing; ++i) ph.Step(c, p, 1.0 / 60.0, now += 1.0 / 60.0);
        REQUIRE(c.mode == BodyMode::Climbing);
        WorldSnapshot moved = w;
        moved.windows[0].bounds = {1250, 700, 1850, 1392};
        ph.SetWorld(moved);
        ph.Revalidate(c, p, now);
        CHECK(c.mode == BodyMode::Climbing);
        CHECK(c.foot.x == 1250 - 64);
    }
    SECTION("오르는 중 옆면이 다른 창에 가려지면 유예 뒤 낙하") {
        PetBody c = NewPet(900, 1392);
        c.mode = BodyMode::Grounded;
        Physics::StartClimb(c, *ph.PlanClimb(c, p));
        for (int i = 0; i < 600 && c.mode != BodyMode::Climbing; ++i) ph.Step(c, p, 1.0 / 60.0, now += 1.0 / 60.0);
        REQUIRE(c.mode == BodyMode::Climbing);
        WorldSnapshot covered = w;
        covered.windows.insert(covered.windows.begin(), Win(6, {1000, 400, 1400, 1392}));
        ph.SetWorld(covered);
        ph.Revalidate(c, p, now);
        CHECK(c.mode == BodyMode::Climbing);  // 유예 중
        now += 0.3;
        ph.Step(c, p, 1.0 / 60.0, now);
        CHECK(c.mode == BodyMode::Falling);
    }
}

TEST_CASE("Climb: no plan when the side is covered or too low", "[physics][climb]") {
    const PhysicsParams p = JumpParams();
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    PetBody b = NewPet(900, 1392);
    b.mode = BodyMode::Grounded;

    SECTION("옆면이 위 창에 가려짐 → 그 창은 후보 아님") {
        w.windows = {Win(6, {1100, 600, 1300, 1392}),   // 위: 창 5 의 왼쪽 옆면을 덮음
                     Win(5, {1200, 700, 1800, 1392})};
        ph.SetWorld(w);
        auto plan = ph.PlanClimb(b, p);
        REQUIRE(plan);
        CHECK(plan->wall != 5);
    }
    SECTION("펫 키보다 낮은 창은 오르지 않음 → 화면 끝") {
        w.windows = {Win(5, {1200, 1300, 1800, 1392})};
        ph.SetWorld(w);
        auto plan = ph.PlanClimb(b, p);
        REQUIRE(plan);
        CHECK(plan->wall == kNoWindow);
    }
    SECTION("벽이 발 근처까지 내려오지 않는 창 → 화면 끝") {
        w.windows = {Win(5, {1200, 300, 1800, 900})};
        ph.SetWorld(w);
        auto plan = ph.PlanClimb(b, p);
        REQUIRE(plan);
        CHECK(plan->wall == kNoWindow);
    }
}

TEST_CASE("Climb: screen edge, then jump off", "[physics][climb]") {
    const PhysicsParams p = JumpParams();
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    ph.SetWorld(w);
    double now = 0;
    PetBody b = NewPet(300, 1392);
    b.mode = BodyMode::Grounded;

    auto plan = ph.PlanClimb(b, p);
    REQUIRE(plan);
    CHECK(plan->wall == kNoWindow);
    CHECK(plan->side == -1);  // 가까운 왼쪽 끝
    CHECK(plan->wallX == 0);
    CHECK(plan->startX == 64);
    Physics::StartClimb(b, *plan);
    bool jumped = false;
    double topY = 1392;
    for (int i = 0; i < 4000 && !jumped; ++i) {
        for (const auto& e : ph.Step(b, p, 1.0 / 60.0, now += 1.0 / 60.0))
            if (e.kind == BodyEvent::Kind::Jumped) jumped = true;
        topY = std::min(topY, b.foot.y);
    }
    REQUIRE(jumped);
    CHECK(topY < 1392 - 128);
    CHECK(topY >= 128);
    CHECK(b.mode == BodyMode::Falling);
    CHECK(b.vx > 0);  // 벽 반대쪽으로
    FallUntilRest(ph, b, p, now);
    CHECK(b.mode == BodyMode::Grounded);
    CHECK(b.foot.x > 64);

    SECTION("모니터가 맞닿은 끝은 벽이 아님") {
        WorldSnapshot two = w;
        two.monitors.push_back({{-1920, 0, 0, 1080}, {-1920, 0, 0, 1040}, 96});
        ph.SetWorld(two);
        PetBody c = NewPet(300, 1392);
        c.mode = BodyMode::Grounded;
        auto pc = ph.PlanClimb(c, p);
        REQUIRE(pc);
        CHECK(pc->wallX == 2560);  // 왼쪽(0)은 다른 모니터와 맞닿음 → 오른쪽 끝
    }
    SECTION("드래그하면 벽 타기 상태가 지워진다") {
        PetBody c = NewPet(300, 1392);
        c.mode = BodyMode::Grounded;
        Physics::StartClimb(c, *ph.PlanClimb(c, p));
        Physics::BeginDrag(c);
        CHECK_FALSE(c.climb);
        CHECK(c.vx == 0);
        Physics::EndDrag(c);
        CHECK_FALSE(c.climb);
    }
}


// ---- 창 사이 넘나들기 (같은 높이·낮은 창, 비행 시뮬레이션 확인, 떠 있는 창 벽에 뛰어 붙기) ----
TEST_CASE("Jump: across to a same-level or lower window, verified by simulation", "[physics][jump]") {
    PhysicsParams p = JumpParams();
    p.jumpReach = 640;
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(1, {200, 800, 700, 1200}),     // 출발 창
                 Win(2, {1000, 820, 1500, 1200}),   // 옆 창 (거의 같은 높이, 300px 떨어짐)
                 Win(3, {300, 1150, 2000, 1380})};  // 아래 넓은 창 (아래로 350)
    ph.SetWorld(w);
    double now = 0;
    PetBody b = NewPet(600, 100);
    FallUntilRest(ph, b, p, now);
    REQUIRE(b.mode == BodyMode::Attached);
    REQUIRE(b.host == 1);

    auto plan = ph.PlanJump(b, p, +1, true, 0.0);
    REQUIRE(plan);
    CHECK(plan->toWindow);
    CHECK(plan->target != 1);
    const auto st = ph.LastPlanStats();
    CHECK(st.ok >= 1);
    Physics::StartJump(b, *plan);
    FallUntilRest(ph, b, p, now);
    CHECK(b.mode == BodyMode::Attached);
    CHECK(b.host == plan->target);  // 시뮬레이션대로 목표에 내려앉음
    CHECK(std::abs(b.foot.x - plan->landX) < 1.0);

    SECTION("바닥은 점프 목표가 아니다 (가장자리에서 걸어 내려가면 됨)") {
        WorldSnapshot one;
        one.monitors = {Qhd()};
        one.windows = {Win(1, {200, 800, 700, 1200})};
        ph.SetWorld(one);
        PetBody c = NewPet(600, 800);
        c.mode = BodyMode::Attached;
        c.host = 1;
        c.localX = 400;
        auto hop = ph.PlanJump(c, p, +1, true, 0.0);
        REQUIRE(hop);
        CHECK_FALSE(hop->toWindow);
    }
    SECTION("PlanJumpTo: 바닥의 목표 x 쪽으로") {
        auto to = ph.PlanJumpTo(b, p, kNoWindow, b.foot.x + 200);
        if (to) {  // 아래에 창 3 이 있으면 먼저 걸리므로 시뮬레이션이 막는다
            CHECK(to->target == kNoWindow);
        }
        auto toWin = ph.PlanJumpTo(b, p, 3, b.foot.x);
        if (toWin) CHECK(toWin->target == 3);  // 계획이 나오면 시뮬레이션상 그 창에 내려앉는다
    }
}

TEST_CASE("Climb: leap onto a floating window's side", "[physics][climb]") {
    PhysicsParams p = JumpParams();
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(5, {1200, 600, 1800, 1150})};  // 바닥에서 242 떠 있는 창
    ph.SetWorld(w);
    double now = 0;
    PetBody b = NewPet(900, 1392);
    b.mode = BodyMode::Grounded;
    auto plan = ph.PlanClimb(b, p);
    REQUIRE(plan);
    REQUIRE(plan->wall == 5);
    CHECK(plan->leapToY > 0);
    CHECK(plan->leapToY < 1392);
    Physics::StartClimb(b, *plan);
    bool leaped = false, climbed = false;
    for (int i = 0; i < 4000 && !climbed; ++i) {
        for (const auto& e : ph.Step(b, p, 1.0 / 60.0, now += 1.0 / 60.0))
            if (e.kind == BodyEvent::Kind::Landed && e.reason == "climbed") climbed = true;
        if (b.mode == BodyMode::Climbing && b.climb && b.climb->leaping) leaped = true;
        if (b.mode == BodyMode::Falling) break;
    }
    CHECK(leaped);
    REQUIRE(climbed);
    CHECK(b.host == 5);
    CHECK(b.foot.y == 600);

    SECTION("닿지 않는 높이의 창은 후보 아님") {
        WorldSnapshot hi = w;
        hi.windows[0].bounds = {1200, 200, 1800, 700};  // 아래끝이 692 위 (jumpHeight 320 밖)
        ph.SetWorld(hi);
        PetBody c = NewPet(900, 1392);
        c.mode = BodyMode::Grounded;
        auto pc = ph.PlanClimb(c, p);
        REQUIRE(pc);
        CHECK(pc->wall == kNoWindow);
        CHECK(ph.LastPlanStats().height >= 1);
    }
}


TEST_CASE("Climb: a chosen screen edge (race)", "[physics][climb]") {
    const PhysicsParams p = JumpParams();
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    w.windows = {Win(4, {1000, 900, 1600, 1200})};
    ph.SetWorld(w);
    PetBody b = NewPet(1800, 1392);
    b.mode = BodyMode::Grounded;
    auto left = ph.PlanClimbEdge(b, p, -1);
    auto right = ph.PlanClimbEdge(b, p, +1);
    REQUIRE(left);
    REQUIRE(right);
    CHECK(left->wallX == 0);
    CHECK(right->wallX == 2560);
    CHECK(right->side == +1);
    CHECK(right->wall == kNoWindow);
    // 창 위에서는 화면 끝까지 걸어갈 수 없다
    PetBody c = NewPet(1300, 100);
    double now = 0;
    FallUntilRest(ph, c, p, now);
    REQUIRE(c.mode == BodyMode::Attached);
    CHECK_FALSE(ph.PlanClimbEdge(c, p, -1));
    CHECK_FALSE(ph.PlanClimbEdge(c, p, +1));
}


TEST_CASE("Stack: aim at another pet's head and catch on the way down", "[physics][stack]") {
    const PhysicsParams p = JumpParams();
    Physics ph;
    WorldSnapshot w;
    w.monitors = {Qhd()};
    ph.SetWorld(w);
    PetBody mount = NewPet(1200, 1392);
    mount.mode = BodyMode::Grounded;
    PetBody me = NewPet(1000, 1392);
    me.mode = BodyMode::Grounded;
    const PointD head = RiderFoot(mount, 0.47);
    CHECK(head.x == 1200);
    CHECK(head.y == 1392 - 128 * 0.47);
    auto plan = ph.PlanJump(me, p, 0, false);  // 비교: 그냥 뛰기
    REQUIRE(plan);
    auto aim = ph.AimAtPoint(me, p, head.x, head.y);
    REQUIRE(aim);
    CHECK(aim->vx > 0);
    Physics::StartJump(me, *aim);
    bool caught = false;
    double now = 0;
    for (int i = 0; i < 600 && me.mode == BodyMode::Falling && !caught; ++i) {
        const double prevY = me.foot.y;
        ph.Step(me, p, p.stepSec, now += p.stepSec);
        caught = CrossesHead(prevY, me.foot.y, me.foot.x, head.x, head.y, mount.width * 0.35);
    }
    CHECK(caught);  // 같은 스텝으로 날면 머리 높이를 지날 때 가로로 머리 위
    CHECK(std::abs(me.foot.x - head.x) < mount.width * 0.35);

    SECTION("Riding 은 물리 스텝이 아무것도 하지 않고, 스텝은 계속 필요") {
        PetBody r = NewPet(head.x, head.y);
        r.mode = BodyMode::Riding;
        ph.Step(r, p, p.stepSec, now);
        CHECK(r.mode == BodyMode::Riding);
        CHECK(r.foot.y == head.y);
        CHECK(Physics::NeedsStepping(r));
        Physics::BeginDrag(r);
        CHECK(r.mode == BodyMode::Dragged);
    }
    SECTION("머리 위가 화면 위에 너무 가까우면 계획 없음") {
        PetBody top = NewPet(1000, 1392);
        top.mode = BodyMode::Grounded;
        CHECK_FALSE(ph.AimAtPoint(top, p, 1200, 100));
    }
    CHECK_FALSE(CrossesHead(1300, 1310, 1000, 1200, 1320, 40));  // 아직 머리 높이 위
    CHECK_FALSE(CrossesHead(1300, 1330, 1300, 1200, 1320, 40));  // 가로로 벗어남
}
