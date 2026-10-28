#pragma once
#include <windows.h>

#include <wil/resource.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <format>
#include <map>
#include <unordered_map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "ai/observe.h"
#include "ai/responses.h"
#include "bubble.h"
#include "chat_window.h"
#include "consent_window.h"
#include "settings_window.h"
#include "characters_window.h"
#include "welcome_window.h"
#include "update_checker.h"
#include "cost_meter.h"
#include "core/behavior/fsm.h"
#include "ai/pet_chat.h"
#include "core/behavior/social.h"
#include "core/character/animation.h"
#include "core/character/package.h"
#include "core/config.h"
#include "core/physics.h"
#include "core/scheduler.h"
#include "diagnostics/metrics.h"
#include "platform/windows/ai/ai_service.h"
#include "platform/windows/fullscreen_detector.h"
#include "platform/windows/sprite_sheet.h"
#include "platform/windows/tts/tts.h"
#include "platform/windows/tray_icon.h"
#include "platform/windows/window_observer.h"
#include "rendering/d3d11/bubble_renderer.h"
#include "rendering/d3d11/composition_target.h"
#include "rendering/d3d11/gpu_context.h"
#include "rendering/d3d11/model_renderer.h"
#include "rendering/d3d11/sprite_renderer.h"
#include "rendering/hit_mask.h"
#include "script/lua_behavior.h"

