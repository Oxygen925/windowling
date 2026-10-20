#include "core/scheduler.h"

#include <algorithm>

namespace dp::core {

int FixedStepClock::Advance(double nowSec) {
    if (!started_) {
        Reset(nowSec);
        return 0;
    }
    accum_ = std::min(accum_ + std::max(0.0, nowSec - last_), maxAccum_);
    last_ = nowSec;
    int steps = 0;
    // 부동소수 오차로 마지막 스텝이 누락되지 않도록 작은 여유를 둔다
    while (accum_ + 1e-9 >= step_) {
        accum_ -= step_;
        ++steps;
    }
    accum_ = std::max(0.0, accum_);
    return steps;
}

void FixedStepClock::Reset(double nowSec) {
    last_ = nowSec;
    accum_ = 0;
    started_ = true;
}

std::optional<double> Deadlines::Get(Key key) const {
    auto it = items_.find(key);
    if (it == items_.end()) return std::nullopt;
    return it->second;
}

std::optional<double> Deadlines::Next() const {
    if (items_.empty()) return std::nullopt;
    return std::min_element(items_.begin(), items_.end(), [](auto& a, auto& b) { return a.second < b.second; })->second;
}

bool Deadlines::PopDue(Key key, double nowSec) {
    auto it = items_.find(key);
    if (it == items_.end() || it->second > nowSec) return false;
    items_.erase(it);
    return true;
}

}  // namespace dp::core
