// P0 외부 성능 카운터 수집기 (계획서 §4.1)
// 측정 대상 프로세스와 분리해 dwm.exe CPU·GPU 엔진 사용률을 1초 간격으로 기록한다.
// PdhAddEnglishCounterW 를 사용하므로 한국어 Windows에서도 영문 카운터 경로로 동작한다.
//
// 사용: p0-counters.exe --pid=<대상 PID> --out=<csv> [--interval=1000] [--duration=<초>]
//       대상 프로세스가 종료되면 자동 종료.
#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

DWORD FindPid(const wchar_t* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{sizeof(pe)};
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, exe) == 0) {
            pid = pe.th32ProcessID;
            break;
        }
    CloseHandle(snap);
    return pid;
}

std::wstring ProcessInstanceName(DWORD pid) {
    // Process 카운터 인스턴스 이름은 확장자 없는 exe 이름
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{sizeof(pe)};
    std::wstring name;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (pe.th32ProcessID == pid) {
            name = pe.szExeFile;
            break;
        }
    CloseHandle(snap);
    auto dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? name : name.substr(0, dot);
}

// 와일드카드 카운터의 인스턴스 합
double SumArray(PDH_HCOUNTER c) {
    DWORD bytes = 0, count = 0;
    PDH_STATUS s = PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr);
    if (s != PDH_MORE_DATA) return -1;
    std::vector<BYTE> buf(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items) != ERROR_SUCCESS)
        return -1;
    double sum = 0;
    for (DWORD i = 0; i < count; ++i)
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
            sum += items[i].FmtValue.doubleValue;
    return sum;
}

double Single(PDH_HCOUNTER c) {
    PDH_FMT_COUNTERVALUE v{};
    if (PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) != ERROR_SUCCESS) return -1;
    return v.doubleValue;
}

std::string Fmt(double v) {
    if (v < 0) return "";
    char b[32];
    sprintf_s(b, "%.4f", v);
    return b;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    DWORD pid = 0;
    std::wstring out;
    DWORD interval = 1000;
    DWORD duration = 0;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a.rfind(L"--pid=", 0) == 0) pid = std::wcstoul(a.c_str() + 6, nullptr, 10);
        else if (a.rfind(L"--out=", 0) == 0) out = a.substr(6);
        else if (a.rfind(L"--interval=", 0) == 0) interval = std::wcstoul(a.c_str() + 11, nullptr, 10);
        else if (a.rfind(L"--duration=", 0) == 0) duration = std::wcstoul(a.c_str() + 11, nullptr, 10);
    }
    if (!pid || out.empty()) {
        fwprintf(stderr, L"usage: p0-counters --pid=<pid> --out=<csv> [--interval=ms] [--duration=sec]\n");
        return 2;
    }
    HANDLE target = OpenProcess(SYNCHRONIZE, FALSE, pid);
    const DWORD dwmPid = FindPid(L"dwm.exe");
    const std::wstring targetInst = ProcessInstanceName(pid);
    const DWORD cpus = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

    PDH_HQUERY q = nullptr;
    PdhOpenQueryW(nullptr, 0, &q);
    PDH_HCOUNTER cTargetGpu = nullptr, cDwmGpu = nullptr, cDwmCpu = nullptr, cTargetCpu = nullptr, cTargetMem = nullptr;
    auto add = [&](const std::wstring& path, PDH_HCOUNTER* c) {
        PDH_STATUS s = PdhAddEnglishCounterW(q, path.c_str(), 0, c);
        if (s != ERROR_SUCCESS) {
            fwprintf(stderr, L"counter add failed 0x%08X: %s\n", s, path.c_str());
            *c = nullptr;
        }
    };
    add(L"\\GPU Engine(pid_" + std::to_wstring(pid) + L"_*)\\Utilization Percentage", &cTargetGpu);
    add(L"\\GPU Engine(pid_" + std::to_wstring(dwmPid) + L"_*)\\Utilization Percentage", &cDwmGpu);
    add(L"\\Process(dwm)\\% Processor Time", &cDwmCpu);
    add(L"\\Process(" + targetInst + L")\\% Processor Time", &cTargetCpu);
    add(L"\\GPU Process Memory(pid_" + std::to_wstring(pid) + L"_*)\\Dedicated Usage", &cTargetMem);

    FILE* f = nullptr;
    if (_wfopen_s(&f, out.c_str(), L"wb") != 0 || !f) return 3;
    fprintf(f, "# p0-counters target_pid=%lu dwm_pid=%lu logical_processors=%lu interval_ms=%lu\n", pid, dwmPid, cpus,
            interval);
    fputs("timestamp_utc,target_gpu_util_pct,dwm_gpu_util_pct,dwm_cpu_pct_total,target_cpu_pct_total,"
          "target_gpu_dedicated_mb\n",
          f);
    PdhCollectQueryData(q);

    const ULONGLONG start = GetTickCount64();
    for (;;) {
        if (target && WaitForSingleObject(target, interval) == WAIT_OBJECT_0) break;
        if (!target) Sleep(interval);
        // GPU Engine 인스턴스는 대상이 새 엔진을 쓰기 시작하면 늘어나므로 와일드카드 재확장
        PdhCollectQueryData(q);
        SYSTEMTIME st;
        GetSystemTime(&st);
        const double dwmCpu = cDwmCpu ? Single(cDwmCpu) : -1;
        const double tCpu = cTargetCpu ? Single(cTargetCpu) : -1;
        const double mem = cTargetMem ? SumArray(cTargetMem) : -1;
        fprintf(f, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ,%s,%s,%s,%s,%s\n", st.wYear, st.wMonth, st.wDay, st.wHour,
                st.wMinute, st.wSecond, st.wMilliseconds, Fmt(cTargetGpu ? SumArray(cTargetGpu) : -1).c_str(),
                Fmt(cDwmGpu ? SumArray(cDwmGpu) : -1).c_str(), Fmt(dwmCpu < 0 ? -1 : dwmCpu / cpus).c_str(),
                Fmt(tCpu < 0 ? -1 : tCpu / cpus).c_str(), Fmt(mem < 0 ? -1 : mem / 1048576.0).c_str());
        fflush(f);
        if (duration && GetTickCount64() - start >= duration * 1000ULL) break;
    }
    fclose(f);
    PdhCloseQuery(q);
    if (target) CloseHandle(target);
    return 0;
}
