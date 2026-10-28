#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>

#include "core/behavior/fsm.h"
#include "core/character/animation.h"
#include "core/character/package.h"
#include "core/config.h"

using namespace dp::core;
namespace fs = std::filesystem;

namespace {

// 테스트용 임시 패키지 폴더 (테스트 끝나면 삭제)
struct TempPackage {
    fs::path root;
    explicit TempPackage(const std::string& name) {
        std::random_device rd;
        root = fs::temp_directory_path() / ("dp_pkg_test_" + name + "_" + std::to_string(rd()));
        fs::create_directories(root / "render" / "sprite");
        fs::create_directories(root / "behaviors");
    }
    ~TempPackage() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void Write(const std::string& rel, const std::string& text) const {
        std::ofstream out(root / rel, std::ios::binary);
        out << text;
    }
    void WriteValid() const {
        Write("manifest.toml", R"(schema_version = 1
id = "test-pet"
name = "Test Pet"
version = "1.0.0"
renderers = ["sprite"]
[license]
name = "CC0-1.0"
author = "tester"
[sprite]
atlas = "render/sprite/atlas.png"
animations = "render/sprite/animations.toml"
base_size = 128
)");
        Write("render/sprite/atlas.png", "PNG-PLACEHOLDER");
        Write("render/sprite/animations.toml", R"(frame_width = 64
frame_height = 64
[animations]
idle = { frames = [0], fps = 1 }
walk = { frames = [0, 1, 2, 3], fps = 8 }
)");
    }
};

bool HasError(const PackageLoadResult& r, const std::string& needle) {
    for (const auto& e : r.errors)
        if (e.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST_CASE("ResolveInside rejects escapes", "[package]") {
    TempPackage p("resolve");
    CHECK(ResolveInside(p.root, "render/sprite/atlas.png").has_value());
    CHECK_FALSE(ResolveInside(p.root, "../outside.png").has_value());
    CHECK_FALSE(ResolveInside(p.root, "render/../../outside.png").has_value());
    CHECK_FALSE(ResolveInside(p.root, "C:/Windows/win.ini").has_value());
    CHECK_FALSE(ResolveInside(p.root, "/etc/passwd").has_value());
    CHECK_FALSE(ResolveInside(p.root, "").has_value());
    CHECK(ResolveInside(p.root, "render/./sprite/../sprite/atlas.png").has_value());  // 안쪽 이동은 허용
}

TEST_CASE("LoadPackage valid and defaults", "[package]") {
    TempPackage p("valid");
    p.WriteValid();
    auto r = LoadPackage(p.root);
    REQUIRE(r.errors.empty());
    REQUIRE(r.package);
    CHECK(r.package->id == "test-pet");
    CHECK(r.package->sprite.clips.size() == 2);
    CHECK(r.package->behavior.activities.size() == 3);  // behavior.toml 없음 → 기본 FSM 값
    CHECK(r.package->behavior.script.empty());
}

TEST_CASE("LoadPackage validation errors", "[package]") {
    TempPackage p("invalid");
    p.WriteValid();

    SECTION("스키마 버전") {
        p.Write("manifest.toml", "schema_version = 2\n");
        CHECK(HasError(LoadPackage(p.root), "schema_version"));
    }
    SECTION("id 형식") {
        auto text = std::string(R"(schema_version = 1
id = "Bad ID!"
name = "x"
version = "1"
renderers = ["sprite"]
[license]
name = "CC0-1.0"
author = "a"
[sprite]
atlas = "render/sprite/atlas.png"
animations = "render/sprite/animations.toml"
)");
        p.Write("manifest.toml", text);
        CHECK(HasError(LoadPackage(p.root), "id"));
    }
    SECTION("라이선스 필수") {
        p.Write("manifest.toml", R"(schema_version = 1
id = "no-license"
name = "x"
version = "1"
renderers = ["sprite"]
[sprite]
atlas = "render/sprite/atlas.png"
animations = "render/sprite/animations.toml"
)");
        CHECK(HasError(LoadPackage(p.root), "license"));
    }
    SECTION("경로 이탈") {
        p.Write("manifest.toml", R"(schema_version = 1
id = "escape"
name = "x"
version = "1"
renderers = ["sprite"]
[license]
name = "CC0-1.0"
author = "a"
[sprite]
atlas = "../../evil.png"
animations = "render/sprite/animations.toml"
)");
        auto r = LoadPackage(p.root);
        CHECK_FALSE(r.package);
        CHECK(HasError(r, "패키지 밖"));
    }
    SECTION("idle 애니메이션 필수") {
        p.Write("render/sprite/animations.toml", "frame_width = 64\nframe_height = 64\n[animations]\nwalk = { frames = [0] }\n");
        CHECK(HasError(LoadPackage(p.root), "idle"));
    }
    SECTION("프레임 크기 범위") {
        p.Write("render/sprite/animations.toml", "frame_width = 4\nframe_height = 64\n[animations]\nidle = { frames = [0] }\n");
        CHECK(HasError(LoadPackage(p.root), "frame_width"));
    }
    SECTION("스크립트 경로 이탈") {
        p.Write("behaviors/behavior.toml", "[script]\nfile = \"../../../x.lua\"\n");
        CHECK(HasError(LoadPackage(p.root), "패키지 밖"));
    }
    SECTION("스크립트 크기 제한") {
        p.Write("behaviors/behavior.toml", "[script]\nfile = \"behaviors/custom.lua\"\n");
        p.Write("behaviors/custom.lua", std::string(300 * 1024, '-'));
        CHECK(HasError(LoadPackage(p.root), "너무 큼"));
    }
    SECTION("문법 오류") {
        p.Write("manifest.toml", "schema_version = [");
        CHECK(HasError(LoadPackage(p.root), "문법"));
    }
}

TEST_CASE("ValidateAtlasSize", "[package]") {
    TempPackage p("atlas");
    p.WriteValid();
    auto r = LoadPackage(p.root);
    REQUIRE(r.package);
    CHECK(ValidateAtlasSize(*r.package, 512, 64).empty());
    CHECK_FALSE(ValidateAtlasSize(*r.package, 192, 64).empty());   // walk 프레임 3 이 범위 밖
    CHECK_FALSE(ValidateAtlasSize(*r.package, 512, 32).empty());   // 높이 불일치
    CHECK_FALSE(ValidateAtlasSize(*r.package, 8192, 64).empty());  // 최대 크기 초과
}

TEST_CASE("AnimationPlayer fallback and timing", "[animation]") {
    SpriteDef s;
    s.clips["idle"] = {{0}, 1, true};
    s.clips["walk"] = {{0, 1, 2, 3}, 8, true};
    AnimationPlayer a;
    a.Play("walk", 10.0);
    CHECK(a.FrameAt(s, 10.0) == 0);
    CHECK(a.FrameAt(s, 10.0 + 1.0 / 8.0 + 1e-6) == 1);
    CHECK(a.FrameAt(s, 10.5 + 1e-6) == 0);  // 4프레임 반복
    CHECK(a.NextChangeAt(s, 10.0) == 10.0 + 1.0 / 8.0);
    a.Play("sit", 11.0);                     // 패키지에 없는 행동 → idle 대체
    CHECK(a.ResolvedClip(s) == "idle");
    CHECK(a.FrameAt(s, 11.5) == 0);
    CHECK(a.NextChangeAt(s, 11.5) == std::numeric_limits<double>::infinity());  // 1프레임 → 다시 그릴 필요 없음
}

namespace {
struct FakeHooks : BehaviorHooks {
    std::optional<BehaviorDecision> rest, event;
    std::optional<BehaviorDecision> OnRest(const BehaviorContext&) override { return rest; }
    std::optional<BehaviorDecision> OnEvent(std::string_view, const BehaviorContext&) override { return event; }
};
}  // namespace

TEST_CASE("BehaviorMachine default and hooks", "[fsm]") {
    BehaviorDef def;
    def.activities = {{"walk", {100, 2, 2}}};
    SECTION("가중치 100% 걷기") {
        BehaviorMachine m(def, 1);
        m.Update(0.0, true, false);
        CHECK(m.Activity() == "walk");
        CHECK((m.WalkDir() == 1 || m.WalkDir() == -1));
        CHECK(m.DecisionAt() == 2.0);
        m.Update(1.0, true, false);  // 아직 결정 시각 전
        CHECK(m.DecisionAt() == 2.0);
    }
    SECTION("낙하 중에는 행동 없음, 착지하면 바로 결정") {
        BehaviorMachine m(def, 1);
        m.Update(0.0, false, false);
        CHECK(m.Activity() == "idle");
        CHECK(m.WalkDir() == 0);
        m.Update(0.5, true, false);
        CHECK(m.Activity() == "walk");
    }
    SECTION("훅 결정 사용, 잘못된 결정은 거부하고 기본값") {
        FakeHooks h;
        h.rest = BehaviorDecision{"sit", 5.0, 0};
        BehaviorMachine m(def, 1, &h);
        m.Update(0.0, true, true);
        CHECK(m.Activity() == "sit");
        h.rest = BehaviorDecision{"fly", 5.0, 0};  // 알 수 없는 행동
        m.Update(10.0, true, true);
        CHECK(m.Activity() == "walk");
        CHECK(m.RejectedHookDecisions() == 1);
    }
    SECTION("클릭 기본 반응") {
        BehaviorMachine m(def, 1);
        m.OnEvent("click", 0.0, false);
        CHECK(m.Activity() == "look");
    }
}

TEST_CASE("IsValidDecision", "[fsm]") {
    CHECK(IsValidDecision({"walk", 2.0, 1}));
    CHECK_FALSE(IsValidDecision({"walk", 2.0, 0}));
    CHECK_FALSE(IsValidDecision({"idle", 2.0, 1}));
    CHECK_FALSE(IsValidDecision({"idle", 0.0, 0}));
    CHECK_FALSE(IsValidDecision({"idle", 1e9, 0}));
    CHECK_FALSE(IsValidDecision({"idle", std::nan(""), 0}));
    // 점프·벽 타기 (선택 행동): 점프는 제자리(0)도 허용, 벽 타기 방향은 물리가 정한다
    CHECK(IsValidDecision({"jump", 1.0, 0}));
    CHECK(IsValidDecision({"jump", 1.0, -1}));
    CHECK_FALSE(IsValidDecision({"jump", 1.0, 2}));
    CHECK(IsValidDecision({"climb", 1.0, 0}));
    CHECK_FALSE(IsValidDecision({"climb", 1.0, 1}));
    CHECK_FALSE(IsValidDecision({"fly", 1.0, 0}));
}

TEST_CASE("Behavior jump and climb activities", "[package][fsm]") {
    TempPackage p("jumpclimb");
    p.WriteValid();
    p.Write("behaviors/behavior.toml", R"(walk_speed = 90.0
jump_height = 3.0
climb_speed = 40.0
[activities]
idle  = { weight = 50, min = 2, max = 4 }
jump  = { weight = 25, min = 1, max = 1 }
climb = { weight = 25, min = 1, max = 2 }
)");
    SECTION("climb 애니메이션이 없으면 벽 타기를 끈다 (경고)") {
        auto r = LoadPackage(p.root);
        REQUIRE(r.package);
        const auto& b = r.package->behavior;
        CHECK(b.activities.contains("jump"));
        CHECK_FALSE(b.activities.contains("climb"));
        CHECK(b.jumpHeight == 3.0);
        CHECK(b.climbSpeed == 40.0);
        bool warned = false;
        for (const auto& w : r.warnings) warned |= w.find("climb") != std::string::npos;
        CHECK(warned);
    }
    SECTION("jump·climb 애니메이션은 선택 행동이라 경고 없이 받는다") {
        p.Write("render/sprite/animations.toml", R"(frame_width = 64
frame_height = 64
[animations]
idle = { frames = [0], fps = 1 }
jump = { frames = [1], fps = 1 }
climb = { frames = [2, 3], fps = 6 }
)");
        auto r = LoadPackage(p.root);
        REQUIRE(r.package);
        CHECK(r.warnings.empty());
        CHECK(r.package->behavior.activities.contains("climb"));
        CHECK(r.package->HasClip("jump"));
        CHECK_FALSE(r.package->HasClip("fly"));
        // FSM 이 jump·climb 을 고르고, jump 방향은 -1/0/+1
        BehaviorMachine m(r.package->behavior, 7);
        bool sawJump = false, sawClimb = false;
        double now = 0;
        for (int i = 0; i < 400; ++i) {
            now = m.DecisionAt() + 0.01;
            m.Update(now, true, false);
            if (m.Activity() == "jump") {
                sawJump = true;
                CHECK(m.WalkDir() >= -1);
                CHECK(m.WalkDir() <= 1);
            }
            if (m.Activity() == "climb") {
                sawClimb = true;
                CHECK(m.WalkDir() == 0);
            }
        }
        CHECK(sawJump);
        CHECK(sawClimb);
    }
    SECTION("범위 밖 값은 기본값 + 경고, 예전 climb = true 는 무시") {
        p.Write("behaviors/behavior.toml", "jump_height = 50.0\nclimb_speed = 5.0\nclimb = true\n");
        auto r = LoadPackage(p.root);
        REQUIRE(r.package);
        CHECK(r.package->behavior.jumpHeight == 2.5);
        CHECK(r.package->behavior.climbSpeed == 0);
        CHECK(r.warnings.size() >= 3);
    }
}

TEST_CASE("Shipped test characters load", "[package]") {
    const fs::path chars = fs::path(DP_SOURCE_DIR) / "characters";
    auto dirs = FindPackageDirs({chars});
    REQUIRE(dirs.size() >= 2);
    for (const char* id : {"test-orange", "test-blue"}) {
        auto r = LoadPackage(chars / id);
        INFO(id);
        for (const auto& e : r.errors) UNSCOPED_INFO(e);
        REQUIRE(r.package);
        CHECK(r.errors.empty());
        CHECK(r.package->id == id);
        CHECK(r.package->license.name == "CC0-1.0");
        for (const auto& a : CommonActions()) CHECK(r.package->sprite.clips.count(a) == 1);
        CHECK(ValidateAtlasSize(*r.package, 512, 64).empty());
    }
    auto blue = LoadPackage(chars / "test-blue");
    CHECK_FALSE(blue.package->behavior.scriptSource.empty());
    CHECK(blue.package->behavior.walkSpeed > 100);
    auto orange = LoadPackage(chars / "test-orange");
    CHECK(orange.package->behavior.scriptSource.empty());
}

TEST_CASE("Sample character windowling loads", "[package]") {
    // 릴리스에 따로 올리는 예제 캐릭터 (기본 설정의 펫). 128px 프레임 18개, 점프·벽 타기·펫끼리 상호작용
    auto r = LoadPackage(fs::path(DP_SOURCE_DIR) / "characters" / "windowling");
    for (const auto& e : r.errors) UNSCOPED_INFO(e);
    REQUIRE(r.package);
    CHECK(r.errors.empty());
    CHECK(r.warnings.empty());
    CHECK(r.package->id == "windowling");
    CHECK(r.package->license.name == "MIT");
    CHECK_FALSE(r.package->persona.empty());
    CHECK(r.package->sprite.frameWidth == 128);
    for (const auto& a : CommonActions()) CHECK(r.package->sprite.clips.count(a) == 1);
    CHECK(r.package->sprite.clips.count("talk") == 1);
    for (const char* a : {"jump", "climb", "highfive", "lean"}) CHECK(r.package->HasClip(a));
    const auto& beh = r.package->behavior;
    CHECK((beh.greet && beh.bump && beh.lean));
    for (const char* a : {"jump", "climb", "follow", "train"}) CHECK(beh.activities.contains(a));
    CHECK(ValidateAtlasSize(*r.package, 128 * 18, 128).empty());
    CHECK_FALSE(ValidateAtlasSize(*r.package, 128 * 17, 128).empty());  // 프레임 17 을 쓰므로 17개로는 모자람
    CHECK(Config{}.pets.front().character == r.package->id);
}

TEST_CASE("FindPackageDirs keeps root order", "[package]") {
    TempPackage a("roota"), b("rootb");
    for (auto* t : {&a, &b}) {
        fs::create_directories(t->root / "pet-x");
        std::ofstream(t->root / "pet-x" / "manifest.toml") << "schema_version = 1\n";
        fs::create_directories(t->root / "no-manifest");
    }
    auto dirs = FindPackageDirs({a.root, b.root, a.root / "missing"});
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == a.root / "pet-x");
    CHECK(dirs[1] == b.root / "pet-x");  // 뒤 경로가 나중 → 호출 측에서 우선
}


TEST_CASE("Rest between activities and social keys", "[fsm][package]") {
    TempPackage p("rest");
    p.WriteValid();
    p.Write("behaviors/behavior.toml", R"(rest_between = 1.0
greet = true
[activities]
walk   = { weight = 50, min = 1, max = 2 }
follow = { weight = 50, min = 1, max = 2 }
)");
    auto r = LoadPackage(p.root);
    REQUIRE(r.package);
    const auto& def = r.package->behavior;
    CHECK(def.restBetween == 1.0);
    CHECK(def.greet);
    CHECK(def.activities.contains("follow"));
    CHECK(IsValidDecision({"follow", 1.0, 0}));
    CHECK(IsValidDecision({"rest", 1.0, 0}));

    BehaviorMachine m(def, 3);
    double now = 0;
    std::string last;
    int rests = 0, others = 0;
    for (int i = 0; i < 60; ++i) {
        now = m.DecisionAt() + 0.01;
        m.Update(now, true, false);
        const std::string& a = m.Activity();
        if (a == "rest") {
            rests++;
            const double len = m.DecisionAt() - now;
            CHECK(len >= 0.79);
            CHECK(len <= 1.51);
            CHECK(last != "rest");  // 쉼이 연달아 오지 않는다
        } else {
            others++;
            CHECK(last == "rest");  // 행동 사이에는 항상 쉼
        }
        last = a;
    }
    CHECK(rests >= 25);
    CHECK(others >= 25);
    m.OnEvent("landed", now, false);
    CHECK(m.Activity() == "rest");

    SECTION("rest_between = 0 이면 예전처럼 (landed → idle 1초)") {
        BehaviorDef d0 = def;
        d0.restBetween = 0;
        BehaviorMachine m0(d0, 3);
        m0.OnEvent("landed", 5.0, false);
        CHECK(m0.Activity() == "idle");
        CHECK(m0.DecisionAt() == 6.0);
    }
}

TEST_CASE("Behavior bump, lean and train keys", "[package]") {
    TempPackage p("social2");
    p.WriteValid();
    p.Write("behaviors/behavior.toml", "bump = true\nlean = true\n[activities]\nwalk = { weight = 50, min = 1, max = 2 }\ntrain = { weight = 10, min = 1, max = 1 }\n");
    p.Write("render/sprite/animations.toml", "frame_width = 64\nframe_height = 64\n[animations]\nidle = { frames = [0], fps = 1 }\nhighfive = { frames = [1, 2], fps = 6 }\nlean = { frames = [3], fps = 1 }\n");
    auto r = LoadPackage(p.root);
    REQUIRE(r.package);
    CHECK(r.warnings.empty());
    CHECK(r.package->behavior.bump);
    CHECK(r.package->behavior.lean);
    CHECK(r.package->behavior.activities.contains("train"));
    CHECK(r.package->HasClip("highfive"));
    CHECK(IsValidDecision({"train", 1.0, 0}));
}

TEST_CASE("Behavior bug_hunt key and bug clip", "[package]") {
    TempPackage p("bughunt");
    p.WriteValid();
    p.Write("behaviors/behavior.toml", "bug_hunt = true\n[activities]\nwalk = { weight = 50, min = 1, max = 2 }\n");
    p.Write("render/sprite/animations.toml", "frame_width = 64\nframe_height = 64\n[animations]\nidle = { frames = [0], fps = 1 }\nbug = { frames = [1, 2], fps = 10 }\n");
    auto r = LoadPackage(p.root);
    REQUIRE(r.package);
    CHECK(r.warnings.empty());
    CHECK(r.package->behavior.bugHunt);
    CHECK(r.package->HasClip("bug"));

    TempPackage q("nobughunt");
    q.WriteValid();
    auto r2 = LoadPackage(q.root);
    REQUIRE(r2.package);
    CHECK_FALSE(r2.package->behavior.bugHunt);
}
