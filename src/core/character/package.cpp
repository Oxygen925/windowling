#include "core/character/package.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include <toml++/toml.hpp>

namespace dp::core {

namespace fs = std::filesystem;

const std::vector<std::string>& CommonActions() {
    static const std::vector<std::string> v = {"idle", "walk", "sit", "fall", "drag", "look", "talk"};
    return v;
}

const std::vector<std::string>& OptionalActions() {
    static const std::vector<std::string> v = {"jump", "climb", "rest", "highfive", "lean", "peek", "ride", "bug"};
    return v;
}

std::optional<fs::path> ResolveInside(const fs::path& root, const std::string& relative) {
    if (relative.empty()) return std::nullopt;
    const fs::path rel(std::u8string(relative.begin(), relative.end()));  // UTF-8 (TOML 문자열)
    if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) return std::nullopt;
    const fs::path base = fs::absolute(root).lexically_normal();
    const fs::path full = (base / rel).lexically_normal();
    // base 의 모든 구성 요소가 full 의 앞부분과 같아야 한다 ('..' 로 빠져나가면 실패)
    auto b = base.begin(), f = full.begin();
    for (; b != base.end(); ++b, ++f) {
        if (b->empty() && std::next(b) == base.end()) break;  // 끝 구분자
        if (f == full.end() || *b != *f) return std::nullopt;
    }
    if (full == base) return std::nullopt;  // 폴더 자체는 파일이 아니다
    return full;
}