namespace dp {

inline constexpr UINT WM_AI_EVENT = WM_APP + 2;  // lParam = AiEvent* (받는 쪽이 delete)
inline constexpr UINT_PTR kUpdateTimerId = 7;  // 업데이트 확인 타이머 (앱 창)
inline constexpr UINT_PTR kWipeTimerId = 8;    // 모든 데이터 지우기: 로그아웃 응답을 기다리는 최대 시간
inline constexpr UINT WM_UPDATE_RESULT = WM_APP + 3;  // 업데이트 확인 끝 (결과는 UpdateChecker::Take)

// 화면 관찰 (P4): 트레이 메뉴 ID (pet_app.cpp 의 MenuId 다음 값) 와 마감 키
enum ObserveMenuId : UINT {
    ID_OBSERVE_TOGGLE = 113, ID_OBSERVE_PAUSE_1H, ID_OBSERVE_PAUSE_TODAY, ID_OBSERVE_PAUSE_ON, ID_OBSERVE_RESUME,
    ID_OBSERVE_STATUS, ID_TTS_TOGGLE, ID_OBSERVE_SETTINGS,
    ID_CHARACTERS, ID_SETTINGS, ID_WELCOME,  // UI 다듬기 (121~123)
    ID_UPDATE,                                // 새 버전 내려받기 페이지 (124, P6-03)
};
inline constexpr core::Deadlines::Key kDlObserveKey = 100;  // 다음 관찰 시도 또는 캡처 제외 안전망
// 메인 루프 마감 키 (pet_app*.cpp 공용)
enum DeadlineKey : core::Deadlines::Key { kDlCursor = 1, kDlBehavior, kDlResync, kDlEnd, kDlMetrics, kDlOrder, kDlBubble,
                                           kDlRedraw };
inline constexpr double kNoDeadline = 1e300;
// 창 ID 로그 표시 (실행 파일 이름 대신)
inline std::string Hex(core::WindowId id) { return std::format("0x{:X}", id); }

struct LaunchOptions {
    int pets = 0;                 // >0 이면 설정의 [[pets]] 대신 이 마릿수 (개발·테스트용)
    std::string character;        // pets 와 함께: 사용할 캐릭터 id (비면 설정의 첫 캐릭터)
    std::wstring charactersDir;   // 추가 캐릭터 검색 경로 (테스트용, 가장 높은 우선순위)
    std::wstring configPath;      // 설정 파일 경로 (테스트용). 사용자 캐릭터 폴더도 이 파일 옆 characters
    bool spawnSet = false;
    double spawnX = 0, spawnY = 0;  // 시작 발 위치 (시나리오 테스트용). 펫 i 는 x + i*spawnStepX
    double spawnStepX = -150;
    bool noBehavior = false;      // 자동 행동 끔 (시나리오 테스트에서 위치 고정)
    bool noRegionCache = false;   // R1 비교 측정용: Region 캐시 끄기
    bool forceWalk = false;       // 계속 걷기 (성능 측정)
    std::string forceActivity;    // 시험 전용: 쉴 때마다 이 행동 (jump·climb 확인)
    std::wstring metricsCsv;      // 지정 시 1초 간격 성능 CSV
    int durationSec = 0;          // >0 이면 자동 종료 (측정용)
    // AI (P3, 테스트 전용 인자): 설정과 무관하게 켬 / 모의 서버 주소(127.0.0.1 만) / 브라우저 대신 인가 URL 기록
    bool aiForce = false;
    std::string aiEndpoint;
    std::wstring aiOpenUrlFile;
    // 화면 관찰 (P4, 테스트 전용: --ai-endpoint 와 함께만): 시도 간격을 초 단위로 / 축소한 캡처를 이 파일에도 저장
    int observeIntervalSec = 0;
    std::wstring observeDump;
    bool observeNoExclude = false;  // 대조 시험(B14): 캡처 때 펫을 빼지 않는다
    bool testHooks = false;
    std::string updateUrl;        // 시험: latest.json 주소 재정의 (127.0.0.1 만)
    std::string updateRepo;       // 시험: 저장소 재정의
    int updateDelaySec = 60;      // 시작 뒤 첫 확인까지 (시험은 짧게)
    std::wstring frameLog;          // 진단: 펫별 Present·창 이동 시각 CSV (끊김 분석)         // 시나리오 전용 명령 (WM_COMMAND 9000 = GPU 재생성 강제)
    std::wstring renderDump;        // 3D 확인용 (P5): 펫이 그린 픽셀(화면이 아님)을 PNG 로. 파일 이름에 _N 이 붙는다
    int renderDumpStride = 10;      // readback 몇 번마다 저장할지 (--render-dump-stride=N)
};

// 3D 캐릭터 (P5): 모델 데이터·GPU 자원은 캐릭터마다 1개, 펫은 포즈·MSAA 대상만 따로 (기준선 §6.3)
struct ModelAsset {
    model::Model data;
    ModelRenderer renderer;
    float aspect = 1;            // 창 너비 / 높이 (오른쪽·왼쪽 공통 → 방향을 바꿔도 창 크기가 그대로)
    ModelCamera right, left;     // 오른쪽·왼쪽을 볼 때의 직교 카메라
    std::map<std::string, int> actions;  // 공통 행동 → 애니메이션 인덱스
    // 행동에 맞는 애니메이션. 없으면 idle, 그것도 없으면 -1 (기본 자세) (기준선 §5.1 대체 규칙)
    int AnimationFor(const std::string& action) const {
        auto it = actions.find(action);
        if (it == actions.end()) it = actions.find("idle");
        return it == actions.end() ? -1 : it->second;
    }
};

// 캐릭터 관리 창 썸네일 (UI-08): premultiplied BGRA, 위에서 아래, pitch = width * 4
struct CharacterThumb {
    std::vector<uint8_t> bgra;
    int width = 0, height = 0;
};

// 캐릭터 패키지 1개 = 읽기 전용 에셋. 같은 캐릭터 펫들이 공유한다 (기준선 §6.3, ADR-04)
struct CharacterAsset {
    core::CharacterPackage pkg;
    std::unique_ptr<ModelAsset> model;  // 3D 패키지면 (sheet·renderer 는 비어 있음)
    SpriteSheet sheet;        // CPU 사본 (hit-test Region)
    SpriteRenderer renderer;  // GPU 텍스처
    // R1: (프레임, 반전, 크기)별 hit-test Region 캐시. 같은 캐릭터 펫끼리 공유하고 SetWindowRgn 에는 복사본을 넘긴다
    std::unordered_map<uint64_t, HRGN> regionCache;
    CharacterAsset() = default;
    CharacterAsset(const CharacterAsset&) = delete;
    CharacterAsset& operator=(const CharacterAsset&) = delete;
    ~CharacterAsset() { ClearRegions(); }
    bool InitGpu(ID3D11Device* device) {
        return model ? model->renderer.Init(device, model->data) : renderer.Init(device, sheet);
    }
    void ReleaseGpu() {
        renderer.Release();
        if (model) model->renderer.Release();
    }
    void ClearRegions() {
        for (auto& [_, r] : regionCache) DeleteObject(r);
        regionCache.clear();
    }
};

// 따라가기 종류: 잡기 놀이(닿으면 잡음) · 기차놀이(한 칸 띄워 따라감) · 붙어 앉으러 감 · 코딩하는 친구 옆으로 (페어 프로그래밍)
enum class FollowMode { Tag, Train, Lean, Visit };

struct PetActor {
    int id = 0;
    std::shared_ptr<CharacterAsset> asset;
    core::PetBody body;
    std::unique_ptr<script::LuaBehavior> lua;     // P2-R3: 인스턴스마다 1개
    std::unique_ptr<core::BehaviorMachine> fsm;
    core::AnimationPlayer anim;
    bool cursorNear = false;    // 커서 근접 (near 는 windows.h 매크로)
    std::string loggedActivity;
    double appliedDecisionAt = -1;  // 걷기 방향은 새 결정 때만 적용 (가장자리 반전은 물리가 한다)
    std::mt19937 rng{0x5eed};       // 점프 목표 고르기 (창 위로 / 작게), 인사 문구
    int followTarget = -1;          // 따라가는 펫 id (-1 = 없음)
    FollowMode followMode = FollowMode::Tag;
    // 페어 프로그래밍: 들여다보는 펫(visitor) 쪽에 단계·시각을 둔다. 코딩하는 펫은 pairWith 만
    int pairWith = -1, pairStage = 0;
    bool pairCoder = false, pairAi = false;
    double pairAt = 0;
    int raceWith = -1;              // 벽 타기 경주 상대 (-1 = 없음)
    // 올라타기 (탑 쌓기): mountId = 내가 올라탄 펫, riderId = 내 머리 위 펫, stackTarget = 뛰어오르는 중인 목표
    int mountId = -1, riderId = -1, stackTarget = -1;
    // 버그 잡기: 버그도 펫처럼 움직이고 그려지지만 메뉴·마릿수·관찰·AI·다시 띄우기에서는 빠진다 (id 1000~)
    bool isBug = false, bugDead = false;
    double bugUntil = 0;
    double rideUntil = 0;
    double raceUntil = 0;
    double followUntil = 0, followJumpAt = 0;
    bool tagBack = false;           // 잡힌 뒤 되잡으러 가는 중 (한 번만)
    uint64_t loggedLuaErrors = 0;
    bool loggedLuaDisabled = false;
    uint32_t dpi = 96;

