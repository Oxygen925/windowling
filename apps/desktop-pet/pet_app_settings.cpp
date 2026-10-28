// 설정 창 (UI-07): 열기, 저장(설정 파일 다시 쓰기 + .bak), 시작 프로그램 등록
#include "pet_app.h"

#include <shellapi.h>
#include <shlobj.h>

#include <wil/resource.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "diagnostics/log.h"
#include "platform/windows/strings.h"
#include "startup_run.h"
#include "ui/text.h"
#include "ai/json_util.h"
#include "dp_version.h"

namespace dp {

void PetApp::MaybeShowWelcome() {
    wchar_t v[4] = {};
    if (GetEnvironmentVariableW(L"DP_TEST_NO_WELCOME", v, 4)) return;
    const auto path = configPath_.parent_path() / L"ui_state.json";
    nlohmann::json j = nlohmann::json::object();
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            j = nlohmann::json::parse(in, nullptr, false);
            if (!j.is_object()) j = nlohmann::json::object();
        }
    }
    if (ai::JBool(j, "welcome_shown", false)) return;
    j["welcome_shown"] = true;
    std::ofstream(path, std::ios::binary | std::ios::trunc) << j.dump(2);
    log::Info("[ui] welcome first_run");
    ShowWelcome(false);  // 첫 실행: 다른 앱의 포커스를 빼앗지 않는다
}

