#include "character_install.h"

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <vector>

#include "platform/windows/strings.h"

namespace dp::install {

namespace fs = std::filesystem;

namespace {

bool IsReparse(DWORD attrs) { return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0; }

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

// dir 바로 아래 항목 (., .. 제외)
template <class F>
bool ForEachChild(const fs::path& dir, F&& f) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir / L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    bool ok = true;
    do {
        if (std::wcscmp(fd.cFileName, L".") == 0 || std::wcscmp(fd.cFileName, L"..") == 0) continue;
        if (!f(fd)) {
            ok = false;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

}  // namespace

std::string CheckTree(const fs::path& dir, const core::ZipLimits& limits) {
    const DWORD rootAttrs = GetFileAttributesW(dir.c_str());
    if (rootAttrs == INVALID_FILE_ATTRIBUTES || !(rootAttrs & FILE_ATTRIBUTE_DIRECTORY)) return "폴더가 아님";
    if (IsReparse(rootAttrs)) return "링크(재분석 지점) 폴더";
    struct Item {
        fs::path path;
        int depth;
    };
    std::vector<Item> stack{{dir, 0}};
    size_t count = 0;
    uint64_t total = 0;
    std::string error;
    while (!stack.empty() && error.empty()) {
        const Item cur = stack.back();
        stack.pop_back();
        const bool listed = ForEachChild(cur.path, [&](const WIN32_FIND_DATAW& fd) {
            const fs::path p = cur.path / fd.cFileName;
            if (IsReparse(fd.dwFileAttributes)) return error = "링크(재분석 지점)가 들어 있음: " + ToUtf8(p.filename().wstring()), false;
            if (++count > limits.maxEntries) return error = "파일이 너무 많음", false;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (cur.depth + 1 > limits.maxDepth) return error = "폴더가 너무 깊음", false;
                stack.push_back({p, cur.depth + 1});
                return true;
            }
            const uint64_t size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            if (size > limits.maxFileBytes) return error = "파일이 너무 큼: " + ToUtf8(p.filename().wstring()), false;
            total += size;
            if (total > limits.maxTotalBytes) return error = "전체 크기가 너무 큼", false;
            return true;
        });
        if (!listed && error.empty()) error = "폴더를 읽을 수 없음";
    }
    return error;
}

std::string CopyTree(const fs::path& from, const fs::path& to) {
    if (!CreateDirectoryW(to.c_str(), nullptr)) return "폴더를 만들 수 없음 (오류 " + std::to_string(GetLastError()) + ")";
    std::string error;
    const bool ok = ForEachChild(from, [&](const WIN32_FIND_DATAW& fd) {
        if (IsReparse(fd.dwFileAttributes)) return error = "링크(재분석 지점)", false;  // CheckTree 뒤에 바뀐 경우
        const fs::path s = from / fd.cFileName, d = to / fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            error = CopyTree(s, d);
            return error.empty();
        }
        if (!CopyFileW(s.c_str(), d.c_str(), TRUE)) return error = "복사 실패 (오류 " + std::to_string(GetLastError()) + ")", false;
        return true;
    });
    if (!ok && error.empty()) error = "폴더를 읽을 수 없음";
    return error;
}

namespace {
// 지금까지 푼 크기·개수 (링크를 따라가지 않음)
void Measure(const fs::path& dir, uint64_t& bytes, size_t& count, int depth = 0) {
    if (depth > 16) return;
    ForEachChild(dir, [&](const WIN32_FIND_DATAW& fd) {
        ++count;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!IsReparse(fd.dwFileAttributes)) Measure(dir / fd.cFileName, bytes, count, depth + 1);
        } else {
            bytes += (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        }
        return true;
    });
}
}  // namespace

std::vector<std::wstring> ListFiles(const fs::path& dir) {
    std::vector<std::wstring> out;
    struct Item {
        fs::path path;
        std::wstring rel;
    };
    std::vector<Item> stack{{dir, L""}};
    while (!stack.empty() && out.size() < 100000) {
        const Item cur = stack.back();
        stack.pop_back();
        ForEachChild(cur.path, [&](const WIN32_FIND_DATAW& fd) {
            const std::wstring rel = cur.rel.empty() ? std::wstring(fd.cFileName) : cur.rel + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!IsReparse(fd.dwFileAttributes)) stack.push_back({cur.path / fd.cFileName, rel});
            } else {
                out.push_back(Lower(rel));
            }
            return true;
        });
    }
    return out;
}