    // 표시
    HWND hwnd = nullptr;
    std::unique_ptr<CompositionTarget> target;
    int shownFrame = -1;
    bool shownFlip = false;
    int shownSize = 0;
    const CharacterAsset* shownAsset = nullptr;
    RECT lastRect{};
    long long lastMoveTick = -1;
    double lastPresentAt = -1;     // 프레임 레이트 상한 (display.frame_rate)
    double presentPendingAt = -1;  // 상한 때문에 미룬 Present 시각
    uint64_t regionKey = ~0ull;    // 마지막으로 설정한 Region (같으면 SetWindowRgn 생략)  // 걷기·낙하 중 창 이동을 공통 30Hz 틱으로 제한 (물리는 60Hz, 여러 펫을 한 번에 묶음)
    bool visible = false;
    bool hiddenFullscreen = false;
    bool facingLeft = false;

    // 3D (P5)
    std::unique_ptr<ModelView> modelView;
    int modelAnim = -2;           // 재생 중인 애니메이션 (-1 기본 자세, -2 아직 그리지 않음)
    double modelAnimStart = 0;
    double regionDueAt = 0;       // 다음 hit-test readback 시각 (P5-R2: 최대 10Hz)
    size_t regionHash = 0;        // 마지막으로 설정한 Region (같으면 SetWindowRgn 생략)
    bool regionMoving = false;    // 직전 그리기 때 낙하·드래그 중이었나 (착지 직후 Region 을 바로 맞춘다)

    // AI 대화 (P3): 펫마다 대화 이력(메모리)과 말풍선
    ai::Conversation conv;
    std::unique_ptr<SpeechBubble> bubble;
    bool speaking = false;  // 응답 스트리밍 중 → talk 행동, 걷기 멈춤

