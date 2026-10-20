#include "pet_app.h"

#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>

#include "core/log.h"
#include "dp_version.h"
#include "diagnostics/log.h"
#include "platform/windows/strings.h"
#include "startup_run.h"
#include "ui/app_window.h"
#include "ui/text.h"
#include "ui/ui_common.h"

namespace dp {

namespace {

constexpr wchar_t kAppClass[] = L"DesktopPet.App";
constexpr wchar_t kPetClass[] = L"DesktopPet.Pet";
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr double kNoDeadline = 1e300;

enum DeadlineKey : core::Deadlines::Key { kDlCursor = 1, kDlBehavior, kDlResync, kDlEnd, kDlMetrics, kDlOrder, kDlBubble,
                                           kDlRedraw };

enum MenuId : UINT {
    ID_INFO = 100, ID_HIDE, ID_PAUSE, ID_OPEN_CONFIG, ID_OPEN_LOGS, ID_RESYNC, ID_EXIT,
    ID_REMOVE, ID_RELOAD, ID_OPEN_CHARACTERS, ID_CHAT, ID_AI_TOGGLE, ID_AI_DIAG,  // 112. P4 메뉴는 pet_app.h (113~)
    ID_ADD_BASE = 1000,   // + 캐릭터 index
    ID_SWAP_BASE = 2000,  // + 캐릭터 index
};

std::string Hex(core::WindowId id) { return std::format("0x{:X}", id); }

std::filesystem::path ExeDir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path();
}

}  // namespace

// ============================================================================
// 실행
// ============================================================================

int PetApp::Run(HINSTANCE instance, const core::Config& config, const std::filesystem::path& configPath,
                const LaunchOptions& options) {
    instance_ = instance;
    config_ = config;
    configPath_ = configPath;
    options_ = options;
    startup::UseTestKey(options_.testHooks);  // 시험은 실제 시작 프로그램(Run) 키를 건드리지 않는다
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&qpcStart_);
    clock_ = core::FixedStepClock(config_.physicsHz, 0.1);
    frameIntervalSec_ = 1.0 / config_.frameRate;
    cost_.Enable(!options_.metricsCsv.empty());

    ui::LoadLanguage(config_.language);  // UI-01: 메뉴·창 문구 (로그는 번역하지 않음)
    ui::SetThemePreference(config_.theme);
    if (!CreateAppWindow()) return 3;
    tray_.Add(appWnd_.get(), WM_TRAY, ui::T("tray.tip").c_str(), ui::AppIcon(true));
    if (!WTSRegisterSessionNotification(appWnd_.get(), NOTIFY_FOR_THIS_SESSION))
        log::Warn("WTSRegisterSessionNotification 실패 err={}", GetLastError());
    if (!InitGpu()) return 3;
    SpeechBubble::RegisterClass(instance_);

    observer_.SetExcludedSurfaceExe(config_.excludedSurfaceExe);
    if (!observer_.Start()) log::Warn("일부 WinEvent 훅 설치 실패");
    core::WorldSnapshot world;
    observer_.Flush(world);
    physics_.SetWorld(world);
    log::Info("월드 초기화: 창 {}개, 모니터 {}개", world.windows.size(), world.monitors.size());

    LoadCharacters();
    SpawnPets(SpawnList());  // 캐릭터가 0개여도 트레이 상주 유지 (D13)
    if (config_.ai.enabled || options_.aiForce) EnableAi(true);  // 기본 꺼짐: 켜지 않으면 AI 모듈을 초기화하지 않는다
    if (config_.tts.enabled) EnableTts(true);
    MaybeShowWelcome();
    ScheduleUpdateCheck(options_.updateDelaySec);

    hiResTimer_.reset(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS));
    if (!hiResTimer_) log::Warn("고해상도 타이머 생성 실패 — msgwait 폴백 (D14)");

    sampler_.Init();
    sampler_.SetAdapter(gpu_.Adapter());
    if (!options_.metricsCsv.empty()) {
        _wfopen_s(&metrics_, options_.metricsCsv.c_str(), L"wb");
        if (metrics_) {
            fprintf(metrics_, "# logical_processors=%u pets=%zu\n", MetricsSampler::LogicalProcessorCount(), pets_.size());
            fputs("t,proc_cpu_pct,proc_cpu_ms_delta,proc_cpu_pct_cycles,working_set_mb,private_mb,vram_local_mb,"
                  "present_count_delta,wakeups_delta,we_raw,we_accepted,z_rebuilds,rect_updates,snapshots,modes,"
                  "kc_pump,kc_world,kc_physics,kc_behavior,kc_cursor,kc_render,kc_region,kc_move,kc_bubble,"
                  "n_region_build,n_region_hit,n_move\n",
                  metrics_);
        }
    }

    const double t0 = Now();
    startTime_ = t0;
    deadlines_.Set(kDlResync, t0 + config_.resyncIntervalSec);
    deadlines_.Set(kDlBehavior, t0);
    if (config_.proximityReaction) deadlines_.Set(kDlCursor, t0);
    if (options_.durationSec > 0) deadlines_.Set(kDlEnd, t0 + options_.durationSec);
    if (metrics_) deadlines_.Set(kDlMetrics, t0 + 1.0);
    if (!options_.frameLog.empty() && _wfopen_s(&frameLog_, options_.frameLog.c_str(), L"wb") == 0 && frameLog_)
        fputs("t,pet,event,x,y\n", frameLog_);
    InitObserve(t0);  // 동의 기록 읽기, 켜져 있으면 다음 시도 예약 (꺼져 있으면 마감 없음)
    log::Info("시작: 펫 {}마리, 캐릭터 {}종, physics {}Hz, resync {}s, grace {}ms", pets_.size(), library_.size(),
              config_.physicsHz, config_.resyncIntervalSec, config_.occlusionGraceMs);

    // ---- 메인 루프 (단일 UI 스레드) ----
    while (!quit_) {
        double now = Now();
        // 물리·애니메이션 마감은 고해상도 one-shot, 그 외는 일반 timeout (D14)
        double anim = kNoDeadline;
        if (!Suspended()) {
            for (auto& p : pets_) {
                if (p->hiddenFullscreen) continue;
                if (core::Physics::NeedsStepping(p->body)) anim = std::min(anim, clock_.Started() ? clock_.NextStepAt() : now);
                const double next = p->anim.NextChangeAt(p->asset->pkg.sprite, now);
                if (std::isfinite(next)) anim = std::min(anim, next);
                if (p->presentPendingAt >= 0) anim = std::min(anim, p->presentPendingAt);
                if (p->asset->model && p->modelAnim >= 0) anim = std::min(anim, p->lastPresentAt + frameIntervalSec_);
            }
        }
        double other = deadlines_.Next().value_or(kNoDeadline);
        HANDLE handles[1];
        DWORD count = 0, timeoutMs = INFINITE;
        ArmWait(anim, other, now, timeoutMs, handles, count);
        MsgWaitForMultipleObjectsEx(count, handles, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        GlobalCounters().wakeups++;

        MSG msg;
        {
            CostMeter::Scope cs(cost_, CostPhase::Pump);  // WinEvent 훅 콜백·창 메시지 처리 포함
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) quit_ = true;
                if (chat_.IsDialogMsg(&msg)) continue;  // 대화창 Tab·Enter
                if (ui::AppWindow::RouteDialogMessage(&msg)) continue;  // 동의·설정 등 앱 창
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (quit_) break;
        now = Now();

        // 펌프 1회 동안 쌓인 창 이벤트를 한 번에 반영 (병합)
        {
            CostMeter::Scope cs(cost_, CostPhase::World);
            deadlines_.PopDue(kDlOrder, now);
            if (observer_.HasPending()) OnWorldChanged(now);
            if (observer_.OrderStale() && !deadlines_.Has(kDlOrder))
                deadlines_.Set(kDlOrder, now + WindowObserver::kOrderRefreshMs / 1000.0 + 0.005);
            if (observer_.TakeForegroundChanged()) ApplyFullscreen(now);
            if (deadlines_.PopDue(kDlResync, now)) {
                observer_.RequestFullResync();
                deadlines_.Set(kDlResync, now + config_.resyncIntervalSec);
                OnWorldChanged(now);
            }
        }
        if (!Suspended()) {
            {
                CostMeter::Scope cs(cost_, CostPhase::Physics);
                StepPhysics(now);
            }
            deadlines_.PopDue(kDlBehavior, now);
            deadlines_.PopDue(kDlBubble, now);  // 말풍선 자동 숨김은 RenderAndMove 에서 처리
            deadlines_.PopDue(kDlRedraw, now);  // GPU 재생성 직후 다시 그리기 (아래 RenderAndMove)
            {
                CostMeter::Scope cs(cost_, CostPhase::Behavior);
                UpdateBehavior(now);  // 가벼운 상태 갱신이라 매 기상마다 (다음 결정 시각은 마감으로 건다)
            }
            // 커서 근접: 커서 이동 이벤트가 오면 판정 (최대 30Hz), 그 밖에는 안전망 폴링만 (A4)
            const bool cursorMoved = observer_.TakeCursorMoved() && config_.proximityReaction;
            const bool cursorDue = deadlines_.PopDue(kDlCursor, now);
            if (cursorDue || (cursorMoved && now - lastCursorPoll_ >= 1.0 / 30)) {
                CostMeter::Scope cs(cost_, CostPhase::Cursor);
                PollCursor(now);
            }
            {
                CostMeter::Scope cs(cost_, CostPhase::Render);
                RenderAndMove(now);
            }
        } else {
            clock_.Reset(now);  // 정지 동안의 시간은 버린다
        }
        if (deadlines_.PopDue(kDlObserveKey, now)) ObserveTick(now);
        if (deadlines_.PopDue(kDlMetrics, now)) {
            SampleMetrics();
            deadlines_.Set(kDlMetrics, now + 1.0);
        }
        if (deadlines_.PopDue(kDlEnd, now)) break;
    }

    log::Info("종료");
    SetCaptureExcluded(false);
    EnableTts(false);
    EnableAi(false);  // 진행 중 요청 취소 + 작업 스레드 종료 (최대 2초)
    chat_.Destroy();
    if (metrics_) fclose(metrics_);
    if (frameLog_) fclose(frameLog_);
    observer_.Stop();
    WTSUnRegisterSessionNotification(appWnd_.get());
    DestroyAll();
    tray_.Remove();
    return 0;
}

