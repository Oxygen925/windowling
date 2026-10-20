#pragma once
// M1 단계별 비용 계측: UI 스레드의 단계마다 CPU 사이클(QueryThreadCycleTime)을 누적한다.
// 중첩된 단계는 바깥 단계에서 빠진다(독점 비용). 측정 CSV 를 켰을 때만 동작하고, 끄면 Scope 는 아무것도 하지 않는다.
#include <windows.h>

#include <array>
#include <cstdint>

namespace dp {

enum class CostPhase : int { Pump, World, Physics, Behavior, Cursor, Render, Region, Move, Bubble, Count };
inline constexpr const char* kCostPhaseNames[] = {"pump", "world", "physics", "behavior", "cursor",
                                                  "render", "region", "move", "bubble"};

class CostMeter {
public:
    static constexpr int kPhases = static_cast<int>(CostPhase::Count);
    void Enable(bool on) { enabled_ = on; }
    bool Enabled() const { return enabled_; }

    class Scope {
    public:
        Scope(CostMeter& m, CostPhase p) : m_(m.enabled_ ? &m : nullptr), phase_(static_cast<int>(p)) {
            if (!m_) return;
            parent_ = m_->current_;
            m_->current_ = phase_;
            start_ = Now();
            m_->childAccum_[phase_] = 0;
        }
        ~Scope() {
            if (!m_) return;
            const uint64_t total = Now() - start_;
            const uint64_t self = total - m_->childAccum_[phase_];
            m_->cycles_[phase_] += self;
            m_->counts_[phase_]++;
            m_->current_ = parent_;
            if (parent_ >= 0) m_->childAccum_[parent_] += total;
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        CostMeter* m_;
        int phase_, parent_ = -1;
        uint64_t start_ = 0;
    };

    // 1초 창마다 읽고 0 으로 되돌린다
    std::array<uint64_t, kPhases> TakeCycles() {
        auto c = cycles_;
        cycles_.fill(0);
        return c;
    }
    std::array<uint64_t, kPhases> TakeCounts() {
        auto c = counts_;
        counts_.fill(0);
        return c;
    }
    static uint64_t Now() {
        ULONG64 c = 0;
        QueryThreadCycleTime(GetCurrentThread(), &c);
        return c;
    }

private:
    bool enabled_ = false;
    int current_ = -1;
    std::array<uint64_t, kPhases> cycles_{}, counts_{}, childAccum_{};
};

}  // namespace dp
