#include "diagnostics/log.h"

#include <windows.h>

#include <cstdio>
#include <share.h>
#include <mutex>

namespace dp::log {

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
}  // namespace

void Init(const std::wstring& directory) {
    std::lock_guard lock(g_mutex);
    CreateDirectoryW(directory.c_str(), nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    // 파일 이름 앞부분 = 실행 파일 이름 (desktop-pet_…, p0-spike_…). P0 때 "p0-spike" 로 고정돼 있었다
    wchar_t exe[MAX_PATH] = L"app";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring stem = exe;
    stem = stem.substr(stem.find_last_of(L"\\/") + 1);
    stem = stem.substr(0, stem.find_last_of(L'.'));
    wchar_t name[64];
    swprintf_s(name, L"_%04u%02u%02u_%02u%02u%02u_%lu.log", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
               st.wSecond, GetCurrentProcessId());
    std::wstring path = directory + L"\\" + stem + name;
    // 다른 프로세스(측정 스크립트)가 실행 중에 읽을 수 있도록 공유 모드로 연다
    g_file = _wfsopen(path.c_str(), L"wb", _SH_DENYNO);
    // 보존 기한: 14일 지난 로그는 지운다 (개수가 아니라 날짜 기준 — 여러 번 실행하는 시험이 끝에 앞 로그를 모은다)
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now{{nowFt.dwLowDateTime, nowFt.dwHighDateTime}};
    const unsigned long long kKeep = 14ull * 24 * 3600 * 10'000'000;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((directory + L"\\*.log").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            ULARGE_INTEGER t{{fd.ftLastWriteTime.dwLowDateTime, fd.ftLastWriteTime.dwHighDateTime}};
            if (now.QuadPart > t.QuadPart && now.QuadPart - t.QuadPart > kKeep) DeleteFileW((directory + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

void Shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
}

void Write(const char* level, const std::string& message) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char prefix[48];
    sprintf_s(prefix, "%02u:%02u:%02u.%03u [%s] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level);
    std::string line = prefix + message + "\n";
    std::lock_guard lock(g_mutex);
    if (g_file) {
        fwrite(line.data(), 1, line.size(), g_file);
        fflush(g_file);
    }
    OutputDebugStringA(line.c_str());
}

std::string HrToString(long hr) {
    return std::format("0x{:08X}", static_cast<unsigned long>(hr));
}

}  // namespace dp::log
