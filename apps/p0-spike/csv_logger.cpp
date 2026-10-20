#include "csv_logger.h"

#include <windows.h>

#include <dxgi1_4.h>
#include <shellscalingapi.h>
#include <wrl/client.h>

#include <format>
#include <share.h>

namespace dp {

using Microsoft::WRL::ComPtr;

bool CsvLogger::Open(const std::wstring& path, const std::vector<std::string>& metaLines) {
    Close();
    file_ = _wfsopen(path.c_str(), L"wb", _SH_DENYNO);  // 측정 중 외부에서 읽기 허용
    if (!file_) {
        file_ = nullptr;
        return false;
    }
    for (const auto& line : metaLines) fprintf(file_, "# %s\n", line.c_str());
    fputs(
        "timestamp_utc,scenario,variant,run_index,pet_count,anim_fps,"
        "proc_cpu_pct,proc_cpu_ms_delta,proc_cycles_delta,proc_cpu_pct_cycles,"
        "working_set_mb,private_mb,vram_local_mb,vram_nonlocal_mb,"
        "present_count_delta,state_changed,dwm_cpu_pct,"
        "input_latency_ms_p50,input_latency_ms_p95,"
        "tick_jitter_ms_p50,tick_jitter_ms_p95,tick_jitter_ms_max,"
        "handle_count,gdi_objects,user_objects,"
        "wakeups_delta,present_interval_ms_mean,present_interval_ms_p95,present_jitter_ms_p95,suspended,"
        "interval_sec,notes\n",
        file_);
    fflush(file_);
    return true;
}

namespace {
std::string Opt(double v, int prec = 3) {
    return v < 0 ? std::string() : std::format("{:.{}f}", v, prec);
}
}  // namespace

void CsvLogger::WriteRow(const Options& o, int petCount, const MetricsSample& m, bool suspended,
                         const std::string& notes) {
    if (!file_) return;
    SYSTEMTIME st;
    GetSystemTime(&st);
    std::string row = std::format(
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z,{},{},{},{},{},"
        "{:.4f},{:.3f},{},{:.5f},"
        "{:.2f},{:.2f},{},{},"
        "{},{},{},"
        "{},{},"
        "{},{},{},"
        "{},{},{},"
        "{},{},{},{},{},"
        "{:.3f},{}\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, ToUtf8(o.scenario),
        ToUtf8(o.variant), o.runIndex, petCount, o.animFps, m.procCpuPct, m.procCpuMsDelta, m.procCyclesDelta, m.procCpuPctCycles,
        m.workingSetMb, m.privateMb, Opt(m.vramLocalMb, 2), Opt(m.vramNonLocalMb, 2), m.presentDelta,
        m.stateChanged ? 1 : 0, Opt(m.dwmCpuPct, 4), Opt(m.inputLatencyP50), Opt(m.inputLatencyP95),
        Opt(m.tickJitterP50), Opt(m.tickJitterP95), Opt(m.tickJitterMax), m.handleCount, m.gdiObjects,
        m.userObjects, m.wakeupsDelta, Opt(m.presentIntervalMean), Opt(m.presentIntervalP95), Opt(m.presentJitterP95),
        suspended ? 1 : 0, m.intervalSec, notes);
    fputs(row.c_str(), file_);
    fflush(file_);
}

void CsvLogger::Close() {
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
}

std::vector<std::string> CollectEnvironmentMeta() {
    std::vector<std::string> meta;

    // CPU 이름
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) ==
        ERROR_SUCCESS) {
        wchar_t name[256] = {};
        DWORD size = sizeof(name);
        if (RegQueryValueExW(key, L"ProcessorNameString", nullptr, nullptr, reinterpret_cast<BYTE*>(name), &size) ==
            ERROR_SUCCESS)
            meta.push_back("cpu=" + ToUtf8(name));
        RegCloseKey(key);
    }
    meta.push_back(std::format("logical_processors={}", MetricsSampler::LogicalProcessorCount()));

    // P/E 코어 구분 (EfficiencyClass)
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    std::vector<BYTE> buf(len);
    if (len && GetLogicalProcessorInformationEx(RelationProcessorCore,
                                                reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data()), &len)) {
        int cores[2] = {0, 0};
        for (DWORD off = 0; off < len;) {
            auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
            cores[info->Processor.EfficiencyClass > 0 ? 1 : 0]++;
            off += info->Size;
        }
        meta.push_back(std::format("cores_efficiency_class0(E추정)={} cores_efficiency_class_gt0(P추정)={}", cores[0], cores[1]));
    }

    // GPU
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d{};
            adapter->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            LARGE_INTEGER umd{};
            adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd);
            meta.push_back(std::format("gpu{}={} vram_mb={} driver={}.{}.{}.{}", i, ToUtf8(d.Description),
                                       d.DedicatedVideoMemory / (1024 * 1024), HIWORD(umd.HighPart),
                                       LOWORD(umd.HighPart), HIWORD(umd.LowPart), LOWORD(umd.LowPart)));
        }
    }

    // 모니터
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* out = reinterpret_cast<std::vector<std::string>*>(lp);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            GetMonitorInfoW(mon, &mi);
            UINT dx = 96, dy = 96;
            GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
            DEVMODEW dm{};
            dm.dmSize = sizeof(dm);
            EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm);
            out->push_back(std::format("monitor={} rect=({},{},{},{}) work=({},{},{},{}) dpi={} scale={}% refresh_hz={}",
                                       ToUtf8(mi.szDevice), mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right,
                                       mi.rcMonitor.bottom, mi.rcWork.left, mi.rcWork.top, mi.rcWork.right,
                                       mi.rcWork.bottom, dx, dx * 100 / 96, dm.dmDisplayFrequency));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&meta));

    // OS 빌드
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) {
        OSVERSIONINFOW v{sizeof(v)};
        fn(&v);
        meta.push_back(std::format("os={}.{}.{}", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber));
    }

    // 전원 상태
    SYSTEM_POWER_STATUS ps{};
    GetSystemPowerStatus(&ps);
    meta.push_back(std::format("ac_line={} battery_saver={}", ps.ACLineStatus, ps.SystemStatusFlag));
    return meta;
}

}  // namespace dp
