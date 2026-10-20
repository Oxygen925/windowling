// P5: glTF 로드·애니메이션 포즈·3D 패키지 규격 (Windows·GPU 비의존)
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "core/character/package.h"
#include "model/gltf_model.h"

using namespace dp;
using Catch::Approx;

namespace {
const std::filesystem::path kFoxDir = std::filesystem::path(DP_SOURCE_DIR) / "characters" / "test-fox";
}

TEST_CASE("Fox glb loads with skin, animations and generated normals", "[model]") {
    model::Model m;
    REQUIRE(model::LoadGlb(kFoxDir / "render" / "model" / "Fox.glb", {}, m).empty());
    CHECK(m.skinJoints.size() == 24);
    CHECK(m.triangles == 576);
    REQUIRE(m.animations.size() == 3);
    CHECK(m.FindAnimation("Walk") >= 0);
    CHECK(m.FindAnimation("Sit") == -1);
    REQUIRE(m.primitives.size() == 1);
    const auto& v = m.primitives[0].vertices.front();
    CHECK(std::abs(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2] - 1.0f) < 1e-3f);
    CHECK(m.images.size() == 1);
    CHECK(m.images[0].mimeType == "image/png");

    // 기본 자세: 위로 서 있다 (높이 > 0), yaw 를 돌리면 너비가 바뀐다
    const auto front = model::RestBounds(m, 0);
    const auto side = model::RestBounds(m, 1.5707963f);
    CHECK(front.Height() > 10);
    CHECK(side.Width() > front.Width());

    // 애니메이션은 시간에 따라 포즈가 바뀌고, 길이로 반복한다
    const int walk = m.FindAnimation("Walk");
    std::vector<model::Mat4> w0, w1, w2;
    model::EvaluateWorld(m, walk, 0.0, w0);
    model::EvaluateWorld(m, walk, 0.3, w1);
    model::EvaluateWorld(m, walk, m.animations[walk].duration + 0.3, w2);
    float diff = 0, loopDiff = 0;
    for (size_t i = 0; i < w0.size(); ++i)
        for (int k = 0; k < 16; ++k) {
            diff += std::abs(w0[i].m[k] - w1[i].m[k]);
            loopDiff += std::abs(w1[i].m[k] - w2[i].m[k]);
        }
    CHECK(diff > 1.0f);
    CHECK(loopDiff < 1e-2f);

    // 제한을 넘으면 거부
    model::ModelLimits small;
    small.maxTriangles = 100;
    CHECK_FALSE(model::LoadGlb(kFoxDir / "render" / "model" / "Fox.glb", small, m).empty());
}

TEST_CASE("Animation sampling interpolates and steps", "[model]") {
    model::Model m;
    m.nodes.resize(1);
    model::Animation a;
    a.name = "move";
    model::Channel c;
    c.node = 0;
    c.path = model::Channel::Path::T;
    c.times = {0.0f, 1.0f};
    c.values = {0, 0, 0, 10, 0, 0};
    a.channels.push_back(c);
    a.duration = 1.0f;
    m.animations.push_back(a);
    std::vector<model::Mat4> w;
    model::EvaluateWorld(m, 0, 0.25, w);
    CHECK(w[0].m[12] == Approx(2.5f));
    m.animations[0].channels[0].step = true;
    model::EvaluateWorld(m, 0, 0.75, w);
    CHECK(w[0].m[12] == Approx(0.0f));
    model::EvaluateWorld(m, -1, 0.75, w);  // 애니메이션 없음 = 기본 자세
    CHECK(w[0].m[12] == Approx(0.0f));
}

TEST_CASE("3D character package", "[package]") {
    auto r = core::LoadPackage(kFoxDir);
    REQUIRE(r.package);
    CHECK(r.package->IsModel());
    CHECK(r.package->model.baseHeight == 80);
    CHECK(r.package->model.animations.at("walk") == "Walk");
    CHECK(r.package->sprite.clips.empty());
    // 기존 스프라이트 패키지는 그대로
    auto s = core::LoadPackage(std::filesystem::path(DP_SOURCE_DIR) / "characters" / "test-blue");
    REQUIRE(s.package);
    CHECK_FALSE(s.package->IsModel());
}

// 보안 점검 (2026-10-20): 정점·삼각형 개수는 할당 전에 거부
TEST_CASE("Model vertex and triangle limits are checked before allocation", "[model][security]") {
    model::Model m;
    model::ModelLimits small;
    small.maxVertices = 100;
    CHECK(model::LoadGlb(kFoxDir / "render" / "model" / "Fox.glb", small, m).find("정점") != std::string::npos);
    model::ModelLimits fewTriangles;
    fewTriangles.maxTriangles = 100;
    model::Model m2;
    CHECK(model::LoadGlb(kFoxDir / "render" / "model" / "Fox.glb", fewTriangles, m2).find("삼각형") != std::string::npos);
}
