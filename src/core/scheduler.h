#pragma once
#include <cstdint>
#include <limits>
#include <map>
#include <optional>

namespace dp::core {

// 고정 스텝 누적기 (물리 60Hz, P1-D6). 시간은 호출자가 넣는다(Core는 시계·타이머 API를 모름).
// - 누적 시간은 maxAccumulated 로 잘라 긴 정지 후 폭주를 막는다.
// - Reset 은 누적 시간을 버린다(세션 복귀·숨김 해제, 기준선 §4.3).
class FixedStepClock {
public:
    explicit FixedStepClock(double hz = 60.0, double maxAccumulatedSec = 0.1)
        : step_(1.0 / hz), maxAccum_(maxAccumulatedSec) {}

    // now 까지 진행해야 할 스텝 수를 돌려준다. 첫 호출은 0.
    int Advance(double nowSec);
    void Reset(double nowSec);
    double StepSec() const { return step_; }
    // 다음 스텝 시각 (Advance 이후 기준)
    double NextStepAt() const { return last_ + (step_ - accum_); }
    bool Started() const { return started_; }

private:
    double step_, maxAccum_;
    double last_ = 0, accum_ = 0;
    bool started_ = false;
};

// 이름 붙은 마감 시각 모음. 메인 루프는 Next() 로 대기 시간을 정하고, 플랫폼 계층이 실제 타이머를 건다.
// 마감이 하나도 없으면 이벤트 기반 대기(D14: 정지 시 타이머 해제).
class Deadlines {
public:
    using Key = uint32_t;
    void Set(Key key, double atSec) { items_[key] = atSec; }
    void Clear(Key key) { items_.erase(key); }
    bool Has(Key key) const { return items_.contains(key); }
    std::optional<double> Get(Key key) const;
    // 가장 이른 마감 (없으면 nullopt)
    std::optional<double> Next() const;
    // now 이전에 도래한 마감을 꺼낸다 (호출자가 처리 후 필요하면 다시 Set)
    bool PopDue(Key key, double nowSec);
    bool Empty() const { return items_.empty(); }

private:
    std::map<Key, double> items_;
};

}  // namespace dp::core
