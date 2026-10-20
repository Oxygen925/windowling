#include "options.h"

#include <windows.h>

#include <format>

namespace dp {

const char* ToString(HitTestMode m) {
    switch (m) {
        case HitTestMode::None: return "none";
        case HitTestMode::A: return "A";
        case HitTestMode::B: return "B";
        case HitTestMode::C: return "C";
    }
    return "?";
}
const char* ToString(OverlayMode m) { return m == OverlayMode::Small ? "small" : "single"; }
const char* ToString(TimerMode m) {
    switch (m) {
        case TimerMode::MsgWait: return "msgwait";
        case TimerMode::HiRes: return "hires";
        case TimerMode::SetTimer: return "settimer";
    }
    return "?";
}

namespace {

bool ParseBool(const std::wstring& v, bool& out) {
    if (v == L"on" || v == L"1" || v == L"true") { out = true; return true; }
    if (v == L"off" || v == L"0" || v == L"false") { out = false; return true; }
    return false;
}

bool ParseInt(const std::wstring& v, int& out) {
    try {
        size_t pos = 0;
        out = std::stoi(v, &pos);
        return pos == v.size();
    } catch (...) {
        return false;
    }
}

}  // namespace

bool ParseOptions(int argc, wchar_t** argv, Options& o, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg.rfind(L"--", 0) != 0) {
            error = "unknown argument: " + ToUtf8(arg);
            return false;
        }
        auto eq = arg.find(L'=');
        std::wstring key = arg.substr(2, eq == std::wstring::npos ? std::wstring::npos : eq - 2);
        std::wstring val = eq == std::wstring::npos ? L"on" : arg.substr(eq + 1);
        bool ok = true;

        if (key == L"hittest") {
            if (val == L"none") o.hitTest = HitTestMode::None;
            else if (val == L"A" || val == L"a") o.hitTest = HitTestMode::A;
            else if (val == L"B" || val == L"b") o.hitTest = HitTestMode::B;
            else if (val == L"C" || val == L"c") o.hitTest = HitTestMode::C;
            else ok = false;
        } else if (key == L"overlay") {
            if (val == L"small") o.overlay = OverlayMode::Small;
            else if (val == L"single") o.overlay = OverlayMode::Single;
            else ok = false;
        } else if (key == L"content") {
            if (val == L"swapchain") o.content = ContentMode::SwapChain;
            else if (val == L"surface") o.content = ContentMode::Surface;
            else ok = false;
        } else if (key == L"anchor") {
            if (val == L"bottom") o.anchor = AnchorMode::Bottom;
            else if (val == L"top-left") o.anchor = AnchorMode::TopLeft;
            else ok = false;
        } else if (key == L"timer") {
            if (val == L"msgwait") o.timer = TimerMode::MsgWait;
            else if (val == L"hires") o.timer = TimerMode::HiRes;
            else if (val == L"settimer") o.timer = TimerMode::SetTimer;
            else ok = false;
        } else if (key == L"pets") ok = ParseInt(val, o.pets) && o.pets >= 0 && o.pets <= 16;
        else if (key == L"anim") ok = ParseInt(val, o.animFps) && o.animFps >= 0 && o.animFps <= 240;
        else if (key == L"move") ok = ParseBool(val, o.move);
        else if (key == L"proximity") ok = ParseBool(val, o.proximity);
        else if (key == L"cube") ok = ParseBool(val, o.cube);
        else if (key == L"text") ok = ParseBool(val, o.text);
        else if (key == L"ecoqos") ok = ParseBool(val, o.ecoQos);
        else if (key == L"gpu") ok = ParseBool(val, o.gpu);
        else if (key == L"debug-layer") ok = ParseBool(val, o.debugLayer);
        else if (key == L"scale") {
            try { o.scale = std::stof(val); } catch (...) { ok = false; }
            ok = ok && o.scale >= 0.25f && o.scale <= 8.0f;
        }
        else if (key == L"poll-near") ok = ParseInt(val, o.pollNearMs) && o.pollNearMs >= 1;
        else if (key == L"poll-far") ok = ParseInt(val, o.pollFarMs) && o.pollFarMs >= 1;
        else if (key == L"alpha-threshold") ok = ParseInt(val, o.alphaThreshold) && o.alphaThreshold >= 1 && o.alphaThreshold <= 255;
        else if (key == L"duration") ok = ParseInt(val, o.durationSec) && o.durationSec >= 0;
        else if (key == L"warmup") ok = ParseInt(val, o.warmupSec) && o.warmupSec >= 0;
        else if (key == L"follow-delay") ok = ParseInt(val, o.followDelaySec) && o.followDelaySec >= 0;
        else if (key == L"log") o.logPath = val;
        else if (key == L"capture") o.capturePath = val;
        else if (key == L"recreate-at") ok = ParseInt(val, o.recreateAtSec) && o.recreateAtSec >= 0;
        else if (key == L"capture-delay") ok = ParseInt(val, o.captureDelayMs) && o.captureDelayMs >= 0;
        else if (key == L"scenario") o.scenario = val;
        else if (key == L"variant") o.variant = val;
        else if (key == L"run") ok = ParseInt(val, o.runIndex) && o.runIndex >= 1;
        else {
            error = "unknown option: --" + ToUtf8(key);
            return false;
        }
        if (!ok) {
            error = "invalid value: " + ToUtf8(arg);
            return false;
        }
    }
    if (o.variant.empty()) o.variant = ToWide(std::format("{}-{}-{}", ToString(o.hitTest), ToString(o.overlay), ToString(o.content)));
    return true;
}

