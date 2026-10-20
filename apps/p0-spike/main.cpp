// P0 기술 검증 spike — docs/plan/desktop_pet_p0_detailed_plan_ko.md
// 종료 코드: 0 정상, 2 인자 오류, 3 초기화 실패, 4 중복 실행
#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

#include "app.h"
#include "diagnostics/log.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    dp::log::Init(dir + L"\\logs");

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    dp::Options options;
    std::string error;
    bool ok = dp::ParseOptions(argc, argv, options, error) && dp::ValidateOptions(options, error);
    LocalFree(argv);
    if (!ok) {
        dp::log::Error("옵션 오류: {}", error);
        dp::log::Shutdown();
        return 2;
    }

    // 단일 인스턴스 (측정 스크립트는 순차 실행)
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\DesktopPet.P0Spike");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        dp::log::Error("이미 실행 중");
        dp::log::Shutdown();
        if (mutex) CloseHandle(mutex);
        return 4;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int code;
    {
        dp::App app;
        code = app.Run(instance, options);
    }
    CoUninitialize();
    dp::log::Info("종료 code={}", code);
    dp::log::Shutdown();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return code;
}
