#pragma once
// glTF 2.0 (.glb) 모델 데이터와 애니메이션 포즈 계산 (P5-02·P5-03). Windows·GPU 비의존 → 단위 테스트 가능.
// 지원: 삼각형 메시, 스킨 1개(관절 4개/정점), TRS 애니메이션(LINEAR·STEP, CUBICSPLINE 은 값만 써서 LINEAR 근사 [검증]),
//       base color 텍스처·계수, alphaMode. 법선이 없으면 면 법선을 만든다(glTF 규정: flat shading).
// 미지원(경고 후 무시): morph target, 카메라·조명, 확장. 외부 버퍼·이미지 파일(.gltf + .bin) 은 받지 않는다(.glb 만).
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dp::model {

// 열 우선(column-major) 4x4, glTF 와 같은 배치. m[c*4 + r]
struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static Mat4 Identity() { return {}; }
    static Mat4 FromTrs(const std::array<float, 3>& t, const std::array<float, 4>& q, const std::array<float, 3>& s);
    static Mat4 RotationY(float radians);
    Mat4 operator*(const Mat4& o) const;
    std::array<float, 3> TransformPoint(const std::array<float, 3>& p) const;
};

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    uint16_t joints[4];
    float weights[4];
};

enum class AlphaMode { Opaque, Mask, Blend };

struct Material {
    std::array<float, 4> baseColor{1, 1, 1, 1};
    int image = -1;               // images 인덱스 (-1 = 텍스처 없음)
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool linearFilter = true;     // 샘플러 mag 필터
};

struct Image {
    std::string mimeType;         // image/png, image/jpeg
    std::vector<uint8_t> bytes;   // 인코딩된 그대로 (디코딩은 플랫폼 계층: WIC)
};

struct Primitive {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;  // 항상 채운다 (원본에 없으면 0..n-1)
    int material = -1;
    int node = -1;                  // 이 메시가 붙은 노드
    bool skinned = false;           // false 면 joints=0, weights=(1,0,0,0) 에 palette[0]=노드 월드 행렬
};

struct Node {
    std::string name;
    int parent = -1;
    std::array<float, 3> t{0, 0, 0};
    std::array<float, 4> r{0, 0, 0, 1};  // x y z w
    std::array<float, 3> s{1, 1, 1};
    bool hasMatrix = false;
    Mat4 matrix;
};

struct Channel {
    int node = -1;
    enum class Path { T, R, S } path = Path::T;
    bool step = false;
    std::vector<float> times;
    std::vector<float> values;  // T·S: 3개씩, R: 4개씩 (CUBICSPLINE 은 가운데 값만 남김)
};

struct Animation {
    std::string name;
    float duration = 0;
    std::vector<Channel> channels;
};

struct Bounds {
    std::array<float, 3> min{0, 0, 0}, max{0, 0, 0};
    float Width() const { return max[0] - min[0]; }
    float Height() const { return max[1] - min[1]; }
};

struct ModelLimits {
    size_t maxTriangles = 50000;   // P5-R6
    size_t maxVertices = 150000;   // 할당 전에 검사 (보안 점검 2026-10-20: 삼각형 수는 펼친 뒤에야 알 수 있었다)
    size_t maxJoints = 128;
    size_t maxAnimations = 32;
    size_t maxImages = 8;
};

struct Model {
    std::vector<Node> nodes;
    std::vector<Primitive> primitives;
    std::vector<int> skinJoints;       // 노드 인덱스
    std::vector<Mat4> inverseBind;
    std::vector<Animation> animations;
    std::vector<Material> materials;
    std::vector<Image> images;
    std::vector<std::string> warnings;
    size_t triangles = 0;

    int FindAnimation(const std::string& name) const;  // 없으면 -1
};

// 성공하면 빈 문자열, 실패하면 오류 문구
std::string LoadGlb(const std::filesystem::path& path, const ModelLimits& limits, Model& out);

// 포즈: 애니메이션(없으면 -1 = 기본 자세) 의 time(초, 반복) 에서 노드 월드 행렬
void EvaluateWorld(const Model& model, int animation, double timeSec, std::vector<Mat4>& world);
// 스킨 행렬 팔레트: palette[j] = world[joint j] * inverseBind[j]. 스킨이 없으면 비어 있음
void SkinPalette(const Model& model, const std::vector<Mat4>& world, std::vector<Mat4>& palette);
// 기본 자세(애니메이션 없음)에서 스킨까지 적용한 정점 바운딩 박스. yaw(라디안, Y 축) 를 먼저 적용
Bounds RestBounds(const Model& model, float yawRadians);

}  // namespace dp::model