void PetApp::FrameLog(const char* what, const PetActor& p, const RECT& r, double now) {
    if (frameLog_) fprintf(frameLog_, "%.4f,%d,%s,%ld,%ld\n", now, p.id, what, r.left, r.top);
}

double PetApp::Now() const {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart - qpcStart_.QuadPart) / qpcFreq_.QuadPart;
}

void PetApp::ArmWait(double anim, double other, double now, DWORD& timeoutMs, HANDLE* handles, DWORD& count) {
    count = 0;
    auto toMs = [now](double d) {
        return d >= kNoDeadline ? INFINITE : static_cast<DWORD>(std::ceil(std::max(0.0, d - now) * 1000.0));
    };
    if (hiResTimer_ && anim < kNoDeadline) {
        LARGE_INTEGER due;
        due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>((anim - now) * 1e7));
        SetWaitableTimer(hiResTimer_.get(), &due, 0, nullptr, nullptr, FALSE);
        handles[count++] = hiResTimer_.get();
        timeoutMs = toMs(other);
        return;
    }
    if (hiResTimer_) CancelWaitableTimer(hiResTimer_.get());  // 정지: 고해상도 타이머 해제
    timeoutMs = toMs(std::min(anim, other));
}

// ============================================================================
// 초기화
// ============================================================================

bool PetApp::CreateAppWindow() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &PetApp::StaticAppProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kAppClass;
    RegisterClassExW(&wc);
    WNDCLASSEXW pc{sizeof(pc)};
    pc.lpfnWndProc = &PetApp::StaticPetProc;
    pc.hInstance = instance_;
    pc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    pc.lpszClassName = kPetClass;
    RegisterClassExW(&pc);
    // TaskbarCreated 브로드캐스트를 받기 위해 숨김 최상위 창 (HWND_MESSAGE 아님)
    appWnd_.reset(CreateWindowExW(WS_EX_TOOLWINDOW, kAppClass, L"Desktop Pet", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                  instance_, this));
    return appWnd_ != nullptr;
}

bool PetApp::InitGpu() {
#ifdef _DEBUG
    const bool debugLayer = true;
#else
    const bool debugLayer = false;
#endif
    if (!gpu_.Create(debugLayer)) return false;
    for (auto& [_, a] : library_)
        if (!a->InitGpu(gpu_.Device())) return false;
    bubbleRenderer_.Init(gpu_.D2D(), gpu_.DWrite());  // 실패해도 말풍선만 표시 안 됨
    return true;
}

// 캐릭터 패키지 검색·검증·로드 (P2-01, P2-06). 실패 패키지는 로그만 남기고 건너뛴다 (D12·D13)
void PetApp::LoadCharacters() {
    library_.clear();
    libraryOrder_.clear();
    std::vector<std::filesystem::path> roots = {ExeDir() / L"characters", configPath_.parent_path() / L"characters"};
    std::error_code ec;
    std::filesystem::create_directories(roots[1], ec);  // 사용자 캐릭터 폴더 (P2-R8)
    if (!options_.charactersDir.empty()) roots.push_back(options_.charactersDir);
    rejected_.clear();
    thumbs_.clear();
    for (const auto& dir : core::FindPackageDirs(roots)) {
        std::vector<std::string> errors;
        auto asset = LoadAsset(dir, errors, true);
        if (!asset) {
            log::Error("[library] rejected dir={} errors={}", ToUtf8(dir.wstring()), errors.size());
            for (const auto& e : errors) log::Error("[library]   - {}", e);
            rejected_.push_back({dir, std::move(errors)});
            continue;
        }
        const auto& p = asset->pkg;
        if (library_.contains(p.id)) log::Warn("[library] id {} 중복 — 나중 경로({}) 사용", p.id, ToUtf8(dir.wstring()));
        else libraryOrder_.push_back(p.id);
        log::Info("[library] loaded id={} name={} version={} renderer={} lua={} clips={} license={} root={}", p.id, p.name,
                  p.version, p.IsModel() ? "gltf" : "sprite", p.behavior.scriptSource.empty() ? "off" : "on",
                  p.IsModel() ? asset->model->actions.size() : p.sprite.clips.size(), p.license.name, ToUtf8(dir.wstring()));
        library_[p.id] = std::move(asset);
    }
    log::Info("[library] 캐릭터 {}개 로드, {}개 거부", library_.size(), rejected_.size());
    characters_.Refresh();  // 캐릭터 관리 창이 열려 있으면 목록 갱신
}

// 패키지 1개 검사·로드 (P2-01, P5): manifest·경로·크기 → 아틀라스 디코딩·크기 또는 glb 모델 → (gpu) GPU 자원.
// 설치 검사(UI-08)도 같은 함수를 써서 "설치가 받아들임" = "불러오기가 받아들임" 이 되게 한다
std::shared_ptr<CharacterAsset> PetApp::LoadAsset(const std::filesystem::path& dir, std::vector<std::string>& errors, bool gpu) {
    auto r = core::LoadPackage(dir);
    for (const auto& w : r.warnings) log::Warn("[library] {} 경고: {}", ToUtf8(dir.filename().wstring()), w);
    errors = r.errors;
    if (!r.package) return nullptr;
    auto asset = std::make_shared<CharacterAsset>();
    asset->pkg = std::move(*r.package);
    if (asset->pkg.IsModel()) {
        asset->model = BuildModelAsset(asset->pkg, errors);  // 3D (P5)
    } else if (!asset->sheet.Load(asset->pkg.sprite.atlas.wstring(), asset->pkg.sprite.frameWidth)) {
        errors.push_back("아틀라스 디코딩 실패");
    } else {
        auto e = core::ValidateAtlasSize(asset->pkg, asset->sheet.Width(), asset->sheet.Height());
        errors.insert(errors.end(), e.begin(), e.end());
    }
    if (errors.empty() && gpu && !asset->InitGpu(gpu_.Device())) errors.push_back("GPU 자원 생성 실패");
    return errors.empty() ? asset : nullptr;
}

