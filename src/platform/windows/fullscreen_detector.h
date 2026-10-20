#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "core/config.h"
#include "core/world.h"

namespace dp {

// P1-07: 전경 창 기준 전체화면 판정 (P1 계획 §3.7, 기준선 §4.3)
//  1) 전경 창이 그 모니터를 config.fullscreenCoverage 이상 덮는다 (최대화 창은 작업 표시줄을 덮지 않아 제외)
//  2) 예외 목록: alwaysShow 이면 숨기지 않음, alwaysHide 이면 덮음 비율과 무관하게 숨김
//  3) SHQueryUserNotificationState 는 판정에 쓰지 않고 로그로만 기록 (보조 신호, 동작 [검증])
class FullscreenDetector {
public:
    // 판정이 바뀌면 true
    bool Update(const core::Config& config, const std::vector<core::MonitorInfo>& monitors);
    // 모니터별 숨김 여부 (monitors 와 같은 순서)
    const std::vector<bool>& HiddenMonitors() const { return hidden_; }
    bool AnyHidden() const;
    const std::string& Describe() const { return describe_; }

private:
    std::vector<bool> hidden_;
    std::string describe_;
    int lastQuns_ = -1;
};

}  // namespace dp
