#include "core/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>

#include <toml++/toml.hpp>

namespace dp::core {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

template <class T>
void ReadNumber(const toml::table& t, std::string_view section, std::string_view key, T& out, T lo, T hi,
                std::vector<std::string>& warnings) {
    const auto* sec = t[section].as_table();
    if (!sec) return;
    auto node = (*sec)[key];
    if (!node) return;
    std::optional<T> v;
    if constexpr (std::is_floating_point_v<T>) {
        if (auto d = node.value<double>()) v = static_cast<T>(*d);
    } else {
        if (auto i = node.value<int64_t>()) v = static_cast<T>(*i);
    }
    if (!v || *v < lo || *v > hi) {
        warnings.push_back(std::string(section) + "." + std::string(key) + " 값이 잘못되어 기본값 사용");
        return;
    }
    out = *v;
}

void ReadBool(const toml::table& t, std::string_view section, std::string_view key, bool& out,
              std::vector<std::string>& warnings) {
    const auto* sec = t[section].as_table();
    if (!sec) return;
    auto node = (*sec)[key];
    if (!node) return;
    if (auto b = node.value<bool>()) out = *b;
    else warnings.push_back(std::string(section) + "." + std::string(key) + " 는 true/false 여야 함");
}

void ReadStrings(const toml::table& t, std::string_view section, std::string_view key, std::vector<std::string>& out,
                 std::vector<std::string>& warnings) {
    const auto* sec = t[section].as_table();
    if (!sec) return;
    auto node = (*sec)[key];
    if (!node) return;
    const auto* arr = node.as_array();
    if (!arr) {
        warnings.push_back(std::string(section) + "." + std::string(key) + " 는 문자열 배열이어야 함");
        return;
    }
    out.clear();
    for (const auto& el : *arr)
        if (auto s = el.value<std::string>()) out.push_back(Lower(*s));
}

const std::set<std::string> kKnownKeys = {
    "update.check", "display.scale", "display.proximity_reaction", "display.frame_rate", "display.language", "display.theme", "input.drag_threshold_px", "input.alpha_threshold", "input.hit_region",
    "physics.hz", "physics.gravity", "physics.walk_speed", "world.foot_support_ratio", "world.occlusion_grace_ms",
    "world.resync_interval_sec", "world.excluded_surface_exe", "fullscreen.coverage", "fullscreen.always_show_exe",
    "fullscreen.always_hide_exe", "ai.enabled", "ai.hotkey", "ai.model", "ai.history_turns", "ai.max_reply_chars",
    "ai.bubble_seconds", "ai.first_token_timeout_sec", "ai.request_timeout_sec", "ai.callback_port",
    "observe.interval_min", "observe.interval_max", "observe.max_per_hour", "observe.max_per_day",
    "observe.require_activity_min", "observe.send_app_name", "observe.image_max_px", "observe.jpeg_quality",
    "observe.max_speech_chars", "observe.exclude_exe", "observe.allowed_behaviors", "tts.enabled", "tts.engine", "tts.voice", "tts.rate",
    "tts.volume", "tts.read_chat_replies", "tts.http_url", "tts.http_model", "tts.http_voice", "tts.http_extra_json",
    "tts.http_timeout_sec", "tts.command_path", "tts.command_args", "tts.command_stdin", "tts.command_timeout_sec"};

void ReadString(const toml::table& t, std::string_view section, std::string_view key, std::string& out,
                std::vector<std::string>& warnings) {
    const auto* sec = t[section].as_table();
    if (!sec) return;
    auto node = (*sec)[key];
    if (!node) return;
    if (auto v = node.value<std::string>()) out = *v;
    else warnings.push_back(std::string(section) + "." + std::string(key) + " 는 문자열이어야 함");
}

}  // namespace