namespace {
nlohmann::json ReadUiState(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return nlohmann::json::object();
    auto j = nlohmann::json::parse(in, nullptr, false);
    return j.is_object() ? j : nlohmann::json::object();
}
void WriteUiState(const std::filesystem::path& path, const nlohmann::json& j) {
    const std::wstring tmp = path.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << j.dump(2);
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
int64_t UnixNowSec() { return static_cast<int64_t>(std::time(nullptr)); }
}  // namespace

// ---------------------------------------------------------------------------
// 업데이트 확인 (P6-03, 2026-10-20 결정: 확인·알림만, 하루 1번, 기본 켬)
// ---------------------------------------------------------------------------

std::string PetApp::UpdateRepo() const { return options_.updateRepo.empty() ? std::string(DP_RELEASE_REPO) : options_.updateRepo; }

void PetApp::ScheduleUpdateCheck(int delaySec) {
    SetTimer(appWnd_.get(), kUpdateTimerId, static_cast<UINT>(std::max(1, delaySec)) * 1000u, nullptr);
}

void PetApp::OnUpdateTimer() {
    KillTimer(appWnd_.get(), kUpdateTimerId);
    const std::string repo = UpdateRepo();
    if (!config_.updateCheck || repo.empty()) {
        if (repo.empty()) log::Info("[update] 저장소가 정해지지 않은 빌드 — 확인 안 함");
        return;  // 설정에서 다시 켜면 ApplyConfig 가 다시 건다
    }
    const auto statePath = configPath_.parent_path() / L"ui_state.json";
    const auto j = ReadUiState(statePath);
    const int64_t last = ai::JInt(j, "update_checked_unix", 0), now = UnixNowSec();
    if (last > 0 && last <= now && now - last < 86400) {  // 하루 1번
        ScheduleUpdateCheck(static_cast<int>(std::min<int64_t>(86400 - (now - last), 6 * 3600)));
        return;
    }
    std::string url = "https://github.com/" + repo + "/releases/latest/download/latest.json";
    if (!options_.updateUrl.empty()) {
        if (!core::IsLoopbackUrl(options_.updateUrl)) {
            log::Warn("[update] --update-url 은 루프백만 — 확인 안 함");
            return;
        }
        url = options_.updateUrl;
    }
    if (updater_.Start(appWnd_.get(), WM_UPDATE_RESULT, url, repo)) log::Info("[update] check start");
}

void PetApp::OnUpdateResult() {
    const auto r = updater_.Take();
    if (!r) return;  // 결과 없이 온 메시지 (다른 프로세스 등)
    const auto statePath = configPath_.parent_path() / L"ui_state.json";
    auto j = ReadUiState(statePath);
    j["update_checked_unix"] = UnixNowSec();
    ScheduleUpdateCheck(6 * 3600);  // 켜 둔 채 며칠 지나도 하루 1번 (시각 조건은 OnUpdateTimer)
    if (!r->ok) {
        WriteUiState(statePath, j);
        log::Info("[update] check failed code={}", r->code);
        return;
    }
    const auto cmp = core::CompareVersions(r->info.version, DP_VERSION_STRING);
    log::Info("[update] check ok latest={} current={} newer={}", r->info.version, DP_VERSION_STRING, cmp && *cmp > 0);
    if (cmp && *cmp > 0) {
        updateAvailable_ = r->info;
        if (ai::JStr(j, "update_notified", "") != r->info.version) {  // 같은 버전은 한 번만 알린다
            tray_.ShowBalloon(ui::T("update.balloon_title").c_str(), ui::TF("update.balloon", ToWide(r->info.version)).c_str());
            j["update_notified"] = r->info.version;
            log::Info("[update] notified version={}", r->info.version);
        }
    } else {
        updateAvailable_.reset();
    }
    WriteUiState(statePath, j);
}

void PetApp::OpenUpdatePage() {
    if (!updateAvailable_ || !core::IsReleasePageUrl(updateAvailable_->page, UpdateRepo())) return;
    if (options_.testHooks) {  // 시험은 브라우저를 열지 않는다
        log::Info("[update] open page (test: not opened)");
        return;
    }
    ShellExecuteW(nullptr, L"open", ToWide(updateAvailable_->page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    log::Info("[update] open page");
}

// ---------------------------------------------------------------------------
// 모든 데이터 지우기 (P6-04)
// ---------------------------------------------------------------------------

void PetApp::WipeAllData() {
    if (wipePending_) return;
    HWND owner = settings_.IsOpen() ? settings_.Hwnd() : appWnd_.get();
    if (MessageBoxW(owner, ui::T("settings.wipe_confirm").c_str(), ui::T("settings.title").c_str(),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    wipePending_ = true;
    const bool signedIn = aiEnabled_ && account_.state == AccountState::SignedIn;
    log::Info("[wipe] start signed_in={}", signedIn);
    if (signedIn) {
        ai_.SignOut(false);  // 토큰 폐기. 응답(SignOutDone)에서 FinishWipe, 10초 안에 없으면 타이머로
        SetTimer(appWnd_.get(), kWipeTimerId, 10000, nullptr);
        return;
    }
    FinishWipe();
}

void PetApp::FinishWipe() {
    if (!wipePending_) return;
    wipePending_ = false;
    KillTimer(appWnd_.get(), kWipeTimerId);
    if (startup::IsEnabled()) startup::SetEnabled(false);
    observeOn_ = false;
    EnableTts(false);
    EnableAi(false);  // 작업 스레드를 멈춘 뒤 자격 증명 파일을 지운다

    namespace fs = std::filesystem;
    const fs::path dir = configPath_.parent_path();
    wil::unique_cotaskmem_string local;
    std::error_code ec;
    const bool isDefault = SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) &&
                           fs::equivalent(dir, fs::path(local.get()) / DP_APP_DIR_NAME, ec);
    log::Info("[wipe] remove app data default_dir={} — 로그를 닫고 지운 뒤 종료", isDefault);
    log::Shutdown();  // 로그 파일도 지우므로 먼저 닫는다 (이후 로그는 남지 않음)
    // 앱이 만든 것만 지운다 (--config 가 문서 폴더 같은 곳을 가리켜도 그 폴더를 통째로 지우지 않게)
    for (const wchar_t* name : {L"config.toml", L"config.toml.bak", L"config.toml.tmp", L"observe_state.json",
                                L"observe_state.json.tmp", L"ui_state.json", L"ui_state.json.tmp", L"test_install.txt"})
        fs::remove(dir / name, ec);
    for (const wchar_t* sub : {L"auth", L"characters", L"install_tmp"}) fs::remove_all(dir / sub, ec);
    if (isDefault) fs::remove_all(dir / L"logs", ec);
    fs::remove(dir, ec);  // 비어 있을 때만 지워진다
    PostQuitMessage(0);
}

void PetApp::ShowWelcome(bool activate) {
    WelcomeCallbacks cb;
    cb.openCharacters = [this] { OpenCharacters(); };
    cb.openSettings = [this] { OpenSettings(SettingsWindow::kGeneral); };
    welcome_.Show(std::move(cb), activate, library_.empty());
}

void PetApp::OpenSettings(SettingsWindow::Page page) {
    SettingsCallbacks cb;
    cb.apply = [this](const core::Config& draft, bool startup) { return ApplySettings(draft, startup); };
    cb.openFile = [this] { ShellExecuteW(nullptr, L"open", configPath_.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
    cb.startupEnabled = [] { return startup::IsEnabled(); };
    cb.wipe = [this] { WipeAllData(); };
    cb.models = [this] {
        std::vector<std::pair<std::string, std::wstring>> out;
        for (const auto& m : models_) out.emplace_back(m.slug, ToWide(m.displayName));
        return out;
    };
    // 기본 모델을 고를 수 있게 목록을 받아 둔다 (로그인했을 때만, 도착하면 설정 창이 다시 채운다)
    RequestModelsIfNeeded();
    settings_.Show(config_, page, std::move(cb));
}

bool PetApp::SaveConfigFile(const std::function<void(core::Config&)>& mutate, DWORD& error) {
    // 디스크의 설정 파일을 다시 읽어 바꿀 항목만 덮어쓴다: 실행 중 손으로 고친 다른 값·명령줄 재정의를 지키기 위해.
    // 파일이 없거나 문법 오류면 지금 값에서 시작한다 (원본은 .bak 에 남는다)
    core::Config disk = config_;
    {
        std::ifstream in(configPath_, std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            auto r = core::ParseConfig(ss.str());
            if (r.parsed) disk = std::move(r.config);
            else log::Warn("[settings] 설정 파일 문법 오류 — 지금 값으로 다시 씀 (원본은 .bak)");
        }
    }
    mutate(disk);

    const std::wstring tmp = configPath_.wstring() + L".tmp", bak = configPath_.wstring() + L".bak";
    bool written = false;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "# Desktop Pet 설정 — 앱이 저장함. 직접 쓴 주석은 저장할 때 사라진다 (이전 파일: config.toml.bak)\n"
            << core::SerializeConfig(disk);
        written = static_cast<bool>(out);
    }
    std::error_code ec;
    if (written && std::filesystem::exists(configPath_, ec) && !CopyFileW(configPath_.c_str(), bak.c_str(), FALSE))
        log::Warn("[settings] .bak 만들기 실패 err={}", GetLastError());
    if (!written || !MoveFileExW(tmp.c_str(), configPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = written ? GetLastError() : ERROR_WRITE_FAULT;
        DeleteFileW(tmp.c_str());
        log::Warn("[settings] 저장 실패 err={}", error);
        return false;
    }
    return true;
}

SettingsResult PetApp::ApplySettings(const core::Config& draft, bool startup) {
    DWORD err = 0;
    if (!SaveConfigFile([&](core::Config& c) { core::CopyUiFields(draft, c); }, err))
        return {false, ui::TF("settings.save_failed", static_cast<int>(err))};

    const core::Config old = config_;
    core::CopyUiFields(draft, config_);
    const auto changed = core::DiffUiFields(old, config_);
    notices_.clear();
    const auto restart = ApplyConfig(old);
    bool startupOk = true;
    if (startup != startup::IsEnabled()) startupOk = startup::SetEnabled(startup);

    // 로그에는 바뀐 항목 이름만 (값은 남기지 않는다: 명령 경로 등)
    std::string keys;
    for (const auto& k : changed) keys += (keys.empty() ? "" : ",") + k;
    log::Info("[settings] applied changed={} restart={} startup={}", keys.empty() ? "-" : keys, restart.size(), startup);

    std::wstring message;
    if (restart.empty()) {
        message = ui::T("settings.saved");
    } else {
        std::wstring list;
        for (const auto& r : restart) list += (list.empty() ? L"" : L", ") + r;
        message = ui::TF("settings.saved_restart", list);
    }
    if (!startupOk) message += L" " + ui::T("settings.startup_failed");
    for (const auto& n : notices_) message += L" " + n;
    return {true, message};
}

}  // namespace dp