namespace {

bool ReadText(const fs::path& path, uintmax_t limit, std::string& out, std::vector<std::string>& errors,
              const std::string& label) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        errors.push_back(label + " 파일 없음: " + path.filename().string());
        return false;
    }
    if (size > limit) {
        errors.push_back(label + " 파일이 너무 큼 (" + std::to_string(size) + " bytes)");
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::optional<toml::table> ParseToml(const fs::path& path, uintmax_t limit, std::vector<std::string>& errors,
                                     const std::string& label) {
    std::string text;
    if (!ReadText(path, limit, text, errors, label)) return std::nullopt;
    try {
        return toml::parse(text);
    } catch (const toml::parse_error& e) {
        errors.push_back(label + " 문법 오류: " + std::string(e.description()));
        return std::nullopt;
    }
}

bool ValidId(const std::string& id) {
    if (id.size() < 3 || id.size() > 40) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

std::string Str(const toml::table& t, std::string_view key) { return t[key].value_or(std::string()); }

void ParseAnimations(const toml::table& t, const PackageLimits& lim, SpriteDef& sprite, std::vector<std::string>& errors,
                     std::vector<std::string>& warnings) {
    sprite.frameWidth = static_cast<int>(t["frame_width"].value_or<int64_t>(0));
    sprite.frameHeight = static_cast<int>(t["frame_height"].value_or<int64_t>(0));
    auto frameOk = [&](int v) { return v >= lim.minFrame && v <= lim.maxFrame; };
    if (!frameOk(sprite.frameWidth) || !frameOk(sprite.frameHeight)) {
        errors.push_back("animations: frame_width/frame_height 는 " + std::to_string(lim.minFrame) + "~" +
                         std::to_string(lim.maxFrame) + " 범위여야 함");
        return;
    }
    const toml::table* clips = t["animations"].as_table();
    if (!clips) {
        errors.push_back("animations: [animations] 표 없음");
        return;
    }
    for (const auto& [name, node] : *clips) {
        const std::string n(name.str());
        const toml::table* c = node.as_table();
        const toml::array* frames = c ? (*c)["frames"].as_array() : nullptr;
        if (!frames || frames->empty() || static_cast<int>(frames->size()) > lim.maxFramesPerClip) {
            errors.push_back("animations." + n + ": frames 배열이 없거나 너무 김");
            continue;
        }
        AnimationClip clip;
        for (const auto& f : *frames) {
            auto v = f.value<int64_t>();
            if (!v || *v < 0) {
                errors.push_back("animations." + n + ": 프레임 번호가 음수이거나 정수가 아님");
                break;
            }
            clip.frames.push_back(static_cast<int>(*v));
        }
        clip.fps = (*c)["fps"].value_or(8.0);
        clip.loop = (*c)["loop"].value_or(true);
        if (clip.fps <= 0 || clip.fps > 60) {
            warnings.push_back("animations." + n + ": fps 범위 밖 → 8");
            clip.fps = 8;
        }
        if (std::find(CommonActions().begin(), CommonActions().end(), n) == CommonActions().end() &&
            std::find(OptionalActions().begin(), OptionalActions().end(), n) == OptionalActions().end())
            warnings.push_back("animations." + n + ": 공통 행동이 아님 (사용되지 않음)");
        sprite.clips[n] = std::move(clip);
    }
    if (!sprite.clips.contains("idle")) errors.push_back("animations: 필수 행동 idle 없음 (누락 행동의 대체 기준)");
}

void ParseBehavior(const toml::table& t, const fs::path& root, const PackageLimits& lim, BehaviorDef& b,
                   std::vector<std::string>& errors, std::vector<std::string>& warnings) {
    b.walkSpeed = t["walk_speed"].value_or(b.walkSpeed);
    if (b.walkSpeed < 0 || b.walkSpeed > 1000) {
        warnings.push_back("behavior.walk_speed 범위 밖 → 70");
        b.walkSpeed = 70;
    }
    b.footSupportRatio = t["foot_support_ratio"].value_or(b.footSupportRatio);
    if (b.footSupportRatio < 0.05 || b.footSupportRatio > 1.0) {
        warnings.push_back("behavior.foot_support_ratio 범위 밖 → 0.5");
        b.footSupportRatio = 0.5;
    }
    if (t.contains("climb")) warnings.push_back("behavior.climb 은 쓰지 않음 — [activities] climb 비중으로 켠다 (무시)");
    b.jumpHeight = t["jump_height"].value_or(b.jumpHeight);
    if (!(b.jumpHeight >= 0.5 && b.jumpHeight <= 6.0)) {
        warnings.push_back("behavior.jump_height 범위 밖(0.5~6) → 2.5");
        b.jumpHeight = 2.5;
    }
    b.restBetween = t["rest_between"].value_or(b.restBetween);
    if (!(b.restBetween >= 0 && b.restBetween <= 5)) {
        warnings.push_back("behavior.rest_between 범위 밖(0~5) → 0");
        b.restBetween = 0;
    }
    b.greet = t["greet"].value_or(false);
    b.bump = t["bump"].value_or(false);
    b.lean = t["lean"].value_or(false);
    b.bugHunt = t["bug_hunt"].value_or(false);
    b.headHeight = t["head_height"].value_or(b.headHeight);
    if (!(b.headHeight >= 0.1 && b.headHeight <= 1.0)) {
        warnings.push_back("behavior.head_height 범위 밖(0.1~1) → 0.8");
        b.headHeight = 0.8;
    }
    b.climbSpeed = t["climb_speed"].value_or(b.climbSpeed);
    if (!(b.climbSpeed == 0 || (b.climbSpeed >= 10 && b.climbSpeed <= 1000))) {
        warnings.push_back("behavior.climb_speed 범위 밖(10~1000) → 기본값");
        b.climbSpeed = 0;
    }
    if (const toml::table* acts = t["activities"].as_table()) {
        for (const auto& [name, node] : *acts) {
            const std::string n(name.str());
            if (n != "idle" && n != "walk" && n != "sit" && n != "jump" && n != "climb" && n != "follow" && n != "train" &&
                n != "visit" && n != "race" && n != "stack") {
                warnings.push_back("behavior.activities." + n + ": 알 수 없는 행동 (무시)");
                continue;
            }
            const toml::table* a = node.as_table();
            if (!a) continue;
            ActivityDef d;
            d.weight = (*a)["weight"].value_or(0.0);
            d.minSec = (*a)["min"].value_or(1.0);
            d.maxSec = (*a)["max"].value_or(d.minSec);
            if (d.weight < 0 || d.minSec < 0.1 || d.maxSec < d.minSec || d.maxSec > 600) {
                warnings.push_back("behavior.activities." + n + ": 값 범위 밖 (무시)");
                continue;
            }
            b.activities[n] = d;
        }
    }
    if (const toml::table* s = t["script"].as_table()) {
        const std::string file = Str(*s, "file");
        if (!file.empty()) {
            auto p = ResolveInside(root, file);
            if (!p) {
                errors.push_back("behavior.script.file 이 패키지 밖을 가리킴: " + file);
                return;
            }
            if (p->extension() != ".lua") {
                errors.push_back("behavior.script.file 은 .lua 여야 함");
                return;
            }
            b.script = *p;
            ReadText(*p, lim.maxScriptBytes, b.scriptSource, errors, "Lua 스크립트");
        }
    }
}

}  // namespace

namespace {
void FinishPackage(CharacterPackage pkg, const PackageLimits& lim, PackageLoadResult& r);
}  // namespace

PackageLoadResult LoadPackage(const fs::path& root, const PackageLimits& lim) {
    PackageLoadResult r;
    auto& errors = r.errors;
    CharacterPackage pkg;
    pkg.root = fs::absolute(root).lexically_normal();

    auto manifest = ParseToml(pkg.root / "manifest.toml", lim.maxTomlBytes, errors, "manifest.toml");
    if (!manifest) return r;
    const toml::table& m = *manifest;
    const int64_t schema = m["schema_version"].value_or<int64_t>(0);
    if (schema != 1) {
        errors.push_back("manifest: schema_version " + std::to_string(schema) + " 지원 안 함 (1만 지원)");
        return r;
    }
    pkg.id = Str(m, "id");
    pkg.name = Str(m, "name");
    pkg.version = Str(m, "version");
    if (!ValidId(pkg.id)) errors.push_back("manifest: id 는 영문 소문자·숫자·하이픈 3~40자: '" + pkg.id + "'");
    if (pkg.name.empty()) errors.push_back("manifest: name 없음");
    if (pkg.version.empty()) errors.push_back("manifest: version 없음");
    if (const toml::table* lic = m["license"].as_table()) {
        pkg.license = {Str(*lic, "name"), Str(*lic, "author"), Str(*lic, "source")};
    }
    if (pkg.license.name.empty() || pkg.license.author.empty())
        errors.push_back("manifest: [license] name·author 필수 (배포 애셋 라이선스·출처 기록, 기준선 §6.2)");
    // [voice] 는 선택. 잘못된 값은 경고 후 무시 (전역 [tts] 값 사용)
    if (const toml::table* vt = m["voice"].as_table()) {
        pkg.voice.name = Str(*vt, "name");
        pkg.voice.aiVoice = Str(*vt, "ai_voice");
        const double rate = (*vt)["rate"].value_or(0.0);
        if (rate != 0.0 && (rate < 0.5 || rate > 2.0)) r.warnings.push_back("manifest: voice.rate 는 0.5~2.0 → 무시");
        else pkg.voice.rate = rate;
        auto badChars = [](const std::string& s) {
            return s.size() > 80 || std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 0x20; });
        };
        if (badChars(pkg.voice.name) || badChars(pkg.voice.aiVoice)) {
            r.warnings.push_back("manifest: voice.name / voice.ai_voice 는 80바이트 이하 한 줄 → 무시");
            pkg.voice = {};
        }
    }
    if (const toml::array* rs = m["renderers"].as_array())
        for (const auto& e : *rs)
            if (auto s = e.value<std::string>()) pkg.renderers.push_back(*s);
    const bool wantsSprite = std::find(pkg.renderers.begin(), pkg.renderers.end(), "sprite") != pkg.renderers.end();
    const bool wantsModel = std::find(pkg.renderers.begin(), pkg.renderers.end(), "gltf") != pkg.renderers.end();
    if (!wantsSprite && !wantsModel) errors.push_back("manifest: renderers 에 sprite 또는 gltf 가 필요");

    // [model] (P5): .glb 하나. 외부 파일 참조가 없어 경로 검사는 이 파일만
    std::error_code ec;
    if (wantsModel) {
        const toml::table* mt = m["model"].as_table();
        auto glb = mt ? ResolveInside(pkg.root, Str(*mt, "path")) : std::nullopt;
        if (!mt) {
            errors.push_back("manifest: renderers 에 gltf 가 있으면 [model] 필요");
        } else if (!glb) {
            errors.push_back("manifest: model.path 가 없거나 패키지 밖을 가리킴");
        } else if (glb->extension() != ".glb") {
            errors.push_back("manifest: model.path 는 .glb 여야 함 (외부 버퍼·이미지 파일 없음)");
        } else {
            const auto size = fs::file_size(*glb, ec);
            if (ec) errors.push_back("모델 파일 없음: " + glb->filename().string());
            else if (size > lim.maxModelBytes) errors.push_back("모델 파일이 너무 큼");
            pkg.model.path = *glb;
            pkg.model.baseHeight = static_cast<int>((*mt)["base_height"].value_or<int64_t>(128));
            if (pkg.model.baseHeight < 32 || pkg.model.baseHeight > 512) {
                r.warnings.push_back("manifest: model.base_height 범위 밖 → 128");
                pkg.model.baseHeight = 128;
            }
            pkg.model.yawDeg = (*mt)["yaw_deg"].value_or(35.0);
            if (pkg.model.yawDeg < -90 || pkg.model.yawDeg > 90) {
                r.warnings.push_back("manifest: model.yaw_deg 는 -90~90 → 35");
                pkg.model.yawDeg = 35;
            }
            if (const toml::table* at = (*mt)["animations"].as_table())
                for (const auto& [k, v] : *at)
                    if (auto s = v.value<std::string>()) pkg.model.animations[std::string(k.str())] = *s;
        }
    }

    const toml::table* sp = m["sprite"].as_table();
    if (!sp) {
        if (!wantsModel) {
            errors.push_back("manifest: [sprite] 없음");
            return r;
        }
        FinishPackage(std::move(pkg), lim, r);  // 3D 전용 패키지: 스프라이트 없음
        return r;
    }
    pkg.sprite.baseSize = static_cast<int>((*sp)["base_size"].value_or<int64_t>(128));
    if (pkg.sprite.baseSize < 32 || pkg.sprite.baseSize > 512) {
        r.warnings.push_back("manifest: sprite.base_size 범위 밖 → 128");
        pkg.sprite.baseSize = 128;
    }
    auto atlas = ResolveInside(pkg.root, Str(*sp, "atlas"));
    auto anims = ResolveInside(pkg.root, Str(*sp, "animations"));
    if (!atlas || !anims) {
        errors.push_back("manifest: sprite.atlas / sprite.animations 경로가 없거나 패키지 밖을 가리킴");
        return r;
    }
    if (atlas->extension() != ".png") errors.push_back("manifest: sprite.atlas 는 .png 여야 함");
    const auto atlasSize = fs::file_size(*atlas, ec);
    if (ec) errors.push_back("아틀라스 파일 없음: " + atlas->filename().string());
    else if (atlasSize > lim.maxFileBytes) errors.push_back("아틀라스 파일이 너무 큼");
    pkg.sprite.atlas = *atlas;

    if (auto at = ParseToml(*anims, lim.maxTomlBytes, errors, "animations.toml"))
        ParseAnimations(*at, lim, pkg.sprite, errors, r.warnings);
    FinishPackage(std::move(pkg), lim, r);
    return r;
}