bool ValidateOptions(const Options& o, std::string& error) {
    // 전체 화면 topmost 창이 hit-test 없이 떠 있으면 데스크탑 클릭을 모두 가로챈다.
    if (o.overlay == OverlayMode::Single && o.hitTest == HitTestMode::None &&
        (o.durationSec == 0 || o.durationSec + o.warmupSec > 10)) {
        error = "overlay=single + hittest=none 은 데스크탑 입력을 모두 가로챈다. --duration (warmup 포함 10초 이하)일 때만 허용";
        return false;
    }
    if (!o.gpu && o.pets > 0) {
        error = "--gpu=off 는 --pets=0 (S0-A 트레이만) 에서만 허용";
        return false;
    }
    // 방식 C는 D3D/DComp를 쓰지 않는 별도 창 유형 (§5 E1, 계획서 §3.3)
    if (o.hitTest == HitTestMode::C) {
        if (o.overlay == OverlayMode::Single) { error = "hittest=C 는 overlay=small 만 지원"; return false; }
        if (o.cube) { error = "hittest=C 는 cube 미지원 (GPU 경로 아님)"; return false; }
        if (o.text) { error = "hittest=C 는 text 미지원 (GPU 경로 아님)"; return false; }
        if (o.content == ContentMode::Surface) { error = "hittest=C 는 content 옵션과 무관 (swapchain 기본값 사용)"; return false; }
    }
    // DComp surface는 BeginDraw 오프셋이 큰 아틀라스 텍스처 안을 가리키므로 펫 크기 깊이 버퍼와 맞지 않는다.
    if (o.content == ContentMode::Surface && o.cube) {
        error = "content=surface + cube 미지원 (BeginDraw 오프셋/깊이 버퍼 크기 불일치)";
        return false;
    }
    return true;
}

std::string DescribeOptions(const Options& o) {
    return std::format(
        "hittest={} overlay={} content={} timer={} anchor={} pets={} anim={} move={} proximity={} cube={} text={} "
        "ecoqos={} gpu={} scale={:.2f} poll={}/{}ms alpha>={} duration={} warmup={} follow-delay={} scenario={} variant={} run={}",
        ToString(o.hitTest), ToString(o.overlay), ToString(o.content), ToString(o.timer),
        o.anchor == AnchorMode::TopLeft ? "top-left" : "bottom", o.pets, o.animFps, o.move,
        o.proximity, o.cube, o.text, o.ecoQos, o.gpu, o.scale, o.pollNearMs, o.pollFarMs, o.alphaThreshold, o.durationSec,
        o.warmupSec, o.followDelaySec, ToUtf8(o.scenario), ToUtf8(o.variant), o.runIndex);
}

}  // namespace dp