    // 입력
    bool pressed = false, dragging = false;
    POINT pressPt{};
    double grabDx = 0, grabDy = 0;

    // 로그용 직전 상태
    core::BodyMode loggedMode = core::BodyMode::Dragged;
    core::WindowId loggedHost = ~0ull;
};

class PetApp {
public:
    int Run(HINSTANCE instance, const core::Config& config, const std::filesystem::path& configPath,
            const LaunchOptions& options);

private:
    // 초기화
    bool CreateAppWindow();
    bool InitGpu();
    void LoadCharacters();  // LoadLibrary 는 Win32 매크로라 피함
    std::vector<core::PetSpawn> SpawnList() const;
    void SpawnPets(const std::vector<core::PetSpawn>& list);
    PetActor* CreatePet(const std::shared_ptr<CharacterAsset>& asset, int slot, int slots, int forcedId = -1);
    void SetupBehavior(PetActor& pet);
    void RemovePet(PetActor* pet);
    void SwapCharacter(PetActor& pet, const std::shared_ptr<CharacterAsset>& asset);
    void ReloadCharacters();
    void DestroyAll();

    // 루프
    double Now() const;
    void ArmWait(double animDeadline, double otherDeadline, double now, DWORD& timeoutMs, HANDLE* handles,
                 DWORD& count);
    bool Suspended() const { return userHidden_ || locked_ || sleeping_; }
    void OnWorldChanged(double now);
    void StepPhysics(double now);
    void UpdateBehavior(double now);
    void PollCursor(double now);
    void ApplyFullscreen(double now);
    void RenderAndMove(double now);
    void SampleMetrics();

    // 펫
    core::PhysicsParams ParamsFor(const PetActor& pet) const;
    std::string ActionFor(const PetActor& pet) const;
    void StartJumpOrClimb(PetActor& pet, double now);
    static std::wstring LimitText(int limit);
    void StartFollow(PetActor& pet, double now);
    void StartFollowing(PetActor& pet, PetActor& target, FollowMode mode, double seconds, double now);
    void StartTrain(PetActor& leader, double now);
    void Caught(PetActor& me, PetActor& target, double now);
    void LeanTogether(PetActor& me, PetActor& target, double now);
    void BumpPair(PetActor& a, PetActor& b, double now);
    void StartVisit(PetActor& visitor, double now);
    void StartPairSession(PetActor& visitor, PetActor& coder, double now);
    void UpdatePairs(double now);
    void EndPair(PetActor& visitor);
    void StartRace(PetActor& me, double now);
    void OnClimbTop(PetActor& pet);  // 화면 끝 꼭대기에서 뛰어내림 (경주 판정)
    void StartStack(PetActor& me, double now);
    void MountOn(PetActor& rider, PetActor& mount, double now);
    void HopOff(PetActor& rider, double now);
    void Dismount(PetActor& rider);  // 아래 펫이 사라지거나 움직여 떨어짐
    // 물리 한 스텝 뒤: 뛰어오르던 펫이 머리 높이를 지났으면 올라태우고, 올라탄 펫 위치를 아래 펫에 맞춘다 (아래부터)
    void UpdateStacks(const std::vector<double>& prevFootY, double now);
    // 버그 잡기
    double NextBugCheck(double now);   // 다음 버그 등장 시각 (사냥할 펫이 없으면 kNoDeadline)
    bool SpawnBug(double now);
    void UpdateBug(PetActor& bug, double now);
    void CatchBug(PetActor& hunter, PetActor& bug, double now);
    void SweepBugs();                  // 잡혔거나 도망친 버그를 지운다 (루프 밖에서)
    size_t RealPetCount() const;       // 버그를 뺀 펫 수
    PetActor* LastRealPet();
    double nextBugAt_ = -1;
    std::mt19937 bugRng_{0xb06};
    int nextBugId_ = 1000;
    void SyncRiders();
    void UpdateSocial(double now);  // 인사·따라가기 (UpdateBehavior 끝에서)
    void GreetPair(PetActor& a, PetActor& b, double now);
    void SayFixed(PetActor& pet, const char* keyPrefix, int count, double now, double seconds);
    core::GreetBook greetBook_, bumpBook_, leanBook_;
    // 펫끼리 AI 대화 (설정 ai.pet_chat). 사용자 대화(activeReq_)·관찰과 따로 추적하고, 대화 이력(conv)에 넣지 않는다
    struct PetChat {
        uint64_t req = 0;
        int a = -1, b = -1;  // a 가 먼저 말하고 b 가 답한다
        int stage = 0;       // 1 = a 의 말 받는 중, 2 = b 의 답 받는 중
        std::string reply;
    } petChat_;
    double lastPetChatAt_ = -1e9;
    // 모델 목록이 없으면 한 번만 요청 (AI 켬·로그인·플랜 사용 가능일 때). 요청했으면 true
    bool RequestModelsIfNeeded();
    // 설정의 기본 모델(없으면 첫 모델)을 고르고 대화창 목록에 반영. 고른 index (없으면 -1)
    int SelectDefaultModel();
    // speaker 의 펫끼리 한마디 요청 (otherLine 이 비면 먼저 말 걸기). 말풍선 "…" 를 띄우고 요청 id 를 돌려준다
    uint64_t SendPetChatLine(PetActor& speaker, const PetActor& other, const std::string& otherLine, ai::PetChatSituation situation);
    const char* PetChatBlock(double now);  // 지금 펫끼리 AI 대화를 못 하는 이유 (nullptr = 할 수 있음)
    bool TryPetChat(PetActor& a, PetActor& b, double now, ai::PetChatSituation situation);  // 할 수 있으면 시작, 아니면 이유를 로그
    void StartPetChat(PetActor& a, PetActor& b, double now, ai::PetChatSituation situation);
    void OnPetChatDone(const AiEvent& e, double now);
    void EndPetChat(double now);
    RECT RectFor(const PetActor& pet) const;
    void UpdatePetSize(PetActor& pet);
    void LogBody(PetActor& pet, const std::vector<core::BodyEvent>& events);
    void LogLua(PetActor& pet);
    PetActor* PetFromHwnd(HWND hwnd);
    void SetSuspended(bool userHidden, bool locked, bool sleeping, const char* reason);
    void RecreateGpu(const char* reason);