namespace {
// 렌더러와 무관한 나머지: behavior.toml·persona.md (스프라이트·3D 공통). 오류가 없으면 r.package 를 채운다
void FinishPackage(CharacterPackage pkg, const PackageLimits& lim, PackageLoadResult& r) {
    auto& errors = r.errors;
    std::error_code ec;
    // behavior.toml 은 선택. 없으면 기본 FSM 값
    pkg.behavior.activities = {{"idle", {45, 3, 7}}, {"walk", {40, 2, 6}}, {"sit", {15, 5, 10}}};
    const fs::path behaviorPath = pkg.root / "behaviors" / "behavior.toml";
    if (fs::exists(behaviorPath, ec)) {
        if (auto bt = ParseToml(behaviorPath, lim.maxTomlBytes, errors, "behavior.toml")) {
            pkg.behavior.activities.clear();
            ParseBehavior(*bt, pkg.root, lim, pkg.behavior, errors, r.warnings);
            if (pkg.behavior.activities.empty()) {
                r.warnings.push_back("behavior.activities 가 비어 기본값 사용");
                pkg.behavior.activities = {{"idle", {45, 3, 7}}, {"walk", {40, 2, 6}}, {"sit", {15, 5, 10}}};
            }
        }
    }
    // 벽 타기는 climb 애니메이션이 있는 캐릭터만 (벽에 붙은 모습이 없으면 어색하다)
    if (pkg.behavior.activities.contains("climb") && !pkg.HasClip("climb")) {
        r.warnings.push_back("behavior.activities.climb: climb 애니메이션이 없어 벽 타기를 끔");
        pkg.behavior.activities.erase("climb");
    }
    // persona.md 는 선택 (P3, 기준선 §6.1). 대화 성격만 정하고 권한에는 영향 없음. 문제가 있어도 경고 후 무시
    const fs::path personaPath = pkg.root / "persona.md";
    if (fs::exists(personaPath, ec)) {
        std::vector<std::string> personaErrors;
        if (!ReadText(personaPath, lim.maxPersonaBytes, pkg.persona, personaErrors, "persona.md")) {
            for (auto& e : personaErrors) r.warnings.push_back(e + " → 페르소나 없이 사용");
            pkg.persona.clear();
        }
    }
    if (errors.empty()) r.package = std::move(pkg);
}
}  // namespace