ConfigLoadResult ParseConfig(const std::string& tomlText) {
    ConfigLoadResult r;
    toml::table t;
    try {
        t = toml::parse(tomlText);
    } catch (const toml::parse_error& e) {
        r.warnings.push_back(std::string("설정 파일 문법 오류 — 기본값 사용: ") + std::string(e.description()));
        return r;
    }
    r.parsed = true;
    auto& c = r.config;
    auto& w = r.warnings;

    ReadNumber(t, "display", "scale", c.scale, 0.25, 8.0, w);
    ReadBool(t, "display", "proximity_reaction", c.proximityReaction, w);
    ReadNumber(t, "display", "frame_rate", c.frameRate, 5, 60, w);
    ReadString(t, "display", "language", c.language, w);
    if (c.language != "auto" && c.language != "ko" && c.language != "en") {
        w.push_back("display.language 은 auto·ko·en — auto 사용");
        c.language = "auto";
    }
    ReadString(t, "display", "theme", c.theme, w);
    if (c.theme != "auto" && c.theme != "light" && c.theme != "dark") {
        w.push_back("display.theme 은 auto·light·dark — auto 사용");
        c.theme = "auto";
    }
    ReadBool(t, "update", "check", c.updateCheck, w);
    ReadNumber(t, "input", "drag_threshold_px", c.dragThresholdPx, 1, 64, w);
    ReadNumber(t, "input", "alpha_threshold", c.alphaThreshold, 1, 255, w);
    ReadString(t, "input", "hit_region", c.hitRegion, w);
    if (c.hitRegion != "frame" && c.hitRegion != "clip") {
        w.push_back("input.hit_region 은 frame 또는 clip — frame 사용");
        c.hitRegion = "frame";
    }
    ReadNumber(t, "physics", "hz", c.physicsHz, 15.0, 240.0, w);
    ReadNumber(t, "physics", "gravity", c.gravityPxPerSec2, 100.0, 20000.0, w);
    ReadNumber(t, "physics", "walk_speed", c.walkSpeedPxPerSec, 0.0, 2000.0, w);
    ReadNumber(t, "world", "foot_support_ratio", c.footSupportRatio, 0.05, 1.0, w);
    ReadNumber(t, "world", "occlusion_grace_ms", c.occlusionGraceMs, 0, 5000, w);
    ReadNumber(t, "world", "resync_interval_sec", c.resyncIntervalSec, 5, 3600, w);
    ReadStrings(t, "world", "excluded_surface_exe", c.excludedSurfaceExe, w);
    ReadNumber(t, "fullscreen", "coverage", c.fullscreenCoverage, 0.5, 1.0, w);
    ReadStrings(t, "fullscreen", "always_show_exe", c.alwaysShowExe, w);
    ReadStrings(t, "fullscreen", "always_hide_exe", c.alwaysHideExe, w);

    // [ai] (P3)
    ReadBool(t, "ai", "enabled", c.ai.enabled, w);
    ReadString(t, "ai", "hotkey", c.ai.hotkey, w);
    if (!ParseHotkey(c.ai.hotkey)) {
        w.push_back("ai.hotkey 형식 오류 — 기본값 Ctrl+Alt+P 사용");
        c.ai.hotkey = AiConfig{}.hotkey;
    }
    ReadString(t, "ai", "model", c.ai.model, w);
    ReadNumber(t, "ai", "history_turns", c.ai.historyTurns, 1, 50, w);
    ReadNumber(t, "ai", "max_reply_chars", c.ai.maxReplyChars, 100, 20000, w);
    ReadNumber(t, "ai", "bubble_seconds", c.ai.bubbleSeconds, 1, 120, w);
    ReadNumber(t, "ai", "first_token_timeout_sec", c.ai.firstTokenTimeoutSec, 3, 600, w);
    ReadNumber(t, "ai", "request_timeout_sec", c.ai.requestTimeoutSec, 5, 3600, w);
    if (c.ai.requestTimeoutSec < c.ai.firstTokenTimeoutSec) {
        w.push_back("ai.request_timeout_sec 가 first_token_timeout_sec 보다 작아 같은 값으로 맞춤");
        c.ai.requestTimeoutSec = c.ai.firstTokenTimeoutSec;
    }
    ReadNumber(t, "ai", "callback_port", c.ai.callbackPort, 1024, 65535, w);

    // [observe]·[tts] (P4)
    auto& o = c.observe;
    ReadNumber(t, "observe", "interval_min", o.intervalMinMin, 1, 24 * 60, w);
    ReadNumber(t, "observe", "interval_max", o.intervalMaxMin, 1, 24 * 60, w);
    if (o.intervalMaxMin < o.intervalMinMin) {
        w.push_back("observe.interval_max 가 interval_min 보다 작아 같은 값으로 맞춤");
        o.intervalMaxMin = o.intervalMinMin;
    }
    ReadNumber(t, "observe", "max_per_hour", o.maxPerHour, 1, 60, w);
    ReadNumber(t, "observe", "max_per_day", o.maxPerDay, 1, 500, w);
    ReadNumber(t, "observe", "require_activity_min", o.requireActivityMin, 0, 24 * 60, w);
    ReadBool(t, "observe", "send_app_name", o.sendAppName, w);
    ReadNumber(t, "observe", "image_max_px", o.imageMaxPx, 256, 2048, w);
    ReadNumber(t, "observe", "jpeg_quality", o.jpegQuality, 30, 95, w);
    ReadNumber(t, "observe", "max_speech_chars", o.maxSpeechChars, 10, 400, w);
    ReadStrings(t, "observe", "exclude_exe", o.excludeExe, w);
    ReadStrings(t, "observe", "allowed_behaviors", o.allowedBehaviors, w);
    std::erase_if(o.allowedBehaviors, [&](const std::string& b) {
        const bool ok = b == "idle" || b == "sit" || b == "look" || b == "walk";
        if (!ok) w.push_back("observe.allowed_behaviors: '" + b + "' 는 지원 안 함 (idle·sit·look·walk) → 제외");
        return !ok;
    });
    ReadBool(t, "tts", "enabled", c.tts.enabled, w);
    ReadString(t, "tts", "engine", c.tts.engine, w);
    if (c.tts.engine != "winrt" && c.tts.engine != "http" && c.tts.engine != "command") {
        w.push_back("tts.engine 은 winrt·http·command 중 하나 — winrt 사용");
        c.tts.engine = "winrt";
    }
    ReadString(t, "tts", "voice", c.tts.voice, w);
    ReadNumber(t, "tts", "rate", c.tts.rate, 0.5, 2.0, w);
    ReadNumber(t, "tts", "volume", c.tts.volume, 0.0, 1.0, w);
    ReadBool(t, "tts", "read_chat_replies", c.tts.readChatReplies, w);
    ReadString(t, "tts", "http_url", c.tts.httpUrl, w);
    ReadString(t, "tts", "http_model", c.tts.httpModel, w);
    ReadString(t, "tts", "http_voice", c.tts.httpVoice, w);
    ReadString(t, "tts", "http_extra_json", c.tts.httpExtraJson, w);
    ReadNumber(t, "tts", "http_timeout_sec", c.tts.httpTimeoutSec, 1, 600, w);
    ReadString(t, "tts", "command_path", c.tts.commandPath, w);
    ReadString(t, "tts", "command_args", c.tts.commandArgs, w);
    ReadBool(t, "tts", "command_stdin", c.tts.commandStdin, w);
    ReadNumber(t, "tts", "command_timeout_sec", c.tts.commandTimeoutSec, 1, 600, w);
    if (c.tts.engine == "http" && !IsLoopbackUrl(c.tts.httpUrl))
        w.push_back("tts.http_url 은 http://127.0.0.1·localhost·[::1] 만 허용 — 이 설정으로는 읽지 않음");
    if (c.tts.engine == "command" && c.tts.commandArgs.find("{out}") == std::string::npos)
        w.push_back("tts.command_args 에 {out}(WAV 출력 파일) 이 없음 — 이 설정으로는 읽지 않음");
    if (!c.tts.commandPath.empty()) {
        const CommandKind kind = CommandKindOf(c.tts.commandPath);
        if (kind == CommandKind::Unsupported)
            w.push_back("tts.command_path 는 .exe·.bat·.cmd 만 지원 — 이 설정으로는 읽지 않음");
        else if (kind == CommandKind::Batch && (c.tts.commandArgs.find("{text}") != std::string::npos ||
                                                c.tts.commandArgs.find("{voice}") != std::string::npos))
            w.push_back("tts.command_args: 배치 파일(.bat/.cmd)에는 {text}·{voice} 를 쓸 수 없음 (cmd.exe 가 해석) — "
                        "{text_file} 또는 command_stdin 사용. 이 설정으로는 읽지 않음");
    }

    // [[pets]] — 캐릭터 id 와 마릿수 (P2)
    if (auto* arr = t["pets"].as_array()) {
        c.pets.clear();
        int total = 0;
        for (const auto& el : *arr) {
            const toml::table* pt = el.as_table();
            if (!pt) {
                w.push_back("pets 항목은 표([[pets]])여야 함");
                continue;
            }
            PetSpawn ps;
            ps.character = Lower((*pt)["character"].value_or(std::string()));
            ps.count = static_cast<int>((*pt)["count"].value_or<int64_t>(1));
            if (ps.character.empty() || ps.count < 1) {
                w.push_back("pets: character 가 없거나 count < 1 인 항목 무시");
                continue;
            }
            if (total + ps.count > Config::kMaxPets) {
                w.push_back("pets: 합계가 " + std::to_string(Config::kMaxPets) + "마리를 넘어 잘라냄");
                ps.count = Config::kMaxPets - total;
                if (ps.count <= 0) break;
            }
            total += ps.count;
            c.pets.push_back(ps);
        }
    } else if (t.contains("pets")) {
        w.push_back("pets 는 [[pets]] 배열이어야 함 — 기본값 사용");
    }

    // 알 수 없는 키 경고 (오타 발견용)
    for (const auto& [section, node] : t) {
        if (section.str() == "pets") continue;
        if (const auto* sec = node.as_table()) {
            for (const auto& [key, _] : *sec) {
                const std::string full = std::string(section.str()) + "." + std::string(key.str());
                if (!kKnownKeys.contains(full)) w.push_back("알 수 없는 설정 키: " + full);
            }
        } else {
            w.push_back("알 수 없는 최상위 설정: " + std::string(section.str()));
        }
    }
    return r;
}

