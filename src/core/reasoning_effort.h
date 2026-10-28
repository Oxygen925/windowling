#pragma once
// reasoning.effort 로 보낼 수 있는 값 (설정·요청·설정 창 공용, 2026-10-21). "" = 보내지 않음 = 모델 기본값
#include <array>
#include <string_view>

namespace dp::core {

inline constexpr std::array<std::string_view, 7> kReasoningEfforts = {"", "none", "minimal", "low", "medium", "high", "xhigh"};

inline bool IsReasoningEffort(std::string_view effort) {
    for (std::string_view v : kReasoningEfforts)
        if (effort == v) return true;
    return false;
}

}  // namespace dp::core
