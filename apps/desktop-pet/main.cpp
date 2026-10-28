// Desktop Pet 정식 앱 (P1~P3). 부트스트랩: 단일 인스턴스, 로그, 설정, 명령행(개발·테스트용) → PetApp.
#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/config.h"
#include "core/log.h"
#include "diagnostics/log.h"
#include "pet_app.h"
#include "platform/windows/strings.h"
#include "dp_version.h"

namespace {

// 사용자 데이터는 %LOCALAPPDATA%\DesktopPet (기준선 §6.2)
std::filesystem::path UserDataDir() {
    wil::unique_cotaskmem_string local;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return std::filesystem::temp_directory_path();
    return std::filesystem::path(local.get()) / DP_APP_DIR_NAME;
}

// config.toml 이 없으면 기본값으로 만들고, 있으면 읽는다. 문제는 경고 로그만 남기고 기본값으로 진행 (D13과 같은 원칙)
dp::core::Config LoadOrCreateConfig(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        dp::core::Config defaults;
        std::ofstream out(path, std::ios::binary);
        out << dp::core::SerializeConfig(defaults);
        dp::log::Info("설정 파일 생성: {}", dp::ToUtf8(path.wstring()));
        return defaults;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    auto result = dp::core::ParseConfig(ss.str());
    for (const auto& w : result.warnings) dp::log::Warn("설정: {}", w);
    dp::log::Info("설정 로드: {} (경고 {}건)", dp::ToUtf8(path.wstring()), result.warnings.size());
    return result.config;
}

// 개발·테스트용 명령행: --pets=N --character=id --characters-dir=path --config=path --spawn=X,Y --spawn-step=DX --ai --ai-endpoint=http://127.0.0.1:N --ai-open-url-file=path --observe-interval-sec=N --observe-dump=path.jpg --no-behavior --force-walk --force-activity=jump|climb --metrics=path.csv --duration=초
dp::LaunchOptions ParseArgs() {
    dp::LaunchOptions o;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        auto val = [&](const wchar_t* key) -> const wchar_t* {
            const size_t n = wcslen(key);
            return a.compare(0, n, key) == 0 ? a.c_str() + n : nullptr;
        };
        if (auto v = val(L"--pets=")) o.pets = std::clamp(_wtoi(v), 1, 8);
        else if (auto c = val(L"--character=")) {
            o.character = dp::ToUtf8(c);
            std::transform(o.character.begin(), o.character.end(), o.character.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        } else if (auto f = val(L"--config=")) o.configPath = f;
        else if (auto st = val(L"--spawn-step=")) o.spawnStepX = _wtof(st);
        else if (auto v2 = val(L"--spawn=")) o.spawnSet = swscanf_s(v2, L"%lf,%lf", &o.spawnX, &o.spawnY) == 2;
        else if (a == L"--ai") o.aiForce = true;
        else if (a == L"--no-behavior") o.noBehavior = true;
        else if (a == L"--no-region-cache") o.noRegionCache = true;
        else if (a == L"--force-walk") o.forceWalk = true;
        else if (auto v4 = val(L"--duration=")) o.durationSec = _wtoi(v4);
#if DP_TEST_HOOKS
        // 시험 전용: 임의 경로에 파일을 쓰거나(덤프·측정) 동작을 바꾸는(모의 서버·캡처 제외 끔·훅) 인자.
        // 배포 빌드(DP_TEST_HOOKS=OFF)에는 들어가지 않는다
        else if (auto d = val(L"--characters-dir=")) o.charactersDir = d;
        else if (auto e = val(L"--ai-endpoint=")) o.aiEndpoint = dp::ToUtf8(e);
        else if (auto u = val(L"--ai-open-url-file=")) o.aiOpenUrlFile = u;
        else if (auto oi = val(L"--observe-interval-sec=")) o.observeIntervalSec = std::max(1, _wtoi(oi));
        else if (auto od = val(L"--observe-dump=")) o.observeDump = od;
        else if (a == L"--observe-no-exclude") o.observeNoExclude = true;
        else if (auto rd = val(L"--render-dump=")) o.renderDump = rd;
        else if (auto rs = val(L"--render-dump-stride=")) o.renderDumpStride = std::max(1, _wtoi(rs));
        else if (a == L"--test-hooks") o.testHooks = true;
        else if (auto fl = val(L"--frame-log=")) o.frameLog = fl;
        else if (auto v3 = val(L"--metrics=")) o.metricsCsv = v3;
        else if (auto uu = val(L"--update-url=")) o.updateUrl = dp::ToUtf8(uu);
        else if (auto ur = val(L"--update-repo=")) o.updateRepo = dp::ToUtf8(ur);
        else if (auto ud = val(L"--update-delay-sec=")) o.updateDelaySec = std::max(1, _wtoi(ud));
        else if (auto fa = val(L"--force-activity=")) o.forceActivity = dp::ToUtf8(fa);
#endif
        else dp::log::Warn("알 수 없는 인자: {}", dp::ToUtf8(a));
    }
    LocalFree(argv);
    return o;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // DLL 검색: 현재 폴더·PATH 를 빼고 앱 폴더·System32 만 (나중에 시스템 구성 요소가 여는 DLL 포함).
    // 정적 가져오기(로더가 main 전에 여는 DLL)는 P6 에서 링커 옵션·CRT 배포 방식과 함께 정한다 (보안 점검 2026-10-20)
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto dataDir = UserDataDir();
    std::error_code ec;
    std::filesystem::create_directories(dataDir / L"logs", ec);
    dp::log::Init((dataDir / L"logs").wstring());
    dp::log::Info("Desktop Pet {} channel={}", DP_VERSION_STRING, DP_CHANNEL_NAME);
    dp::core::SetLogSink([](dp::core::LogLevel level, const std::string& msg) {
        dp::log::Write(level == dp::core::LogLevel::Error ? "ERROR" : level == dp::core::LogLevel::Warn ? "WARN" : "INFO",
                       "[core] " + msg);
    });

    wil::unique_mutex_nothrow single;
    single.reset(CreateMutexW(nullptr, TRUE, L"Local\\" DP_CLASS_PREFIX L"App"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        dp::log::Info("이미 실행 중 — 종료");
        dp::log::Shutdown();
        return 4;
    }

    int code = 0;
    {
        const auto coInit = wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
        const dp::LaunchOptions options = ParseArgs();
        const auto configPath = options.configPath.empty() ? dataDir / L"config.toml"
                                                           : std::filesystem::absolute(options.configPath);
        dp::core::Config config = LoadOrCreateConfig(configPath);
        // 시나리오 전용: 언어 고정 (화면 문구를 확인하는 시험이 Windows 표시 언어에 흔들리지 않게)
        wchar_t testLang[8] = {};
        if (GetEnvironmentVariableW(L"DP_TEST_LANGUAGE", testLang, 8)) config.language = dp::ToUtf8(testLang);
        dp::PetApp app;
        code = app.Run(instance, config, configPath, options);
    }
    dp::log::Info("종료 code={}", code);
    dp::log::Shutdown();
    return code;
}
