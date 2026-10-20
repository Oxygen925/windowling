#include "diagnostics/metrics.h"

#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>

#include "diagnostics/log.h"

namespace dp {

Counters& GlobalCounters() {
    static Counters c;
    return c;
}

namespace {

uint64_t FileTimeTo100ns(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

uint64_t ProcessCpu100ns(HANDLE process) {
    FILETIME c, e, k, u;
    if (!GetProcessTimes(process, &c, &e, &k, &u)) return 0;
    return FileTimeTo100ns(k) + FileTimeTo100ns(u);
}

HANDLE OpenDwm() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return nullptr;
    PROCESSENTRY32W pe{sizeof(pe)};
    HANDLE result = nullptr;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"dwm.exe") == 0) {
            result = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!result) log::Warn("dwm.exe(pid {}) OpenProcess 실패 err={} — dwm CPU는 p0-counters로 측정", pe.th32ProcessID, GetLastError());
            break;
        }
    }
    CloseHandle(snap);
    return result;
}

}  // namespace

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    double idx = p * (v.size() - 1);
    size_t lo = static_cast<size_t>(idx);
    size_t hi = std::min(lo + 1, v.size() - 1);
    double t = idx - lo;
    return v[lo] * (1 - t) + v[hi] * t;
}

uint32_t MetricsSampler::LogicalProcessorCount() {
    return GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
}

void MetricsSampler::Init() {
    QueryPerformanceFrequency(&freq_);
    // 사이클 카운터 주파수 보정: 50ms 동안 스레드를 계속 돌려 사이클 증가량 / 경과 시간
    ULONG64 c0 = 0, c1 = 0;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    QueryThreadCycleTime(GetCurrentThread(), &c0);
    do {
        QueryPerformanceCounter(&t1);
    } while (t1.QuadPart - t0.QuadPart < freq_.QuadPart / 20);
    QueryThreadCycleTime(GetCurrentThread(), &c1);
    cycleHz_ = (c1 - c0) / (static_cast<double>(t1.QuadPart - t0.QuadPart) / freq_.QuadPart);
    log::Info("사이클 카운터 보정 {:.1f} MHz", cycleHz_ / 1e6);
    dwm_ = OpenDwm();
    last_ = Take();
}

void MetricsSampler::Reset() {
    last_ = Take();
    inputLatency_.clear();
    tickJitter_.clear();
    presentInterval_.clear();
    presentJitter_.clear();
}

MetricsSampler::Snapshot MetricsSampler::Take() const {
    Snapshot s;
    QueryPerformanceCounter(&s.qpc);
    s.cpu100ns = ProcessCpu100ns(GetCurrentProcess());
    ULONG64 cycles = 0;
    QueryProcessCycleTime(GetCurrentProcess(), &cycles);
    s.cycles = cycles;
    s.dwmCpu100ns = dwm_ ? ProcessCpu100ns(dwm_) : 0;
    s.presents = GlobalCounters().presents;
    s.stateChanges = GlobalCounters().stateChanges;
    s.wakeups = GlobalCounters().wakeups;
    return s;
}

MetricsSample MetricsSampler::Sample() {
    Snapshot now = Take();
    MetricsSample m;
    m.intervalSec = static_cast<double>(now.qpc.QuadPart - last_.qpc.QuadPart) / freq_.QuadPart;
    const double wall100ns = m.intervalSec * 1e7 * LogicalProcessorCount();
    m.procCpuMsDelta = (now.cpu100ns - last_.cpu100ns) / 1e4;
    m.procCpuPct = wall100ns > 0 ? 100.0 * (now.cpu100ns - last_.cpu100ns) / wall100ns : 0;
    m.procCyclesDelta = now.cycles - last_.cycles;
    if (cycleHz_ > 0 && m.intervalSec > 0)
        m.procCpuPctCycles = 100.0 * (m.procCyclesDelta / cycleHz_) / (m.intervalSec * LogicalProcessorCount());
    if (dwm_ && wall100ns > 0) m.dwmCpuPct = 100.0 * (now.dwmCpu100ns - last_.dwmCpu100ns) / wall100ns;
    m.presentDelta = now.presents - last_.presents;
    m.stateChanged = now.stateChanges != last_.stateChanges;
    m.wakeupsDelta = now.wakeups - last_.wakeups;

    PROCESS_MEMORY_COUNTERS_EX pmc{sizeof(pmc)};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        m.workingSetMb = pmc.WorkingSetSize / (1024.0 * 1024.0);
        m.privateMb = pmc.PrivateUsage / (1024.0 * 1024.0);
    }
    if (adapter_) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
            m.vramLocalMb = info.CurrentUsage / (1024.0 * 1024.0);
        if (SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info)))
            m.vramNonLocalMb = info.CurrentUsage / (1024.0 * 1024.0);
    }
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    m.handleCount = handles;
    m.gdiObjects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    m.userObjects = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);

    m.inputLatencyP50 = Percentile(inputLatency_, 0.5);
    m.inputLatencyP95 = Percentile(inputLatency_, 0.95);
    m.tickJitterP50 = Percentile(tickJitter_, 0.5);
    m.tickJitterP95 = Percentile(tickJitter_, 0.95);
    m.tickJitterMax = tickJitter_.empty() ? -1 : *std::max_element(tickJitter_.begin(), tickJitter_.end());
    if (!presentInterval_.empty()) {
        double sum = 0;
        for (double v : presentInterval_) sum += v;
        m.presentIntervalMean = sum / presentInterval_.size();
        m.presentIntervalP95 = Percentile(presentInterval_, 0.95);
        m.presentJitterP95 = Percentile(presentJitter_, 0.95);
    }
    inputLatency_.clear();
    tickJitter_.clear();
    presentInterval_.clear();
    presentJitter_.clear();
    last_ = now;
    return m;
}

}  // namespace dp
