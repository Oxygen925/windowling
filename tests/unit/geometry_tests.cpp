#include <catch2/catch_test_macros.hpp>

#include "core/geometry.h"

using namespace dp::core;

// ctest 가 테스트 이름을 명령행으로 넘기므로 케이스 이름은 ASCII 로 둔다 (한글은 SECTION 에만)
TEST_CASE("RectI intersection and coverage", "[geometry]") {
    const RectI monitor{0, 0, 2560, 1440};

    SECTION("완전히 덮는 전체화면 창") {
        CHECK(CoverageRatio(monitor, RectI{0, 0, 2560, 1440}) == 1.0);
    }
    SECTION("최대화 창은 작업 표시줄만큼 덜 덮는다") {
        CHECK(CoverageRatio(monitor, RectI{0, 0, 2560, 1392}) < 0.98);
    }
    SECTION("겹치지 않으면 빈 사각형") {
        CHECK(Intersect(monitor, RectI{3000, 0, 3100, 100}).Empty());
    }
    SECTION("음수 좌표 모니터 (D10: 데이터로 대체 검증)") {
        const RectI left{-1920, 0, 0, 1080};
        CHECK(Intersect(left, RectI{-100, 10, 50, 20}) == RectI{-100, 10, 0, 20});
    }
}

TEST_CASE("IntervalI length", "[geometry]") {
    CHECK(IntervalI{10, 30}.Length() == 20);
    CHECK(IntervalI{30, 10}.Empty());
}