std::vector<core::PetSpawn> PetApp::SpawnList() const {
    if (options_.pets > 0 || !options_.character.empty()) {
        std::string id = options_.character;
        if (id.empty()) id = config_.pets.empty() ? std::string("windowling") : config_.pets.front().character;
        return {{id, std::clamp(options_.pets, 1, core::Config::kMaxPets)}};
    }
    return config_.pets;
}

void PetApp::SpawnPets(const std::vector<core::PetSpawn>& list) {
    int total = 0;
    for (const auto& s : list) total += s.count;
    int slot = 0;
    for (const auto& s : list) {
        auto it = library_.find(s.character);
        if (it == library_.end()) {
            log::Error("[pets] 알 수 없는 캐릭터 '{}' — {}마리 건너뜀", s.character, s.count);
            slot += s.count;
            continue;
        }
        for (int i = 0; i < s.count; ++i) CreatePet(it->second, slot++, std::max(total, 1));
    }
    if (pets_.empty()) log::Warn("[pets] 실행할 펫이 없음 — 트레이만 상주 (D13)");
}

void PetApp::SetupBehavior(PetActor& pet) {
    const auto& pkg = pet.asset->pkg;
    const uint32_t seed = static_cast<uint32_t>(std::hash<std::string>{}(pkg.id) ^ (pet.id * 2654435761u) ^ GetTickCount());
    pet.lua.reset();
    if (!pkg.behavior.scriptSource.empty()) {
        std::string err;
        pet.lua = script::LuaBehavior::Create(pkg.behavior.scriptSource, pkg.id + "/custom.lua", seed, {}, err);
        if (!pet.lua) log::Warn("[pet{}] Lua 로드 실패 → 기본 행동: {}", pet.id, err);
    }
    pet.fsm = std::make_unique<core::BehaviorMachine>(pkg.behavior, seed, pet.lua.get());
    pet.anim = core::AnimationPlayer{};
    pet.loggedActivity.clear();
    pet.loggedLuaErrors = 0;
    pet.loggedLuaDisabled = false;
}

PetActor* PetApp::CreatePet(const std::shared_ptr<CharacterAsset>& asset, int slot, int slots) {
    auto pet = std::make_unique<PetActor>();
    pet->id = nextPetId_++;
    pet->asset = asset;
    const auto& world = physics_.World();
    // 시작 위치: 지정값 또는 주 모니터 작업 영역 위쪽에 고르게 나눠 떨어뜨린다
    core::RectI work{0, 0, 1920, 1040};
    if (!world.monitors.empty()) work = world.monitors[0].work;
    pet->body.foot = options_.spawnSet
                         ? core::PointD{options_.spawnX + options_.spawnStepX * slot, options_.spawnY}
                         : core::PointD{work.left + work.Width() * (slot + 1.0) / (slots + 1.0), work.top + 200.0};
    pet->body.mode = core::BodyMode::Falling;
    UpdatePetSize(*pet);
    SetupBehavior(*pet);

    const RECT r = RectFor(*pet);
    pet->hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kPetClass, L"pet", WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, instance_, this);
    if (!pet->hwnd) {
        log::Error("펫 창 생성 실패 err={}", GetLastError());
        return nullptr;
    }
    pet->target = std::make_unique<CompositionTarget>();
    if (!pet->target->Create(gpu_, pet->hwnd, ContentMode::SwapChain, r.right - r.left, r.bottom - r.top, false, false)) {
        DestroyWindow(pet->hwnd);
        return nullptr;
    }
    pet->lastRect = r;
    // 같은 캐릭터 펫은 텍스처·CPU 사본을 공유한다 (use_count = 라이브러리 1 + 펫 수)
    log::Info("[pet{}] spawn character={} lua={} asset_refs={} hwnd={}", pet->id, asset->pkg.id, pet->lua ? "on" : "off",
              asset.use_count(), Hex(reinterpret_cast<core::WindowId>(pet->hwnd)));
    pets_.push_back(std::move(pet));
    return pets_.back().get();
}

void PetApp::RemovePet(PetActor* pet) {
    auto it = std::find_if(pets_.begin(), pets_.end(), [pet](auto& p) { return p.get() == pet; });
    if (it == pets_.end()) return;
    log::Info("[pet{}] remove character={}", pet->id, pet->asset->pkg.id);
    if (activePetId_ == pet->id) StopChat();
    if (observePetId_ == pet->id) ObserveInterrupted("pet_removed");
    tts_.StopAll();
    if (chatPetId_ == pet->id) {
        chatPetId_ = -1;
        chat_.Hide();
    }
    (*it)->bubble.reset();
    (*it)->target.reset();
    if ((*it)->hwnd) DestroyWindow((*it)->hwnd);
    if (menuTarget_ == pet) menuTarget_ = nullptr;
    pets_.erase(it);
}

void PetApp::SwapCharacter(PetActor& pet, const std::shared_ptr<CharacterAsset>& asset) {
    if (pet.asset == asset) return;
    log::Info("[pet{}] swap {} → {}", pet.id, pet.asset->pkg.id, asset->pkg.id);
    if (activePetId_ == pet.id) StopChat();
    pet.conv.Clear();  // 페르소나가 바뀌므로 대화를 새로 시작한다
    if (pet.bubble) pet.bubble->Hide();
    if (chatPetId_ == pet.id) {
        chat_.SetTarget(ToWide(asset->pkg.name));
        chat_.SetHistory(L"");
    }
    pet.asset = asset;
    UpdatePetSize(pet);
    SetupBehavior(pet);
    pet.shownFrame = -1;  // 다른 텍스처로 다시 그린다
    pet.modelView.reset();
    pet.modelAnim = -2;
    pet.regionHash = 0;
    if (pet.body.mode != core::BodyMode::Dragged) {
        pet.body.mode = core::BodyMode::Falling;  // 크기가 바뀌었을 수 있어 다시 떨어뜨린다
        pet.body.host = core::kNoWindow;
    }
}

// 패키지를 다시 읽고 같은 구성으로 다시 띄운다 (P2: 파일 감시 대신 수동 다시 읽기)
void PetApp::ReloadCharacters() {
    std::vector<core::PetSpawn> list;
    for (auto& p : pets_) {
        if (!list.empty() && list.back().character == p->asset->pkg.id) list.back().count++;
        else list.push_back({p->asset->pkg.id, 1});
    }
    while (!pets_.empty()) RemovePet(pets_.back().get());
    for (auto& [_, a] : library_) a->ReleaseGpu();
    LoadCharacters();
    SpawnPets(list.empty() ? SpawnList() : list);
    clock_.Reset(Now());
}

void PetApp::DestroyAll() {
    for (auto& p : pets_) {
        p->bubble.reset();
        p->target.reset();
        if (p->hwnd) DestroyWindow(p->hwnd);
    }
    pets_.clear();
    for (auto& [_, a] : library_) a->ReleaseGpu();
    library_.clear();
    bubbleRenderer_.Release();
    gpu_.Release();
}

// ============================================================================
// 월드·물리
// ============================================================================

void PetApp::OnWorldChanged(double now) {
    core::WorldSnapshot world;
    if (!observer_.Flush(world)) return;
    physics_.SetWorld(world);  // 착지 위 공간은 펫마다 자기 키로 판정 (크기가 다른 캐릭터 혼합)
    for (auto& p : pets_) {
        UpdatePetSize(*p);
        if (p->hiddenFullscreen || p->body.mode == core::BodyMode::Dragged) continue;
        LogBody(*p, physics_.Revalidate(p->body, ParamsFor(*p), now));
    }
    ApplyFullscreen(now);  // 전경 창의 크기·위치 변화(게임의 전체화면 전환 등)도 판정 계기
}

core::PhysicsParams PetApp::ParamsFor(const PetActor& pet) const {
    const double s = pet.dpi / 96.0 * config_.scale;
    const auto& b = pet.asset->pkg.behavior;  // P2: 캐릭터 패키지 값 우선
    core::PhysicsParams p;
    p.gravity = config_.gravityPxPerSec2 * s;
    p.maxFallSpeed = 3000.0 * s;
    p.walkSpeed = b.walkSpeed * s;
    p.footSupportRatio = b.footSupportRatio;
    p.occlusionGraceSec = config_.occlusionGraceMs / 1000.0;
    return p;
}