std::vector<std::string> ValidateAtlasSize(const CharacterPackage& pkg, int width, int height, const PackageLimits& lim) {
    std::vector<std::string> errors;
    if (width <= 0 || height <= 0 || width > lim.maxAtlasDim || height > lim.maxAtlasDim) {
        errors.push_back("아틀라스 크기 " + std::to_string(width) + "x" + std::to_string(height) + " 허용 범위 밖 (최대 " +
                         std::to_string(lim.maxAtlasDim) + ")");
        return errors;
    }
    if (height != pkg.sprite.frameHeight || width % pkg.sprite.frameWidth != 0) {
        errors.push_back("아틀라스가 가로 배열 시트가 아님 (높이 = frame_height, 폭 = frame_width 의 배수)");
        return errors;
    }
    const int count = width / pkg.sprite.frameWidth;
    for (const auto& [name, clip] : pkg.sprite.clips)
        for (int f : clip.frames)
            if (f >= count) {
                errors.push_back("animations." + name + ": 프레임 " + std::to_string(f) + " 이 시트 범위(" +
                                 std::to_string(count) + "개) 밖");
                break;
            }
    return errors;
}

std::vector<fs::path> FindPackageDirs(const std::vector<fs::path>& searchRoots) {
    // 검색 경로 순서대로, 경로 안에서는 폴더 이름 순. 같은 id 의 우선순위(뒤 경로 우선)는 호출 측이 id 기준으로 정한다
    // → 뒤 경로의 패키지가 깨져 있으면 앞 경로의 같은 id 가 남는다
    std::vector<fs::path> out;
    for (const auto& root : searchRoots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        std::vector<fs::path> dirs;
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec)) continue;
            if (fs::exists(entry.path() / "manifest.toml", ec)) dirs.push_back(entry.path());
        }
        std::sort(dirs.begin(), dirs.end());
        out.insert(out.end(), dirs.begin(), dirs.end());
    }
    return out;
}

}  // namespace dp::core