    // 메시지
    static LRESULT CALLBACK StaticAppProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK StaticPetProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT AppProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT PetProc(HWND, UINT, WPARAM, LPARAM);
    void ShowMenu(PetActor* target);

    // AI 대화 (P3, pet_app_ai.cpp)
    void EnableAi(bool on);
    void RegisterAiHotkey();
    void OnAiEvent(std::unique_ptr<AiEvent> e);
    void OpenChat(PetActor* pet);
    void SendChat(const std::wstring& text);
    void StopChat();
    void RenderChatHistory();
    void UpdateChatAccount();
    PetActor* PetById(int id);
    bool UpdateBubbles(double now, HDWP& batch);  // false = Device Lost

    // 화면 관찰·자율 발화·TTS (P4, pet_app_observe.cpp)
    void InitObserve(double now);
    bool OnObserveCommand(UINT id);
    void AppendObserveMenu(HMENU menu);
    void ObserveTick(double now);       // 마감 도달 시 메인 루프에서
    void OnObserveEvent(std::unique_ptr<AiEvent> e);
    void ToggleObserve();
    void OnObserveConsent(bool agreed);  // 동의 창의 답
    // 동의 범위 (보안 점검): 동의할 때 보여 준 값. 지금 설정이 더 넓으면 관찰을 끈다 (시작할 때·설정 적용 때)
    ai::ObserveScope ScopeOfConfig() const;
    bool TurnOffIfScopeWidened(const char* when);
    void PauseObserve(double untilSec, const char* reason);  // untilSec < 0 = 다시 켤 때까지
    void ShowObserveStatus();
    bool SetCaptureExcluded(bool on);  // 켜기: 하나라도 실패하면 모두 되돌리고 false
    void ObserveInterrupted(const char* reason);  // 숨김·잠금·전체화면: 진행 중 관찰 취소, TTS 중지
    void ScheduleObserve(double now);              // 다음 시도 시각을 새로 뽑고 마감을 건다
    void ScheduleObserveDeadlineOnly(double now);  // 시도 시각은 그대로, 마감만 다시 건다
    void SaveObserveState();
    void SpeakTts(const PetActor& pet, const std::string& utf8);
    void EnableTts(bool on);
    std::vector<std::string> ObserveBehaviors(const PetActor& pet) const;
    void UpdateObserveIndicators();  // 트레이 툴팁·대화창 표시
    // 설정 창 (UI-07): 저장 = 디스크 설정 + 창이 다루는 항목, 반영 = ApplyConfig (즉시 / 다시 시작 필요 목록)
    void OpenSettings(SettingsWindow::Page page);
    SettingsResult ApplySettings(const core::Config& draft, bool startup);
    std::vector<std::wstring> ApplyConfig(const core::Config& old);
    std::vector<std::wstring> notices_;  // ApplyConfig 가 남긴 안내 (설정 창 상태 줄)
    void ReRegisterAiHotkey();
    // 설정 파일 저장: 디스크 설정을 다시 읽어 mutate 로 고친 뒤 .bak 보관 + 임시 파일 교체. 실패하면 false·error
    bool SaveConfigFile(const std::function<void(core::Config&)>& mutate, DWORD& error);
    // 캐릭터 관리 (UI-08)
    std::shared_ptr<CharacterAsset> LoadAsset(const std::filesystem::path& dir, std::vector<std::string>& errors, bool gpu);
    std::filesystem::path UserCharactersDir() const { return configPath_.parent_path() / L"characters"; }
    void OpenCharacters();
    // 환영 창 (UI-09): 첫 실행이면 포커스를 빼앗지 않고 연다 (설정 폴더 ui_state.json, 시나리오는 DP_TEST_NO_WELCOME)
    void MaybeShowWelcome();
    // 업데이트 확인 (P6-03): 하루 1번 (ui_state.json), 새 버전이면 트레이 알림 + 메뉴. 앱은 내려받지 않는다
    void ScheduleUpdateCheck(int delaySec);
    void OnUpdateTimer();
    void OnUpdateResult();
    void OpenUpdatePage();
    std::string UpdateRepo() const;
    UpdateChecker updater_;
    std::optional<core::ReleaseInfo> updateAvailable_;
    // 모든 데이터 지우기 (P6-04): 확인 → (로그인돼 있으면) 로그아웃·토큰 폐기를 기다림(최대 10초) → 시작 프로그램 등록 해제 →
    // 데이터 폴더에서 앱이 만든 것만 지우고 종료. --config 로 다른 폴더를 쓰면 그 폴더의 앱 파일만 지운다 (폴더 통째로 아님)
    void WipeAllData();
    void FinishWipe();
    bool wipePending_ = false;
    void ShowWelcome(bool activate);
    std::vector<CharacterEntry> CharacterEntries();
    const CharacterThumb& ThumbnailFor(const CharacterAsset& asset);
    bool RenderModelThumbnail(CharacterAsset& asset, int height, CharacterThumb& out);
    std::wstring ApplyPetCounts(const std::vector<core::PetSpawn>& list);
    std::wstring InstallCharacter(const std::filesystem::path& source, std::string& installedId);
    std::wstring DeleteCharacter(const CharacterEntry& entry);