std::string SerializeConfig(const Config& c) {
    auto strings = [](const std::vector<std::string>& v) {
        toml::array a;
        for (const auto& s : v) a.push_back(s);
        return a;
    };
    toml::table t{
        {"display", toml::table{{"scale", c.scale}, {"proximity_reaction", c.proximityReaction}, {"frame_rate", c.frameRate},
                                {"language", c.language}, {"theme", c.theme}}},
        {"input", toml::table{{"drag_threshold_px", c.dragThresholdPx}, {"alpha_threshold", c.alphaThreshold},
                              {"hit_region", c.hitRegion}}},
        {"update", toml::table{{"check", c.updateCheck}}},
        {"physics", toml::table{{"hz", c.physicsHz}, {"gravity", c.gravityPxPerSec2}, {"walk_speed", c.walkSpeedPxPerSec}}},
        {"world", toml::table{{"foot_support_ratio", c.footSupportRatio},
                              {"occlusion_grace_ms", c.occlusionGraceMs},
                              {"resync_interval_sec", c.resyncIntervalSec},
                              {"excluded_surface_exe", strings(c.excludedSurfaceExe)}}},
        {"fullscreen", toml::table{{"coverage", c.fullscreenCoverage},
                                   {"always_show_exe", strings(c.alwaysShowExe)},
                                   {"always_hide_exe", strings(c.alwaysHideExe)}}},
        {"ai", toml::table{{"enabled", c.ai.enabled},
                           {"hotkey", c.ai.hotkey},
                           {"model", c.ai.model},
                           {"history_turns", c.ai.historyTurns},
                           {"max_reply_chars", c.ai.maxReplyChars},
                           {"bubble_seconds", c.ai.bubbleSeconds},
                           {"first_token_timeout_sec", c.ai.firstTokenTimeoutSec},
                           {"request_timeout_sec", c.ai.requestTimeoutSec},
                           {"callback_port", c.ai.callbackPort}}},
        {"observe", toml::table{{"interval_min", c.observe.intervalMinMin},
                                {"interval_max", c.observe.intervalMaxMin},
                                {"max_per_hour", c.observe.maxPerHour},
                                {"max_per_day", c.observe.maxPerDay},
                                {"require_activity_min", c.observe.requireActivityMin},
                                {"send_app_name", c.observe.sendAppName},
                                {"image_max_px", c.observe.imageMaxPx},
                                {"jpeg_quality", c.observe.jpegQuality},
                                {"max_speech_chars", c.observe.maxSpeechChars},
                                {"exclude_exe", strings(c.observe.excludeExe)},
                                {"allowed_behaviors", strings(c.observe.allowedBehaviors)}}},
        {"tts", toml::table{{"enabled", c.tts.enabled},
                            {"engine", c.tts.engine},
                            {"voice", c.tts.voice},
                            {"rate", c.tts.rate},
                            {"volume", c.tts.volume},
                            {"read_chat_replies", c.tts.readChatReplies},
                            {"http_url", c.tts.httpUrl},
                            {"http_model", c.tts.httpModel},
                            {"http_voice", c.tts.httpVoice},
                            {"http_extra_json", c.tts.httpExtraJson},
                            {"http_timeout_sec", c.tts.httpTimeoutSec},
                            {"command_path", c.tts.commandPath},
                            {"command_args", c.tts.commandArgs},
                            {"command_stdin", c.tts.commandStdin},
                            {"command_timeout_sec", c.tts.commandTimeoutSec}}},
    };
    toml::array pets;
    for (const auto& ps : c.pets) pets.push_back(toml::table{{"character", ps.character}, {"count", ps.count}});
    t.insert("pets", std::move(pets));
    std::ostringstream os;
    os << "# Desktop Pet 설정. 잘못된 값은 기본값으로 대체되고 로그에 경고가 남는다.\n"
       << "# [ai] enabled = true 로 AI 대화(Sign in with ChatGPT)를 켠다. 기본은 꺼짐.\n"
       << "# 화면 관찰은 트레이 메뉴의 동의 화면으로만 켠다. [observe] 는 간격·상한·제외 앱 같은 값만 정한다.\n"
       << "# [[pets]] 의 character 를 바꾸면 코드 수정 없이 캐릭터를 교체할 수 있다 (characters 폴더의 패키지 id).\n"
       << t << "\n";
    return os.str();
}

