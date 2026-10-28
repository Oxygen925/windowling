#include <catch2/catch_test_macros.hpp>

#include "core/behavior/social.h"

using namespace dp::core;

namespace {
SocialPet Pet(int id, double x, double y, BodyMode m = BodyMode::Grounded, WindowId host = kNoWindow) {
    SocialPet s;
    s.id = id;
    s.foot = {x, y};
    s.width = 128;
    s.mode = m;
    s.host = host;
    s.available = true;
    s.greets = true;
    return s;
}
}  // namespace

TEST_CASE("SameSurface", "[social]") {
    CHECK(SameSurface(Pet(1, 0, 1392), Pet(2, 500, 1392)));
    CHECK_FALSE(SameSurface(Pet(1, 0, 1392), Pet(2, 500, 700)));
    CHECK(SameSurface(Pet(1, 0, 700, BodyMode::Attached, 9), Pet(2, 50, 700, BodyMode::Attached, 9)));
    CHECK_FALSE(SameSurface(Pet(1, 0, 700, BodyMode::Attached, 9), Pet(2, 50, 700, BodyMode::Attached, 8)));
    CHECK_FALSE(SameSurface(Pet(1, 0, 700, BodyMode::Falling), Pet(2, 50, 700, BodyMode::Falling)));
}

TEST_CASE("FindGreeting: near, same surface, cooldown", "[social]") {
    GreetBook book;
    std::vector<SocialPet> pets = {Pet(1, 100, 1392), Pet(2, 200, 1392), Pet(3, 1000, 1392)};
    auto g = FindGreeting(pets, book, 10.0, 90.0);
    REQUIRE(g);
    CHECK(*g == std::pair{1, 2});
    book.Mark(1, 2, 10.0);
    CHECK_FALSE(FindGreeting(pets, book, 50.0, 90.0));   // 쿨다운
    CHECK(FindGreeting(pets, book, 101.0, 90.0));        // 지남
    pets[1].greets = false;
    CHECK_FALSE(FindGreeting(pets, book, 200.0, 90.0));  // 한쪽이 인사를 안 켬
    pets[1].greets = true;
    pets[0].available = false;
    CHECK_FALSE(FindGreeting(pets, book, 200.0, 90.0));  // 바쁨
    book.Forget(1);
    pets[0].available = true;
    CHECK(FindGreeting(pets, book, 11.0, 90.0));
}

TEST_CASE("DecideFollow", "[social]") {
    const SocialPet me = Pet(1, 100, 1392);
    CHECK(DecideFollow(me, Pet(2, 600, 1392)).kind == FollowStep::Kind::Walk);
    CHECK(DecideFollow(me, Pet(2, 600, 1392)).dir == 1);
    CHECK(DecideFollow(me, Pet(2, -400, 1392)).dir == -1);
    CHECK(DecideFollow(me, Pet(2, 190, 1392)).kind == FollowStep::Kind::Caught);
    CHECK(DecideFollow(me, Pet(2, 300, 900, BodyMode::Attached, 4)).kind == FollowStep::Kind::Jump);
    CHECK(DecideFollow(me, Pet(2, 300, 900, BodyMode::Falling)).kind == FollowStep::Kind::Wait);
    CHECK(DecideFollow(me, Pet(2, 300, 900, BodyMode::Climbing)).kind == FollowStep::Kind::Wait);
}


TEST_CASE("FindBump: walking toward each other on the same surface", "[social]") {
    GreetBook book;
    auto a = Pet(1, 100, 1392), b = Pet(2, 180, 1392);
    a.bumps = b.bumps = true;
    CHECK_FALSE(FindBump({a, b}, book, 0, 15));  // 둘 다 서 있음
    a.walkDir = 1;                               // a 가 b 쪽으로
    auto r = FindBump({a, b}, book, 0, 15);
    REQUIRE(r);
    CHECK(*r == std::pair{1, 2});
    a.walkDir = -1;  // 멀어지는 중
    CHECK_FALSE(FindBump({a, b}, book, 0, 15));
    b.walkDir = -1;  // b 가 a 쪽으로
    CHECK(FindBump({a, b}, book, 0, 15));
    book.Mark(1, 2, 0);
    CHECK_FALSE(FindBump({a, b}, book, 10, 15));  // 쿨다운
    CHECK(FindBump({a, b}, book, 16, 15));
    b.foot.x = 400;  // 멀다
    CHECK_FALSE(FindBump({a, b}, book, 100, 15));
    b.foot.x = 180;
    b.bumps = false;
    CHECK_FALSE(FindBump({a, b}, book, 100, 15));
}

TEST_CASE("FindLean: both sitting nearby", "[social]") {
    GreetBook book;
    auto a = Pet(1, 100, 1392), b = Pet(2, 400, 1392), c = Pet(3, 300, 1392);
    for (auto* p : {&a, &b, &c}) p->leans = true;
    CHECK_FALSE(FindLean({a, b}, book, 0, 120));  // 앉아 있지 않음
    a.sitting = b.sitting = c.sitting = true;
    auto r = FindLean({a, b, c}, book, 0, 120);
    REQUIRE(r);
    CHECK(*r == std::pair{2, 3});  // 가장 가까운 쌍
    book.Mark(2, 3, 0);
    auto r2 = FindLean({a, b, c}, book, 1, 120);
    REQUIRE(r2);
    CHECK(*r2 == std::pair{1, 3});
    b.foot.x = 1500;
    c.foot.x = 1500;
    CHECK_FALSE(FindLean({a, b}, book, 500, 120));  // 너무 멂 (두 폭 평균 × 3 넘음)
}