    HINSTANCE instance_ = nullptr;
    core::Config config_;
    std::filesystem::path configPath_;
    LaunchOptions options_;
    wil::unique_hwnd appWnd_;
    TrayIcon tray_;

    GpuContext gpu_;
    std::map<std::string, std::shared_ptr<CharacterAsset>> library_;  // id → 공유 에셋
    std::vector<std::string> libraryOrder_;                           // 메뉴 표시 순서

    WindowObserver observer_;
    FullscreenDetector fullscreen_;
    core::Physics physics_;
    core::FixedStepClock clock_;
    core::Deadlines deadlines_;
    std::vector<std::unique_ptr<PetActor>> pets_;
    int nextPetId_ = 0;
    PetActor* menuTarget_ = nullptr;  // 우클릭한 펫 (트레이 메뉴면 null)

    LARGE_INTEGER qpcFreq_{}, qpcStart_{};
    wil::unique_handle hiResTimer_;
    bool quit_ = false;
    bool userHidden_ = false, locked_ = false, sleeping_ = false, paused_ = false;

    // AI
    bool aiEnabled_ = false;
    AiService ai_;
    ChatWindow chat_;
    ConsentWindow consent_;  // 화면 관찰 동의 (UI-06)
    SettingsWindow settings_;  // 설정 (UI-07)
    CharactersWindow characters_;  // 캐릭터 관리 (UI-08)
    WelcomeWindow welcome_;  // 환영 (UI-09)
    struct RejectedPackage {
        std::filesystem::path dir;
        std::vector<std::string> errors;
    };
    std::vector<RejectedPackage> rejected_;  // 마지막 LoadCharacters 에서 거부된 패키지 (캐릭터 관리 창에 표시)
    std::map<std::string, CharacterThumb> thumbs_;  // 캐릭터 id → 썸네일 (LoadCharacters 때 비움)
    BubbleRenderer bubbleRenderer_;
    AccountInfo account_;
    bool signingIn_ = false, modelsRequested_ = false, hotkey_ = false;
    std::vector<ai::ModelInfo> models_;
    std::string modelSlug_;
    int chatPetId_ = -1, lastPetId_ = -1, activePetId_ = -1;
    uint64_t activeReq_ = 0;