CommandKind CommandKindOf(std::string_view path) {
    const size_t dot = path.find_last_of('.');
    const size_t sep = path.find_last_of("/\\");
    if (dot == std::string_view::npos || (sep != std::string_view::npos && dot < sep)) return CommandKind::Unsupported;
    const std::string ext = Lower(std::string(path.substr(dot)));
    if (ext == ".exe") return CommandKind::Exe;
    if (ext == ".bat" || ext == ".cmd") return CommandKind::Batch;
    return CommandKind::Unsupported;
}

bool IsLoopbackUrl(std::string_view url) {
    // 접두어 비교만 하면 "http://127.0.0.1:1@evil.example/" 처럼 userinfo 로 다른 호스트를 가리킬 수 있었다 (보안 점검 2026-10-20).
    // authority 를 끝까지 읽어 호스트가 정확히 루프백이고 포트가 숫자뿐인지 본다. 역슬래시·@·공백·제어 문자는 거부
    constexpr std::string_view scheme = "http://";
    if (url.substr(0, scheme.size()) != scheme) return false;
    for (unsigned char c : url)
        if (c <= 0x20 || c == 0x7F || c == '\\') return false;
    const std::string_view rest = url.substr(scheme.size());
    const std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
    if (authority.find('@') != std::string_view::npos) return false;
    std::string_view host, port;
    if (!authority.empty() && authority.front() == '[') {
        const size_t close = authority.find(']');
        if (close == std::string_view::npos) return false;
        host = authority.substr(0, close + 1);
        port = authority.substr(close + 1);
    } else {
        const size_t colon = authority.find(':');
        host = authority.substr(0, colon);
        port = colon == std::string_view::npos ? std::string_view() : authority.substr(colon);
    }
    const std::string h = Lower(std::string(host));
    if (h != "127.0.0.1" && h != "localhost" && h != "[::1]") return false;
    if (port.empty()) return true;
    if (port.front() != ':' || port.size() < 2 || port.size() > 6) return false;
    int value = 0;
    for (char c : port.substr(1)) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    return value >= 1 && value <= 65535;
}