void PetApp::StepPhysics(double now) {
    bool any = false;
    for (auto& p : pets_)
        if (!p->hiddenFullscreen && core::Physics::NeedsStepping(p->body)) any = true;
    if (!any) {
        clock_.Reset(now);  // 완전 정지: 이벤트까지 물리 정지, 누적 시간 없음 (P1-D6)
        return;
    }
    const int steps = clock_.Advance(now);
    for (int i = 0; i < steps; ++i)
        for (auto& p : pets_) {
            if (p->hiddenFullscreen) continue;
            LogBody(*p, physics_.Step(p->body, ParamsFor(*p), clock_.StepSec(), now));
        }
}

void PetApp::LogBody(PetActor& pet, const std::vector<core::BodyEvent>& events) {
    for (const auto& e : events) {
        static const char* kinds[] = {"Landed", "Detached", "Turned", "Relocated"};
        if (e.kind == core::BodyEvent::Kind::Turned) continue;  // 걷기 방향 전환은 기록하지 않음 (빈번)
        // 실행 파일 이름은 남기지 않는다 (앱 사용 이력이 로그에 쌓이지 않게, 보안 점검 2026-10-20)
        log::Info("[pet{}] event={} window={} reason={} foot=({:.0f},{:.0f})", pet.id, kinds[static_cast<int>(e.kind)],
                  Hex(e.window), e.reason, pet.body.foot.x, pet.body.foot.y);
        if (e.kind == core::BodyEvent::Kind::Landed && pet.fsm)
            pet.fsm->OnEvent("landed", Now(), pet.body.mode == core::BodyMode::Attached);
    }
    if (pet.body.mode != pet.loggedMode || pet.body.host != pet.loggedHost) {
        log::Info("[pet{}] mode={} host={} localX={:.0f} foot=({:.0f},{:.0f})", pet.id, core::ToString(pet.body.mode),
                  Hex(pet.body.host), pet.body.localX, pet.body.foot.x, pet.body.foot.y);
        pet.loggedMode = pet.body.mode;
        pet.loggedHost = pet.body.host;
    }
}

void PetApp::UpdatePetSize(PetActor& pet) {
    const auto& world = physics_.World();
    const int mi = core::MonitorIndexFor(world, pet.body.foot);
    const uint32_t dpi = mi >= 0 ? world.monitors[mi].dpi : 96;
    if (dpi != pet.dpi && pet.body.width > 0) log::Info("[pet{}] DPI {}→{}", pet.id, pet.dpi, dpi);
    pet.dpi = dpi;
    if (const ModelAsset* m = pet.asset->model.get()) {  // 3D: 높이는 base_height, 너비는 모델 비율 (P5)
        pet.body.height = static_cast<int>(std::lround(pet.asset->pkg.model.baseHeight * config_.scale * dpi / 96.0));
        pet.body.width = std::max(8, static_cast<int>(std::lround(pet.body.height * m->aspect)));
        return;
    }
    const int size = static_cast<int>(std::lround(pet.asset->pkg.sprite.baseSize * config_.scale * dpi / 96.0));
    pet.body.width = pet.body.height = size;
}

// ============================================================================
// 행동 (P2: 패키지 FSM + Lua 훅)
// ============================================================================

void PetApp::UpdateBehavior(double now) {
    double next = kNoDeadline;
    for (auto& p : pets_) {
        auto& b = p->body;
        const bool resting = b.mode == core::BodyMode::Grounded || b.mode == core::BodyMode::Attached;
        if (p->hiddenFullscreen) continue;
        if (paused_ || options_.noBehavior) {
            // 자율 결정만 막는다. 클릭 등 이벤트 반응(짧은 sit/look)은 끝날 때까지 두되 이동(walk)은 막는다
            if (p->fsm->Activity() == "walk" || p->fsm->DecisionAt() <= now) p->fsm->Force({"idle", 3600.0, 0}, now);
        } else if (options_.forceWalk && resting) {
            if (p->fsm->Activity() != "walk" || p->fsm->DecisionAt() <= now) p->fsm->Force({"walk", 3600.0, 1}, now);
        } else {
            p->fsm->Update(now, resting, b.mode == core::BodyMode::Attached);
        }
        const bool walking = resting && p->fsm->Activity() == "walk";
        if (!walking) b.walkDir = 0;
        else if (b.walkDir == 0 || p->fsm->DecisionAt() != p->appliedDecisionAt) b.walkDir = p->fsm->WalkDir();
        p->appliedDecisionAt = p->fsm->DecisionAt();
        if (p->speaking) b.walkDir = 0;  // 말하는 동안은 멈춘다 (P3)
        if (b.walkDir != 0) p->facingLeft = b.walkDir < 0;
        if (p->fsm->Activity() != p->loggedActivity && resting) {
            log::Info("[pet{}] act={} dir={} for={:.1f}s hooks={} rejected={}", p->id, p->fsm->Activity(), p->fsm->WalkDir(),
                      p->fsm->DecisionAt() - now, p->fsm->HookDecisions(), p->fsm->RejectedHookDecisions());
            p->loggedActivity = p->fsm->Activity();
        }
        if (resting) next = std::min(next, p->fsm->DecisionAt());
        LogLua(*p);
    }
    if (next < kNoDeadline) deadlines_.Set(kDlBehavior, std::max(next, now + 0.001));
    else deadlines_.Clear(kDlBehavior);
}

// Lua 런타임 오류(명령 수·메모리 제한 포함)와 비활성화를 기록한다
void PetApp::LogLua(PetActor& p) {
    if (!p.lua) return;
    if (p.lua->Errors() != p.loggedLuaErrors) {
        log::Warn("[pet{}] lua error count={} last={}", p.id, p.lua->Errors(), p.lua->LastError());
        p.loggedLuaErrors = p.lua->Errors();
    }
    if (p.lua->Disabled() && !p.loggedLuaDisabled) {
        log::Warn("[pet{}] lua disabled errors={} → 기본 FSM", p.id, p.lua->Errors());
        p.loggedLuaDisabled = true;
    }
}

std::string PetApp::ActionFor(const PetActor& p) const {
    switch (p.body.mode) {
        case core::BodyMode::Dragged: return "drag";
        case core::BodyMode::Falling: return "fall";
        default: break;
    }
    if (p.speaking) return "talk";  // 없으면 AnimationPlayer 가 idle 로 대체
    const std::string& act = p.fsm->Activity();
    if (p.cursorNear && act != "walk") return "look";
    return act;
}

// ============================================================================
// 설정 반영 (UI-07). 바로 반영할 수 있는 항목은 여기서, 아니면 "다시 시작 필요" 목록으로 돌려준다.
// 관찰·AI 시간 제한·말풍선·이력·전체화면 예외는 쓸 때마다 config_ 를 읽어 따로 할 일이 없다.
// ============================================================================

std::vector<std::wstring> PetApp::ApplyConfig(const core::Config& old) {
    std::vector<std::wstring> restart;
    const double now = Now();
    if (old.language != config_.language) {
        ui::LoadLanguage(config_.language);
        chat_.ReloadTexts();
        ui::AppWindow::ReloadTextsAll();
        UpdateObserveIndicators();  // 트레이 툴팁·대화창 관찰 표시
    }
    if (old.theme != config_.theme) {
        ui::SetThemePreference(config_.theme);
        chat_.ApplyTheme();
        ui::AppWindow::ApplyThemeAll();
    }
    if (old.frameRate != config_.frameRate) frameIntervalSec_ = 1.0 / config_.frameRate;
    if (!old.updateCheck && config_.updateCheck) ScheduleUpdateCheck(5);
    if (!old.proximityReaction && config_.proximityReaction) deadlines_.Set(kDlCursor, now);
    if (old.ai.hotkey != config_.ai.hotkey) ReRegisterAiHotkey();
    if (TurnOffIfScopeWidened("settings")) notices_.push_back(ui::T("settings.observe_rescoped"));
    if (observeOn_ && (old.observe.intervalMinMin != config_.observe.intervalMinMin ||
                       old.observe.intervalMaxMin != config_.observe.intervalMaxMin))
        ScheduleObserve(now);
    // 음성: 켜져 있으면 엔진 설정을 새로 (시작할 때 켜기·대화 응답 읽기는 엔진과 무관)
    auto engine = [](core::TtsConfig t) {
        t.enabled = t.readChatReplies = false;
        return t;
    };
    if (ttsOn_ && !(engine(old.tts) == engine(config_.tts))) {
        EnableTts(false);
        EnableTts(true);
    }
    // 펫 크기는 창·영역·3D 카메라가 모두 크기에 걸려 있어 다시 시작할 때 적용 (실행 중 경로는 시험하지 않음)
    if (old.scale != config_.scale) restart.push_back(ui::T("settings.restart_scale"));
    return restart;
}

