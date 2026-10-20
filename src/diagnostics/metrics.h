#pragma once
#include <windows.h>

#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace dp {

// 렌더 경로 공통 카운터 (UI 스레드 전용)
struct Counters {
    uint64_t presents = 0;      // Present / Commit / UpdateLayeredWindow 횟수
    uint64_t stateChanges = 0;  // dirty 표시 횟수 (상태 변화)
    uint64_t wakeups = 0;       // 메인 루프 대기 해제 횟수 (전력 효율 지표)
};
Counters& GlobalCounters();

struct MetricsSample {
    double intervalSec = 0;
    double procCpuPct = 0;  // GetProcessTimes 기반, 전체 논리 프로세서 대비 (15.6ms 틱 단위 → 30~60초 이상 집계해 판정)
    double procCpuPctCycles = 0;  // QueryProcessCycleTime ÷ 보정 주파수 (보조 지표, 단독 판정 금지 — C2)
    double procCpuMsDelta = 0;
    uint64_t procCyclesDelta = 0;
    double workingSetMb = 0;
    double privateMb = 0;
    double vramLocalMb = -1;
    double vramNonLocalMb = -1;
    uint64_t presentDelta = 0;
    bool stateChanged = false;
    double dwmCpuPct = -1;  // 접근 불가 시 -1 (외부 p0-counters 가 기준)
    double inputLatencyP50 = -1, inputLatencyP95 = -1;
    double tickJitterP50 = -1, tickJitterP95 = -1, tickJitterMax = -1;
    uint32_t handleCount = 0, gdiObjects = 0, userObjects = 0;
    uint64_t wakeupsDelta = 0;
    double presentIntervalMean = -1, presentIntervalP95 = -1, presentJitterP95 = -1;
};

class MetricsSampler {
public:
    void Init();
    void SetAdapter(IDXGIAdapter3* adapter) { adapter_ = adapter; }
    // 기준점만 갱신 (warmup 종료 시점 등)
    void Reset();
    MetricsSample Sample();

    void AddInputLatencyMs(double ms) { inputLatency_.push_back(ms); }
    void AddTickJitterMs(double ms) { tickJitter_.push_back(ms); }
    void AddPresentIntervalMs(double intervalMs, double jitterMs) {
        presentInterval_.push_back(intervalMs);
        presentJitter_.push_back(jitterMs);
    }

    static uint32_t LogicalProcessorCount();
    double CycleHz() const { return cycleHz_; }

private:
    struct Snapshot {
        LARGE_INTEGER qpc{};
        uint64_t cpu100ns = 0;
        uint64_t cycles = 0;
        uint64_t dwmCpu100ns = 0;
        uint64_t presents = 0;
        uint64_t stateChanges = 0;
        uint64_t wakeups = 0;
    };
    Snapshot Take() const;

    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
    HANDLE dwm_ = nullptr;
    LARGE_INTEGER freq_{};
    double cycleHz_ = 0;  // 사이클 카운터 주파수 (Init에서 보정)
    Snapshot last_{};
    std::vector<double> inputLatency_;
    std::vector<double> tickJitter_;
    std::vector<double> presentInterval_;
    std::vector<double> presentJitter_;
};

// 구간 통계 헬퍼
double Percentile(std::vector<double> v, double p);

}  // namespace dp