std::optional<Hotkey> ParseHotkey(std::string_view text) {
    Hotkey h;
    bool haveKey = false;
    std::string s = Lower(std::string(text));
    size_t start = 0;
    while (start <= s.size()) {
        const size_t plus = s.find('+', start);
        std::string part = s.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        part.erase(0, part.find_first_not_of(' '));
        if (const auto e = part.find_last_not_of(' '); e != std::string::npos) part.erase(e + 1);
        if (part == "ctrl" || part == "control") h.ctrl = true;
        else if (part == "alt") h.alt = true;
        else if (part == "shift") h.shift = true;
        else if (part == "win") h.win = true;
        else if (!haveKey && part.size() == 1 && std::isalnum(static_cast<unsigned char>(part[0]))) {
            h.key = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
            haveKey = true;
        } else if (!haveKey && part.size() >= 2 && part[0] == 'f' && std::isdigit(static_cast<unsigned char>(part[1]))) {
            const int n = std::atoi(part.c_str() + 1);
            if (n < 1 || n > 12) return std::nullopt;
            h.fkey = n;
            haveKey = true;
        } else {
            return std::nullopt;
        }
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    // 수정 키 없는 단일 문자 키는 전역 단축키로 부적합
    if (!haveKey || (h.key && !h.ctrl && !h.alt && !h.win)) return std::nullopt;
    return h;
}

// 설정 창(UI-07)이 다루는 항목: 설정 파일 키, Config 멤버. CopyUiFields·DiffUiFields 가 같은 목록을 쓴다
// (AI 모델 선택은 대화창, callback_port·max_reply_chars·image_max_px 등은 설정 파일에서만, 관찰 켜고 끄기는 동의 기록)
#define DP_UI_FIELDS(X) \
    X("display.language", language) \
    X("display.theme", theme) \
    X("display.scale", scale) \
    X("display.frame_rate", frameRate) \
    X("display.proximity_reaction", proximityReaction) \
    X("update.check", updateCheck) \
    X("ai.enabled", ai.enabled) \
    X("ai.hotkey", ai.hotkey) \
    X("ai.history_turns", ai.historyTurns) \
    X("ai.bubble_seconds", ai.bubbleSeconds) \
    X("ai.first_token_timeout_sec", ai.firstTokenTimeoutSec) \
    X("ai.request_timeout_sec", ai.requestTimeoutSec) \
    X("observe.interval_min", observe.intervalMinMin) \
    X("observe.interval_max", observe.intervalMaxMin) \
    X("observe.max_per_hour", observe.maxPerHour) \
    X("observe.max_per_day", observe.maxPerDay) \
    X("observe.require_activity_min", observe.requireActivityMin) \
    X("observe.send_app_name", observe.sendAppName) \
    X("observe.exclude_exe", observe.excludeExe) \
    X("observe.allowed_behaviors", observe.allowedBehaviors) \
    X("tts.enabled", tts.enabled) \
    X("tts.engine", tts.engine) \
    X("tts.voice", tts.voice) \
    X("tts.rate", tts.rate) \
    X("tts.volume", tts.volume) \
    X("tts.read_chat_replies", tts.readChatReplies) \
    X("tts.http_url", tts.httpUrl) \
    X("tts.http_model", tts.httpModel) \
    X("tts.http_voice", tts.httpVoice) \
    X("tts.command_path", tts.commandPath) \
    X("tts.command_args", tts.commandArgs) \
    X("fullscreen.always_show_exe", alwaysShowExe) \
    X("fullscreen.always_hide_exe", alwaysHideExe)

void CopyUiFields(const Config& from, Config& to) {
#define DP_COPY(key, member) to.member = from.member;
    DP_UI_FIELDS(DP_COPY)
#undef DP_COPY
}

std::vector<std::string> DiffUiFields(const Config& a, const Config& b) {
    std::vector<std::string> out;
#define DP_DIFF(key, member) \
    if (!(a.member == b.member)) out.emplace_back(key);
    DP_UI_FIELDS(DP_DIFF)
#undef DP_DIFF
    return out;
}

#undef DP_UI_FIELDS

}  // namespace dp::core
