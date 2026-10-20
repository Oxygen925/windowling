// 캐릭터 패키지 검증 도구 (A6): 앱이 읽는 것과 같은 규칙으로 패키지 폴더를 검사한다 — 외부 캐릭터 제작자용.
//   pack-check <패키지 폴더> [...]
// 검사: manifest·animations·behavior TOML, 경로 이탈, 크기 제한, 아틀라스 디코딩·프레임 범위, persona.md,
//       3D(.glb, P5): 앱과 같은 로더·제한(삼각형·관절·애니메이션), 행동 매핑의 애니메이션 이름,
//       Lua 스크립트 로드(앱과 같은 샌드박스·명령 수·메모리 제한). 오류가 있으면 종료 코드 1.
#include <windows.h>

#include <objbase.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "core/character/package.h"
#include "model/gltf_model.h"
#include "platform/windows/sprite_sheet.h"
#include "platform/windows/strings.h"
#include "script/lua_behavior.h"

namespace fs = std::filesystem;
using namespace dp;

namespace {

int CheckOne(const fs::path& dir) {
    std::printf("== %s\n", ToUtf8(dir.wstring()).c_str());
    auto r = core::LoadPackage(dir);
    std::vector<std::string> errors = r.errors;
    std::string luaError;  // 앱은 패키지를 읽고 Lua 만 끈다 (기본 행동으로 실행)
    if (r.package) {
        const auto& pkg = *r.package;
        SpriteSheet sheet;
        model::Model mdl;
        if (pkg.IsModel()) {
            if (const std::string e = model::LoadGlb(pkg.model.path, {}, mdl); !e.empty()) errors.push_back("모델: " + e);
            for (const auto& w : mdl.warnings) r.warnings.push_back("모델: " + w);
            for (const auto& [action, name] : pkg.model.animations)
                if (errors.empty() && mdl.FindAnimation(name) < 0)
                    r.warnings.push_back("모델: 애니메이션 '" + name + "' 없음 (행동 " + action + ") → idle 로 대체");
        } else if (!sheet.Load(pkg.sprite.atlas.wstring(), pkg.sprite.frameWidth)) {
            errors.push_back("아틀라스 PNG 디코딩 실패");
        } else {
            auto e = core::ValidateAtlasSize(pkg, sheet.Width(), sheet.Height());
            errors.insert(errors.end(), e.begin(), e.end());
        }
        if (!pkg.behavior.scriptSource.empty()) {
            std::string err;
            auto lua = script::LuaBehavior::Create(pkg.behavior.scriptSource, pkg.id + "/custom.lua", 1, {}, err);
            if (!lua) luaError = err;
        }
        if (errors.empty() && luaError.empty()) {
            std::printf("  id=%s name=%s version=%s license=%s author=%s\n", pkg.id.c_str(), pkg.name.c_str(),
                        pkg.version.c_str(), pkg.license.name.c_str(), pkg.license.author.c_str());
            if (pkg.IsModel()) {
                std::printf("  3D: triangles=%zu joints=%zu animations=%zu base_height=%d yaw=%.0f lua=%s persona=%s\n",
                            mdl.triangles, mdl.skinJoints.size(), mdl.animations.size(), pkg.model.baseHeight,
                            pkg.model.yawDeg, pkg.behavior.scriptSource.empty() ? "없음" : "있음",
                            pkg.persona.empty() ? "없음" : "있음");
                for (const auto& a : core::CommonActions())
                    if (!pkg.model.animations.contains(a)) std::printf("  참고: 공통 행동 '%s' 매핑 없음 → idle 로 대체\n", a.c_str());
            } else {
                std::printf("  frames=%dx%d base_size=%d clips=%zu lua=%s persona=%s\n", pkg.sprite.frameWidth,
                            pkg.sprite.frameHeight, pkg.sprite.baseSize, pkg.sprite.clips.size(),
                            pkg.behavior.scriptSource.empty() ? "없음" : "있음", pkg.persona.empty() ? "없음" : "있음");
                for (const auto& a : core::CommonActions())
                    if (!pkg.sprite.clips.contains(a)) std::printf("  참고: 공통 행동 '%s' 클립 없음 → idle 로 대체\n", a.c_str());
            }
        }
    }
    for (const auto& w : r.warnings) std::printf("  경고: %s\n", w.c_str());
    for (const auto& e : errors) std::printf("  오류: %s\n", e.c_str());
    if (!luaError.empty()) std::printf("  오류: Lua 로드 실패: %s\n", luaError.c_str());
    if (!errors.empty()) std::printf("  결과: 실패 (앱은 이 패키지를 읽지 않음)\n");
    else if (!luaError.empty()) std::printf("  결과: 실패 (앱은 패키지를 읽지만 Lua 를 끄고 기본 행동으로 실행)\n");
    else std::printf("  결과: 통과\n");
    return errors.empty() && luaError.empty() ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // WIC (아틀라스 디코딩)
    if (argc < 2) {
        std::printf("사용: pack-check <패키지 폴더> [...]\n");
        return 2;
    }
    int failed = 0;
    for (int i = 1; i < argc; ++i) failed += CheckOne(fs::path(argv[i]));
    return failed ? 1 : 0;
}
