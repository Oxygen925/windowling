#include <catch2/catch_test_macros.hpp>

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
