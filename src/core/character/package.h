#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dp::core {

// 캐릭터 패키지 규격 schema_version = 1 (P2 계획 §2.1, 기준선 §6)
// 패키지는 읽기 전용 에셋이다. 인스턴스 상태(위치 등)는 여기 두지 않는다 (기준선 §6.2, ADR-04).

struct AnimationClip {
    std::vector<int> frames;  // 시트 내 프레임 번호
    double fps = 8.0;
    bool loop = true;
};

struct SpriteDef {
    std::filesystem::path atlas;   // 정규화된 절대 경로 (패키지 안)
    int frameWidth = 64, frameHeight = 64;
    int baseSize = 128;            // 100% 배율 표시 크기 (px, 정사각)
    std::map<std::string, AnimationClip> clips;
};

struct ActivityDef {
    double weight = 0;
    double minSec = 1, maxSec = 1;
};

struct BehaviorDef {
    double walkSpeed = 70.0;        // px/s @96DPI
    double footSupportRatio = 0.5;  // P1-D1
    double jumpHeight = 2.5;        // 더 높은 창 위로 뛸 때 최대 높이 (펫 키의 배수)
    double climbSpeed = 0;          // 벽 오르기 속도 px/s @96DPI (0 = walk_speed × 0.6)
    double restBetween = 0;         // 행동과 행동 사이에 가만히 서 있는 시간 (초, 0 = 없음). 실제는 0.8~1.5배
    bool greet = false;             // 다른 펫과 마주치면 인사 (말풍선). highfive 애니메이션이 있으면 가끔 하이파이브
    bool bump = false;              // 걷다 마주치면 서로 튕겨 물러남
    bool lean = false;              // 둘 다 앉아 있으면 다가가 붙어 앉음
    double headHeight = 0.8;        // 머리 꼭대기 높이 (키의 비율, 발에서). 다른 펫이 올라탈 자리
    bool bugHunt = false;           // 가끔 나타나는 버그를 쫓아가 잡음 (bug 애니메이션이 있어야 버그가 나타남)
    std::map<std::string, ActivityDef> activities;  // idle / walk / sit / jump / climb / follow / train / visit / race / stack
    std::filesystem::path script;   // 비어 있으면 Lua 없음
    std::string scriptSource;       // 로드된 Lua 소스
};

// 3D 모델 (P5, renderers 에 "gltf"). 물리·행동은 스프라이트와 같다 (ADR-03: 표시만 3D)
struct ModelDef {
    std::filesystem::path path;   // .glb (정규화된 절대 경로, 패키지 안)
    int baseHeight = 128;         // 100% 배율 화면 높이 (px). 너비는 모델 비율
    double yawDeg = 35;           // 기본 시점 (오른쪽을 볼 때). 왼쪽을 볼 때는 -yaw
    std::map<std::string, std::string> animations;  // 공통 행동 → glTF 애니메이션 이름
};

// 캐릭터 음성 (선택, P4 B12 결정). 없거나 설치되지 않은 음성이면 전역 [tts] 값을 쓴다
struct VoiceDef {
    std::string name;     // Windows 음성 표시 이름 일부 (예: "Heami")
    std::string aiVoice;  // 로컬 AI TTS 엔진의 voice id (HTTP 엔진의 "voice")
    double rate = 0;      // 0 = 전역 값. 0.5~2.0
};

struct LicenseInfo {
    std::string name, author, source;
};

struct CharacterPackage {
    std::filesystem::path root;
    std::string id, name, version;
    LicenseInfo license;
    std::vector<std::string> renderers;
    SpriteDef sprite;     // renderers 에 "sprite" 일 때
    ModelDef model;       // renderers 에 "gltf" 일 때 (P5). 둘 다 있으면 모델 우선
    bool IsModel() const { return !model.path.empty(); }
    // 그 행동의 애니메이션이 있는가 (모델은 [model.animations], 스프라이트는 [animations])
    bool HasClip(const std::string& action) const {
        return IsModel() ? model.animations.contains(action) : sprite.clips.contains(action);
    }
    BehaviorDef behavior;
    std::string persona;  // persona.md (선택, P3): AI 대화의 성격·말투. 권한에는 영향 없음
    VoiceDef voice;       // manifest [voice] (선택, P4)
};

// P2-R7 크기 제한 (미확정 초기값)
struct PackageLimits {
    uintmax_t maxFileBytes = 16ull * 1024 * 1024;
    uintmax_t maxScriptBytes = 256ull * 1024;
    uintmax_t maxTomlBytes = 256ull * 1024;
    uintmax_t maxPersonaBytes = 16ull * 1024;  // P3-R6
    uintmax_t maxModelBytes = 32ull * 1024 * 1024;  // P5-R6 (glb)
    int minFrame = 16, maxFrame = 512;
    int maxAtlasDim = 4096;
    int maxFramesPerClip = 256;
};

struct PackageLoadResult {
    std::optional<CharacterPackage> package;
    std::vector<std::string> errors;    // 하나라도 있으면 package 없음 (D12: 로드하지 않음)
    std::vector<std::string> warnings;
};

// 공통 행동 이름 (기준선 §5.1 + 근접·클릭 반응 look)
const std::vector<std::string>& CommonActions();
// 선택 행동 애니메이션: 없어도 되고, 있으면 쓴다 (jump 가 없으면 fall, climb 이 없으면 벽 타기를 하지 않음)
const std::vector<std::string>& OptionalActions();

// root 아래 상대 경로를 정규화해 root 안에 있으면 절대 경로를 돌려준다.
// 절대 경로, 드라이브 지정, '..' 로 밖으로 나가는 경로는 거부 (기준선 §6.2 경로 정규화)
std::optional<std::filesystem::path> ResolveInside(const std::filesystem::path& root, const std::string& relative);

// 폴더 패키지 로드·검증 (P2-R1: 폴더만). 이미지 크기 검증은 디코딩 후 ValidateAtlasSize 로 따로 한다.
PackageLoadResult LoadPackage(const std::filesystem::path& root, const PackageLimits& limits = {});

// 디코딩된 아틀라스 크기와 애니메이션 정의가 맞는지 (가로 배열 시트)
std::vector<std::string> ValidateAtlasSize(const CharacterPackage& pkg, int width, int height,
                                           const PackageLimits& limits = {});

// 검색 경로들에서 패키지 폴더 목록 (manifest.toml 이 있는 하위 폴더). 경로 순서 유지 —
// 같은 id 는 호출 측에서 뒤 경로가 우선하고, 뒤 패키지가 깨졌으면 앞 것이 남는다 (P2-R8)
std::vector<std::filesystem::path> FindPackageDirs(const std::vector<std::filesystem::path>& searchRoots);

}  // namespace dp::core
