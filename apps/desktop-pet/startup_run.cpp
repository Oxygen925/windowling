#include "startup_run.h"

#include <windows.h>

#include <string>

#include "diagnostics/log.h"
#include "dp_version.h"

namespace dp::startup {

namespace {
bool g_test = false;
constexpr wchar_t kValue[] = DP_APP_DIR_NAME;  // 개발 빌드는 "DesktopPet-Dev"

const wchar_t* KeyPath() {
    return g_test ? L"Software\\DesktopPet\\TestRun" : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}
}  // namespace

void UseTestKey(bool on) { g_test = on; }

bool IsEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, KeyPath(), kValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

bool SetEnabled(bool on) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, KeyPath(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LSTATUS st;
    if (on) {
        std::wstring exe(MAX_PATH, L'\0');
        for (;;) {
            const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
            if (n < exe.size()) {
                exe.resize(n);
                break;
            }
            exe.resize(exe.size() * 2);
        }
        const std::wstring data = L"\"" + exe + L"\"";
        st = RegSetValueExW(key, kValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()),
                            static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, kValue);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    log::Info("[settings] startup {} {} ok={}", on ? "on" : "off", g_test ? "test-key" : "run-key", st == ERROR_SUCCESS);
    return st == ERROR_SUCCESS;
}

}  // namespace dp::startup