// ============================================================================
// 커서 근접 (저주기 확인, 근접 시 33ms / 멀면 200ms)
// ============================================================================

void PetApp::PollCursor(double now) {
    lastCursorPoll_ = now;
    if (!config_.proximityReaction) return;  // 설정 창에서 끈 뒤에는 다시 걸지 않는다
    POINT pt;
    GetCursorPos(&pt);
    bool anyNear = false, anyMoving = false;
    for (auto& p : pets_) {
        const double cx = p->body.foot.x, cy = p->body.foot.y - p->body.height / 2.0;
        const bool n = std::hypot(pt.x - cx, pt.y - cy) < p->body.width * 1.2;
        if (n && !p->cursorNear && p->fsm) p->fsm->OnEvent("cursor_near", now, p->body.mode == core::BodyMode::Attached);
        p->cursorNear = n;
        anyNear |= n;
        anyMoving |= core::Physics::NeedsStepping(p->body);
    }
    // 안전망 폴링: 근접 중 33ms(벗어남 감지), 펫이 움직이는 중 200ms(커서 쪽으로 걸어온 경우), 그 밖에는 1초.
    // 커서가 움직이면 WinEvent(OBJID_CURSOR) 로 바로 다시 판정한다
    deadlines_.Set(kDlCursor, now + (anyNear ? 0.033 : anyMoving ? 0.2 : 1.0));
}

// ============================================================================
// 전체화면 숨김 (P1-07)
// ============================================================================

void PetApp::ApplyFullscreen(double now) {
    const auto& world = physics_.World();
    fullscreen_.Update(config_, world.monitors);
    const auto& hidden = fullscreen_.HiddenMonitors();
    for (auto& p : pets_) {
        const int mi = core::MonitorIndexFor(world, p->body.foot);
        const bool hide = mi >= 0 && mi < static_cast<int>(hidden.size()) && hidden[mi];
        if (hide == p->hiddenFullscreen) continue;
        p->hiddenFullscreen = hide;
        if (hide) {
            // 렌더 중지가 아니라 창 자체를 숨긴다 (P0 E6: 가려진 펫 창만으로 게임이 합성 모드로 바뀐 사례)
            ShowWindow(p->hwnd, SW_HIDE);
            p->visible = false;
            if (p->bubble) p->bubble->SuspendWindow();
            if (observePetId_ == p->id) ObserveInterrupted("fullscreen");
            tts_.StopAll();  // 재생 중인 음성도 멈춘다 (관찰 요청이 끝난 뒤의 발화·대화 읽기 포함)
            log::Info("[pet{}] fullscreen hide ({})", p->id, fullscreen_.Describe());
        } else {
            p->body.mode = core::BodyMode::Falling;  // 숨긴 사이 창 배치가 바뀌었을 수 있어 다시 떨어뜨린다
            p->body.host = core::kNoWindow;
            LogBody(*p, physics_.Revalidate(p->body, ParamsFor(*p), now));
            p->shownFrame = -1;  // 다시 그리기
            clock_.Reset(now);
            log::Info("[pet{}] fullscreen show", p->id);
        }
    }
}

// ============================================================================
// 렌더·이동
// ============================================================================

RECT PetApp::RectFor(const PetActor& p) const {
    const int w = p.body.width, h = p.body.height;
    const int left = static_cast<int>(std::lround(p.body.foot.x - w / 2.0));
    const int top = static_cast<int>(std::lround(p.body.foot.y)) - h;
    return {left, top, left + w, top + h};
}