    // 화면 관찰 (P4). 켜짐·동의는 설정 폴더의 observe_state.json (설정 파일만으로는 켜지지 않는다)
    bool observeOn_ = false;
    int observeConsent_ = 0;              // 동의한 버전 (현재 kObserveConsentVersion 과 같아야 동작)
    std::string observeConsentAt_;
    double observePausedUntil_ = 0;       // Now() 기준. <0 = 다시 켤 때까지
    ai::ObserveScope consentScope_;    // 동의한 범위 (observe_state.json "scope")
    ai::ObserveScope pendingScope_;    // 동의 창을 열 때의 값 (그 값에 동의한다)
    // 캡처 제외가 실제로 켜져 있는지 (UI: 켠 뒤 true, 끄기 전 false) — 작업 스레드가 캡처 앞뒤로 확인
    std::shared_ptr<std::atomic<bool>> excludeActive_ = std::make_shared<std::atomic<bool>>(false);
    ai::ObserveSchedule observeSchedule_;
    ai::ObserveCounter observeCounter_;
    uint64_t observeReq_ = 0;
    int observePetId_ = -1, lastObservePet_ = -1;
    bool captureExcluded_ = false;
    double captureExcludeUntil_ = 0;      // 안전망: 이 시각이 지나면 캡처 제외를 되돌린다
    int usageLimitedDay_ = -1;            // 사용량 한도 오류를 받은 날 (그날은 관찰 중지)
    std::vector<std::string> structuredRejected_, imageRejected_;  // 모델별 기능 거부 기록
    std::string lastObserveSkip_;
    uint64_t observeRand_ = 0x9E3779B97F4A7C15ull;
    TtsService tts_;
    bool ttsOn_ = false;

    CostMeter cost_;  // M1 단계별 비용 (측정 CSV 를 켰을 때만)
    uint64_t regionBuilds_ = 0, regionCacheHits_ = 0;
    double frameIntervalSec_ = 1.0 / 30.0;
    double lastCursorPoll_ = -1;
    // clip 이 있으면(hit_region=clip) 그 클립 프레임들의 합집합 Region. key 는 SetWindowRgn 생략 판단용
    HRGN RegionFor(CharacterAsset& asset, const SpritePlacement& sp, const core::AnimationClip* clip, uint64_t& key);
    // 3D (P5)
    std::unique_ptr<ModelAsset> BuildModelAsset(const core::CharacterPackage& pkg, std::vector<std::string>& errors);
    HRESULT DrawModel(PetActor& p, const RECT& r, double now, bool sizeChanged);
    void ApplyModelRegion(PetActor& p, bool wait);
    std::vector<model::Mat4> poseWorld_, posePalette_;  // 펫마다 다시 쓰는 작업 버퍼
    int renderDumps_ = 0, renderReadbacks_ = 0;
    FILE* frameLog_ = nullptr;
    void FrameLog(const char* what, const PetActor& p, const RECT& r, double now);

    MetricsSampler sampler_;
    FILE* metrics_ = nullptr;
    double startTime_ = 0;
};

}  // namespace dp