std::string ExtractZip(const fs::path& zip, const fs::path& to, const core::ZipLimits& limits, int timeoutMs) {
    wchar_t sys[MAX_PATH] = {};
    if (!GetSystemDirectoryW(sys, MAX_PATH)) return "시스템 폴더를 찾을 수 없음";
    const std::wstring exe = std::wstring(sys) + L"\\tar.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return "Windows tar.exe 가 없음";
    std::wstring cmd = L"\"" + exe + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + to.wstring() + L"\"";

    // 표준 입출력은 NUL, 상속하는 핸들은 그것만 (다른 핸들이 새지 않게)
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul == INVALID_HANDLE_VALUE) return "NUL 을 열 수 없음";
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<uint8_t> attrBuf(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = nul;
    bool attrOk = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                  UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &nul, sizeof(nul), nullptr, nullptr);
    if (attrOk) si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi{};
    const BOOL started = attrOk && CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                                  CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, to.c_str(),
                                                  &si.StartupInfo, &pi);
    const DWORD startErr = GetLastError();
    if (attrOk) DeleteProcThreadAttributeList(attrs);
    CloseHandle(nul);
    if (!started) return "tar.exe 를 실행할 수 없음 (오류 " + std::to_string(startErr) + ")";
    std::string error;
    // 0.2초마다 푼 양을 확인한다: zip 목록에 적힌 크기는 실제 데이터와 다를 수 있어(압축 폭탄) 사전 검사만으로는 못 막는다
    const ULONGLONG start = GetTickCount64();
    DWORD wait = WAIT_TIMEOUT;
    while ((wait = WaitForSingleObject(pi.hProcess, 200)) == WAIT_TIMEOUT) {
        uint64_t bytes = 0;
        size_t count = 0;
        Measure(to, bytes, count);
        if (bytes > limits.maxTotalBytes || count > limits.maxEntries * 2) {
            error = "압축을 푼 크기가 상한을 넘음";
            break;
        }
        if (GetTickCount64() - start > static_cast<ULONGLONG>(timeoutMs)) {
            error = "압축 해제 시간 초과";
            break;
        }
    }
    if (!error.empty()) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    } else {
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        if (code != 0) error = "압축 해제 실패 (tar 종료 코드 " + std::to_string(code) + ")";
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return error;
}

fs::path FindManifestDir(const fs::path& dir) {
    if (GetFileAttributesW((dir / L"manifest.toml").c_str()) != INVALID_FILE_ATTRIBUTES) return dir;
    std::vector<fs::path> subdirs;
    bool other = false;
    ForEachChild(dir, [&](const WIN32_FIND_DATAW& fd) {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !IsReparse(fd.dwFileAttributes)) subdirs.push_back(dir / fd.cFileName);
        else other = true;
        return true;
    });
    if (subdirs.size() == 1 && !other && GetFileAttributesW((subdirs[0] / L"manifest.toml").c_str()) != INVALID_FILE_ATTRIBUTES)
        return subdirs[0];
    return {};
}

bool RemoveTree(const fs::path& dir, bool hardDelete) {
    std::error_code ec;
    if (hardDelete) {
        fs::remove_all(dir, ec);  // 링크 자체만 지우고 따라가지 않는다
        return !fs::exists(dir, ec);
    }
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOperation> op;
    ComPtr<IShellItem> item;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op))) ||
        FAILED(op->SetOperationFlags(FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE)) ||
        FAILED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&item))) ||
        FAILED(op->DeleteItem(item.Get(), nullptr)) || FAILED(op->PerformOperations()))
        return false;
    BOOL aborted = FALSE;
    op->GetAnyOperationsAborted(&aborted);
    return !aborted && !fs::exists(dir, ec);
}

bool IsInside(const fs::path& child, const fs::path& parent) {
    std::error_code ec;
    std::wstring c = Lower(fs::weakly_canonical(child, ec).wstring());
    std::wstring p = Lower(fs::weakly_canonical(parent, ec).wstring());
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    return c.size() > p.size() && c.compare(0, p.size(), p) == 0;
}

}  // namespace dp::install