void PetApp::RenderAndMove(double now) {
    // 위치만 바뀐 창은 SetWindowPos 만 (Present 없음). 여러 창은 한 번에 묶는다
    HDWP batch = BeginDeferWindowPos(static_cast<int>(pets_.size()));
    for (auto& up : pets_) {
        PetActor& p = *up;
        if (p.hiddenFullscreen || userHidden_) continue;
        const RECT r = RectFor(p);
        CharacterAsset& asset = *p.asset;
        if (!asset.model) p.anim.Play(ActionFor(p), now);
        const int frame = asset.model ? 0 : p.anim.FrameAt(asset.pkg.sprite, now);
        const bool sizeChanged = (r.right - r.left) != p.target->Width() || (r.bottom - r.top) != p.target->Height();
        if (sizeChanged) {
            HRESULT hr = p.target->Resize(r.right - r.left, r.bottom - r.top);
            if (GpuContext::IsDeviceLost(hr)) {
                EndDeferWindowPos(batch);
                RecreateGpu("resize");
                return;
            }
        }
        if (asset.model) {  // 3D (P5): 애니메이션은 프레임 간격마다, hit-test 는 readback Region
            const HRESULT hr = DrawModel(p, r, now, sizeChanged);
            if (GpuContext::IsDeviceLost(hr)) {
                EndDeferWindowPos(batch);
                RecreateGpu("present");
                return;
            }
        }
        bool contentChanged = !asset.model &&
                              (frame != p.shownFrame || p.facingLeft != p.shownFlip || sizeChanged || p.shownAsset != &asset);
        // 프레임 레이트 상한 (display.frame_rate): 직전 Present 이후 간격이 짧으면 미루고 그 시각에 깨어난다
        if (contentChanged && !sizeChanged && p.shownAsset == &asset && p.lastPresentAt >= 0 &&
            now - p.lastPresentAt < frameIntervalSec_) {
            p.presentPendingAt = p.lastPresentAt + frameIntervalSec_;
            contentChanged = false;
        }
        if (contentChanged) {
            p.presentPendingAt = -1;
            p.lastPresentAt = now;
            const SpritePlacement sp{frame, 0, 0, r.right - r.left, r.bottom - r.top, p.facingLeft};
            FrameTarget ft;
            HRESULT hr = p.target->Begin(ft);
            if (SUCCEEDED(hr)) {
                const float tint[4] = {1, 1, 1, 1};
                asset.renderer.Draw(gpu_.Context(), ft, sp, p.facingLeft, tint);
                hr = p.target->End();
            }
            if (GpuContext::IsDeviceLost(hr)) {
                EndDeferWindowPos(batch);
                RecreateGpu("present");
                return;
            }
            // hit-test B: 콘텐츠가 바뀐 경우에만 Region 설정 (ADR-09). R1: 캐시된 Region 의 복사본
            {
                CostMeter::Scope cs(cost_, CostPhase::Region);
                const core::AnimationClip* clip = nullptr;
                if (config_.hitRegion == "clip") {
                    auto it = asset.pkg.sprite.clips.find(p.anim.ResolvedClip(asset.pkg.sprite));
                    if (it != asset.pkg.sprite.clips.end()) clip = &it->second;
                }
                uint64_t key = 0;
                HRGN rgn = RegionFor(asset, sp, clip, key);
                if (key != p.regionKey || p.shownAsset != &asset) {
                    SetWindowRgn(p.hwnd, rgn, TRUE);
                    p.regionKey = key;
                } else {
                    DeleteObject(rgn);  // 같은 Region: 설정 생략 (SetWindowRgn 이 비용의 대부분 — M1)
                }
            }
            FrameLog("present", p, r, now);
            p.shownFrame = frame;
            p.shownFlip = p.facingLeft;
            p.shownSize = r.right - r.left;
            p.shownAsset = &asset;
        }
        bool moved = r.left != p.lastRect.left || r.top != p.lastRect.top || sizeChanged;
        // 걷기·낙하처럼 물리가 만드는 이동은 화면 갱신률(30Hz)로만 창을 옮긴다 (P1 §4.4 R2 비용 절감).
        // 부착된 창을 따라가는 이동과 드래그는 즉시 반영해 어긋나 보이지 않게 한다.
        const bool physicsMotion = p.body.mode == core::BodyMode::Falling || p.body.walkDir != 0;
        const long long tick = static_cast<long long>(now / frameIntervalSec_);
        if (moved && p.visible && physicsMotion && !sizeChanged) {
            // 3D (P5): 그린 반복에서만 창을 옮겨 창 위치와 내용이 항상 함께 바뀌게 한다. 틱으로 따로 옮기면 그리기와
            // 위상이 어긋나, 빠르게 떨어질 때 모델을 창 안에서 10px 넘게 밀어 그려 아래가 잘렸다 (사용자 보고)
            if (asset.model) moved = p.lastPresentAt == now;
            else if (tick == p.lastMoveTick) moved = false;
        }
        if (moved) p.lastMoveTick = tick;
        if (!p.visible) {
            // 표시·재표시 때만 topmost 를 설정한다 (다른 topmost 창과 경쟁하지 않음, P1-D2)
            CostMeter::Scope cs(cost_, CostPhase::Move);
            SetWindowPos(p.hwnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
            p.visible = true;
        } else if (moved && batch) {
            FrameLog("move", p, r, now);
            CostMeter::Scope cs(cost_, CostPhase::Move);
            batch = DeferWindowPos(batch, p.hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                                   SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        }
        if (moved || !p.visible) p.lastRect = r;
    }
    bool bubblesOk = true;
    {
        CostMeter::Scope cs(cost_, CostPhase::Bubble);
        bubblesOk = UpdateBubbles(now, batch);
    }
    if (batch) {
        CostMeter::Scope cs(cost_, CostPhase::Move);  // 묶인 창 이동이 실제로 일어나는 곳
        EndDeferWindowPos(batch);
    }
    if (!bubblesOk) RecreateGpu("bubble");
}

// R1: hit-test Region 캐시. 키 = 프레임·반전·표시 크기 (임계값은 실행 중 고정). SetWindowRgn 은 넘긴 Region 을
// 소유하므로 캐시 원본이 아니라 복사본을 돌려준다
HRGN PetApp::RegionFor(CharacterAsset& asset, const SpritePlacement& sp, const core::AnimationClip* clip,
                       uint64_t& keyOut) {
    auto buildOne = [&](int frame) {
        std::vector<RECT> rects;
        SpritePlacement one = sp;
        one.frame = frame;
        AppendOpaqueRects(asset.sheet, one, config_.alphaThreshold, rects);
        return CreateRegionFromRects(rects);
    };
    auto build = [&] {
        regionBuilds_++;
        if (!clip || clip->frames.size() < 2) return buildOne(sp.frame);
        HRGN u = buildOne(clip->frames.front());
        for (size_t i = 1; i < clip->frames.size(); ++i) {
            HRGN f = buildOne(clip->frames[i]);
            CombineRgn(u, u, f, RGN_OR);
            DeleteObject(f);
        }
        return u;
    };
    // 클립 합집합이면 프레임 대신 클립의 첫 프레임 번호 + 표시(bit 49)로 키를 만든다
    const uint64_t id = clip && clip->frames.size() >= 2 ? (static_cast<uint64_t>(clip->frames.front()) | (1ull << 49))
                                                         : static_cast<uint64_t>(sp.frame);
    const uint64_t key = (id & 0xFFFF) | (id & (1ull << 49)) | (static_cast<uint64_t>(sp.flipX) << 16) |
                         (static_cast<uint64_t>(sp.w & 0xFFFF) << 17) | (static_cast<uint64_t>(sp.h & 0xFFFF) << 33);
    keyOut = key;
    if (options_.noRegionCache) return build();
    auto it = asset.regionCache.find(key);
    if (it == asset.regionCache.end()) {
        if (asset.regionCache.size() >= 512) asset.ClearRegions();  // DPI·배율이 자주 바뀌는 경우의 상한
        it = asset.regionCache.emplace(key, build()).first;
    } else {
        regionCacheHits_++;
    }
    HRGN copy = CreateRectRgn(0, 0, 0, 0);
    CombineRgn(copy, it->second, nullptr, RGN_COPY);
    return copy;
}

// 말풍선: 펫 창 위치(lastRect)를 따라가고 같은 DeferWindowPos 묶음으로 옮긴다. 자동 숨김 마감도 여기서 건다
bool PetApp::UpdateBubbles(double now, HDWP& batch) {
    double next = kNoDeadline;
    bool ok = true;
    const auto& world = physics_.World();
    for (auto& up : pets_) {
        PetActor& p = *up;
        if (!p.bubble) continue;
        if (p.bubble->Active() && p.bubble->hideAt > 0 && now >= p.bubble->hideAt) p.bubble->Hide();
        if (!p.bubble->Active()) continue;
        if (p.bubble->hideAt > 0) next = std::min(next, p.bubble->hideAt);
        if (p.hiddenFullscreen || userHidden_ || !p.visible) continue;
        const int mi = core::MonitorIndexFor(world, p.body.foot);
        RECT work{0, 0, 1920, 1080};
        if (mi >= 0) {
            const auto& w = world.monitors[mi].work;
            work = {w.left, w.top, w.right, w.bottom};
        }
        const HRESULT hr = p.bubble->Update(instance_, gpu_, bubbleRenderer_, p.lastRect, work, p.dpi / 96.0f, batch, now);
        if (GpuContext::IsDeviceLost(hr)) ok = false;
        if (p.bubble->redrawAt >= 0) next = std::min(next, p.bubble->redrawAt);
    }
    if (next < kNoDeadline) deadlines_.Set(kDlBubble, next);
    else deadlines_.Clear(kDlBubble);
    return ok;
}

// ============================================================================
// 상태 전환·복구
// ============================================================================

void PetApp::SetSuspended(bool userHidden, bool locked, bool sleeping, const char* reason) {
    const bool was = Suspended();
    const bool wasHidden = userHidden_;
    userHidden_ = userHidden;
    locked_ = locked;
    sleeping_ = sleeping;
    if (wasHidden != userHidden_)
        for (auto& p : pets_) {
            if (userHidden_) {
                ShowWindow(p->hwnd, SW_HIDE);
                p->visible = false;
                if (p->bubble) p->bubble->SuspendWindow();
            } else {
                p->shownFrame = -1;
            }
        }
    if (was != Suspended()) {
        log::Info("{} ({})", Suspended() ? "일시정지: 물리·행동·렌더·타이머 중지" : "재개", reason);
        if (Suspended()) ObserveInterrupted("suspended");
        else ScheduleObserve(Now());  // 복귀하면 바로 관찰하지 않고 다음 간격부터 센다
        if (!Suspended()) {
            const double now = Now();
            clock_.Reset(now);  // 누적 시간 폐기
            observer_.RequestFullResync();
            observer_.MarkMonitorsDirty();
            for (auto& p : pets_) p->fsm->Force({"idle", 0.5, 0}, now);
        }
    }
}

void PetApp::RecreateGpu(const char* reason) {
    log::Warn("GPU 재생성 ({}) removed_reason={}", reason, log::HrToString(gpu_.RemovedReason()));
    for (auto& p : pets_) {
        p->target.reset();
        if (p->bubble) p->bubble->ReleaseGpu();
        p->modelView.reset();
        p->modelAnim = -2;
    }
    for (auto& [_, a] : library_) a->ReleaseGpu();
    bubbleRenderer_.Release();
    gpu_.Release();
    if (!InitGpu()) {
        log::Error("GPU 재생성 실패 — 종료");
        PostQuitMessage(3);
        return;
    }
    sampler_.SetAdapter(gpu_.Adapter());
    for (auto& p : pets_) {
        const RECT r = RectFor(*p);
        p->target = std::make_unique<CompositionTarget>();
        p->target->Create(gpu_, p->hwnd, ContentMode::SwapChain, r.right - r.left, r.bottom - r.top, false, false);
        p->shownFrame = -1;
    }
    // 다시 그릴 기상을 바로 건다: 정지한 펫은 다른 이벤트가 올 때까지 기상이 없어 빈 창으로 남았다
    // (exp/dcomp-atlas 검증에서 발견, 커서 근접을 끈 설정에서 재현)
    deadlines_.Set(kDlRedraw, Now());
    log::Info("[render] GPU 재생성 완료 ({}) pets={}", reason, pets_.size());
}

void PetApp::SampleMetrics() {
    if (!metrics_) return;
    MetricsSample m = sampler_.Sample();
    const auto& s = observer_.GetStats();
    std::string modes;
    for (auto& p : pets_) modes += std::string(core::ToString(p->body.mode)) + (p->hiddenFullscreen ? "(hidden)" : "") + " ";
    fprintf(metrics_, "%.3f,%.4f,%.3f,%.5f,%.2f,%.2f,%.2f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%s", Now() - startTime_,
            m.procCpuPct, m.procCpuMsDelta, m.procCpuPctCycles, m.workingSetMb, m.privateMb, m.vramLocalMb,
            m.presentDelta, m.wakeupsDelta, s.rawEvents, s.acceptedEvents, s.zRebuilds, s.rectUpdates, s.snapshots,
            modes.c_str());
    // M1: 1초 동안의 단계별 UI 스레드 사이클 (천 단위)
    const auto cyc = cost_.TakeCycles();
    const auto cnt = cost_.TakeCounts();
    for (uint64_t c : cyc) fprintf(metrics_, ",%llu", static_cast<unsigned long long>(c / 1000));
    fprintf(metrics_, ",%llu,%llu,%llu\n", static_cast<unsigned long long>(regionBuilds_),
            static_cast<unsigned long long>(regionCacheHits_),
            static_cast<unsigned long long>(cnt[static_cast<int>(CostPhase::Move)]));
    regionBuilds_ = regionCacheHits_ = 0;
    fflush(metrics_);
}

// ============================================================================
// 입력·메시지
// ============================================================================

PetActor* PetApp::PetFromHwnd(HWND hwnd) {
    for (auto& p : pets_)
        if (p->hwnd == hwnd) return p.get();
    return nullptr;
}

LRESULT CALLBACK PetApp::StaticAppProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<PetApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->AppProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK PetApp::StaticPetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<PetApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->PetProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PetApp::PetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PetActor* p = PetFromHwnd(hwnd);
    auto screenPt = [&] {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd, &pt);
        return pt;
    };
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;  // 포커스 비탈취
        case WM_LBUTTONDOWN:
            if (p) {
                const POINT pt = screenPt();
                SetCapture(hwnd);
                p->pressed = true;
                p->dragging = false;
                p->pressPt = pt;
                p->grabDx = p->body.foot.x - pt.x;
                p->grabDy = p->body.foot.y - pt.y;
            }
            return 0;
        case WM_MOUSEMOVE:
            if (p && p->pressed) {
                const POINT pt = screenPt();
                const int th = config_.dragThresholdPx * static_cast<int>(p->dpi) / 96;
                if (!p->dragging && (std::abs(pt.x - p->pressPt.x) > th || std::abs(pt.y - p->pressPt.y) > th)) {
                    p->dragging = true;
                    core::Physics::BeginDrag(p->body);  // 드래그 시작 시 부착 해제 (기준선 §5.2)
                    p->fsm->OnEvent("drag_start", Now(), false);
                    LogBody(*p, {});
                }
                if (p->dragging) p->body.foot = {pt.x + p->grabDx, pt.y + p->grabDy};
            }
            return 0;
        case WM_LBUTTONUP:
            if (p && p->pressed) {
                p->pressed = false;
                if (p->dragging) {
                    p->dragging = false;
                    core::Physics::EndDrag(p->body);  // 놓으면 중력 복구
                    p->fsm->OnEvent("drag_end", Now(), false);
                    clock_.Reset(Now());
                    LogBody(*p, {});
                } else {
                    // 클릭 반응은 FSM 이 정한다 (기본 look, Lua on_event 로 재정의 가능)
                    p->fsm->OnEvent("click", Now(), p->body.mode == core::BodyMode::Attached);
                    lastPetId_ = p->id;
                    if (aiEnabled_) OpenChat(p);  // AI 를 켠 경우: 클릭 → 대화창 (P3-R4, 기준선 §5.2)
                    log::Info("[pet{}] click → act={}", p->id, p->fsm->Activity());
                    deadlines_.Set(kDlBehavior, Now());
                }
            }
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            if (p && p->dragging) {
                p->pressed = p->dragging = false;
                core::Physics::EndDrag(p->body);
                clock_.Reset(Now());
            }
            return 0;
        case WM_RBUTTONUP: ShowMenu(p); return 0;
        case WM_DPICHANGED: observer_.MarkMonitorsDirty(); return 0;  // 크기는 발 위치 모니터 DPI로 다시 계산
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PetApp::AppProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == TrayIcon::TaskbarCreatedMessage()) {
        log::Info("TaskbarCreated — 트레이 재등록 {}", tray_.Readd() ? "성공" : "실패");
        observer_.MarkMonitorsDirty();  // 작업 표시줄 재시작 → 작업 영역 재계산
        return 0;
    }
    switch (msg) {
        case WM_AI_EVENT:
            // AI 서비스가 보낸 이벤트만 받는다 (다른 프로세스가 같은 번호로 보낸 임의 포인터는 무시)
            if (auto e = ai_.Take(lp)) {
                try {
                    OnAiEvent(std::move(e));
                } catch (const std::exception& ex) {
                    // 응답 처리 중 예외: 앱을 끝내지 않고 진행 중인 대화·관찰을 정리한다
                    log::Error("[ai] 이벤트 처리 예외: {}", ex.what());
                    if (activeReq_) StopChat();
                    ObserveInterrupted("internal_error");
                }
            }
            return 0;
        case WM_HOTKEY:
            if (wp == 1) {
                PetActor* t = PetById(lastPetId_);
                if (!t && !pets_.empty()) t = pets_.front().get();
                log::Info("[ai] hotkey → chat pet={}", t ? t->id : -1);
                OpenChat(t);
            }
            return 0;
        case WM_TIMER:
            if (wp == kUpdateTimerId) OnUpdateTimer();
            else if (wp == kWipeTimerId) {
                log::Warn("[wipe] 로그아웃 응답이 없어 그대로 진행");
                FinishWipe();
            }
            return 0;
        case WM_UPDATE_RESULT:
            OnUpdateResult();
            return 0;
        case WM_TRAY:
            if (LOWORD(lp) == WM_CONTEXTMENU || LOWORD(lp) == NIN_SELECT || LOWORD(lp) == NIN_KEYSELECT) ShowMenu(nullptr);
            return 0;
        case WM_COMMAND: {
            const UINT id = LOWORD(wp);
            // 메뉴 대상: 우클릭한 펫, 없으면 마지막 펫
            PetActor* target = menuTarget_ ? menuTarget_ : (pets_.empty() ? nullptr : pets_.back().get());
            if (id >= ID_ADD_BASE && id < ID_ADD_BASE + libraryOrder_.size()) {
                if (static_cast<int>(pets_.size()) >= core::Config::kMaxPets) {
                    log::Warn("[pets] 최대 {}마리 — 추가하지 않음", core::Config::kMaxPets);
                } else {
                    CreatePet(library_[libraryOrder_[id - ID_ADD_BASE]], static_cast<int>(pets_.size()), core::Config::kMaxPets);
                }
            } else if (id >= ID_SWAP_BASE && id < ID_SWAP_BASE + libraryOrder_.size()) {
                if (target) SwapCharacter(*target, library_[libraryOrder_[id - ID_SWAP_BASE]]);
            } else {
                switch (id) {
                    case ID_REMOVE: if (target) RemovePet(target); break;
                    case ID_RELOAD: ReloadCharacters(); break;
                    case ID_HIDE: SetSuspended(!userHidden_, locked_, sleeping_, "tray"); break;
                    case ID_PAUSE:
                        paused_ = !paused_;
                        log::Info("행동 {}", paused_ ? "일시정지" : "재개");
                        for (auto& p : pets_) p->fsm->Force({"idle", 0.5, 0}, Now());
                        break;
                    case ID_OPEN_CONFIG: ShellExecuteW(nullptr, L"open", configPath_.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
                    case ID_OPEN_LOGS: ShellExecuteW(nullptr, L"open", (configPath_.parent_path() / L"logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
                    case ID_OPEN_CHARACTERS: ShellExecuteW(nullptr, L"open", (configPath_.parent_path() / L"characters").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
                    case ID_RESYNC: observer_.RequestFullResync(); break;
                    case ID_CHAT: OpenChat(target); break;
                    case ID_AI_TOGGLE: EnableAi(!aiEnabled_); break;
                    case ID_SETTINGS: OpenSettings(SettingsWindow::kGeneral); break;
                    case ID_CHARACTERS: OpenCharacters(); break;
                    case ID_WELCOME: ShowWelcome(true); break;
                    case ID_UPDATE: OpenUpdatePage(); break;
                    case 9001: {  // 시나리오 전용: 설정 폴더의 test_install.txt 에 적힌 경로를 설치 (--test-hooks)
                        if (!options_.testHooks) break;
                        std::ifstream in(configPath_.parent_path() / L"test_install.txt", std::ios::binary);
                        std::string line;
                        std::getline(in, line);
                        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                        std::string installed;
                        const std::wstring result = InstallCharacter(std::filesystem::path(ToWide(line)), installed);
                        log::Info("[characters] test install id={} message_len={}", installed.empty() ? "-" : installed, result.size());
                        break;
                    }
                    case ID_AI_DIAG:
                        if (aiEnabled_) {
                            OpenChat(target);
                            chat_.SetStatus(ui::T("status.diag_running"));
                            ai_.DiagnoseRefresh();
                        }
                        break;
                    case ID_EXIT: PostQuitMessage(0); break;
                    case 9000:  // 시나리오 전용: GPU 재생성 강제 (--test-hooks 일 때만)
                        if (options_.testHooks) RecreateGpu("test");
                        break;
                    default: OnObserveCommand(id); break;
                }
            }
            menuTarget_ = nullptr;
            return 0;
        }
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK) SetSuspended(userHidden_, true, sleeping_, "session lock");
            else if (wp == WTS_SESSION_UNLOCK) SetSuspended(userHidden_, false, sleeping_, "session unlock");
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMSUSPEND) SetSuspended(userHidden_, locked_, true, "suspend");
            else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) SetSuspended(userHidden_, locked_, false, "resume");
            return TRUE;
        case WM_DISPLAYCHANGE:
            log::Info("WM_DISPLAYCHANGE — 모니터 재계산");
            observer_.MarkMonitorsDirty();
            return 0;
        case WM_SETTINGCHANGE:
            if (wp == SPI_SETWORKAREA) {
                log::Info("SPI_SETWORKAREA — 작업 영역 재계산");
                observer_.MarkMonitorsDirty();
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void PetApp::ShowMenu(PetActor* target) {
    // UI-03: 하위 메뉴로 묶는다. 명령 ID 는 바꾸지 않는다 (시나리오가 쓴다). 펫 우클릭은 그 펫에 관한 것만
    menuTarget_ = target;
    HMENU menu = CreatePopupMenu();
    auto item = [](HMENU m, UINT flags, UINT_PTR id, const std::wstring& text) { AppendMenuW(m, flags, id, text.c_str()); };
    const UINT petFlags = pets_.empty() ? MF_GRAYED : 0;
    HMENU swap = CreatePopupMenu();
    for (size_t i = 0; i < libraryOrder_.size(); ++i) {
        const auto& pkg = library_[libraryOrder_[i]]->pkg;
        const bool current = target && target->asset->pkg.id == pkg.id;
        item(swap, MF_STRING | (current ? MF_CHECKED : 0), ID_SWAP_BASE + i, ToWide(pkg.name));
    }
    if (target) {  // 펫 우클릭
        if (aiEnabled_) item(menu, MF_STRING, ID_CHAT, ui::T("menu.chat_this"));
        item(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(swap), ui::T("menu.swap"));
        item(menu, MF_STRING, ID_REMOVE, ui::T("menu.remove"));
        item(menu, MF_STRING, ID_HIDE, ui::T("menu.hide"));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        item(menu, MF_STRING, ID_CHARACTERS, ui::T("menu.characters"));
        item(menu, MF_STRING, ID_SETTINGS, ui::T("menu.settings"));
    } else {  // 트레이
        item(menu, MF_STRING | MF_GRAYED, ID_INFO, ui::TF("menu.info", std::wstring(DP_VERSION_WSTRING), pets_.size(), library_.size()));
        if (updateAvailable_) item(menu, MF_STRING, ID_UPDATE, ui::TF("update.menu", ToWide(updateAvailable_->version)));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (aiEnabled_) item(menu, MF_STRING | petFlags, ID_CHAT, ui::T("menu.chat"));
        HMENU pets = CreatePopupMenu(), add = CreatePopupMenu();
        for (size_t i = 0; i < libraryOrder_.size(); ++i)
            item(add, MF_STRING, ID_ADD_BASE + i, ToWide(library_[libraryOrder_[i]]->pkg.name));
        const UINT addFlags = libraryOrder_.empty() || static_cast<int>(pets_.size()) >= core::Config::kMaxPets ? MF_GRAYED : 0;
        item(pets, MF_POPUP | addFlags, reinterpret_cast<UINT_PTR>(add), ui::T("menu.add"));
        item(pets, MF_POPUP | petFlags, reinterpret_cast<UINT_PTR>(swap), ui::T("menu.swap_last"));
        item(pets, MF_STRING | petFlags, ID_REMOVE, ui::T("menu.remove_last"));
        item(pets, MF_STRING, ID_RELOAD, ui::T("menu.reload"));
        AppendMenuW(pets, MF_SEPARATOR, 0, nullptr);
        item(pets, MF_STRING, ID_HIDE, userHidden_ ? ui::T("menu.show") : ui::T("menu.hide"));
        item(pets, MF_STRING | (paused_ ? MF_CHECKED : 0), ID_PAUSE, ui::T("menu.pause"));
        item(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pets), ui::T("menu.pets"));
        HMENU ai = CreatePopupMenu();
        item(ai, MF_STRING | (aiEnabled_ ? MF_CHECKED : 0), ID_AI_TOGGLE, ui::T("menu.ai_enable"));
        item(ai, MF_STRING | (aiEnabled_ && account_.state == AccountState::SignedIn ? 0 : MF_GRAYED), ID_AI_DIAG,
             ui::T("menu.ai_diag"));
        item(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(ai), ui::T("menu.ai"));
        AppendObserveMenu(menu);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        item(menu, MF_STRING, ID_CHARACTERS, ui::T("menu.characters"));
        item(menu, MF_STRING, ID_SETTINGS, ui::T("menu.settings"));
        item(menu, MF_STRING, ID_WELCOME, ui::T("menu.help"));
        HMENU folders = CreatePopupMenu();
        item(folders, MF_STRING, ID_OPEN_CHARACTERS, ui::T("menu.open_characters"));
        item(folders, MF_STRING, ID_OPEN_LOGS, ui::T("menu.open_logs"));
        item(folders, MF_STRING, ID_OPEN_CONFIG, ui::T("menu.open_config"));
        AppendMenuW(folders, MF_SEPARATOR, 0, nullptr);
        item(folders, MF_STRING, ID_RESYNC, ui::T("menu.resync"));
        item(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(folders), ui::T("menu.folders"));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        item(menu, MF_STRING, ID_EXIT, ui::T("menu.exit"));
    }
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(appWnd_.get());  // 바깥 클릭으로 메뉴가 닫히도록
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, appWnd_.get(), nullptr);
    PostMessageW(appWnd_.get(), WM_NULL, 0, 0);
    DestroyMenu(menu);  // 하위 메뉴도 함께 파괴됨
}

}  // namespace dp
