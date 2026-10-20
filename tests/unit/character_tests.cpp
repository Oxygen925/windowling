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
    // 릴리스에 따로 올리는 예제 캐릭터 (기본 설정의 펫). 128px 프레임 11개
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
    CHECK(ValidateAtlasSize(*r.package, 128 * 11, 128).empty());
    CHECK_FALSE(ValidateAtlasSize(*r.package, 128 * 10, 128).empty());  // 프레임 10 을 쓰므로 10개로는 모자람
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
