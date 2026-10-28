#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dp::core {

// 실행할 펫 (P2: 설정만 바꿔 캐릭터 교체·다중 실행)
struct PetSpawn {
    std::string character;  // 캐릭터 패키지 id
    int count = 1;
    friend bool operator==(const PetSpawn&, const PetSpawn&) = default;
};

// AI 대화 (P3). 기본 꺼짐 — 끄면 P2 와 동작이 같다 (P3-R4)
struct AiConfig {
    bool enabled = false;
    std::string hotkey = "Ctrl+Alt+P";  // 기준선 §1.3 후보, P3-R14
    std::string model;                  // 비면 모델 목록의 첫 항목
    int historyTurns = 20;              // 캐릭터(펫)별 RAM 이력. 디스크 저장 없음 (2026-10-14 결정: 20턴)
    int maxReplyChars = 1200;           // 클라이언트 응답 상한 (P3-R7, max_output_tokens 미지원)
    int bubbleSeconds = 8;              // 응답 완료 후 말풍선 유지 (P3-R8)
    int firstTokenTimeoutSec = 120;     // 첫 응답 글자까지 대기 (추론이 긴 모델 고려)
    int requestTimeoutSec = 300;        // 요청 전체 (첫 응답 대기 포함). DevKit 기본값은 180초
    int callbackPort = 1455;            // OAuth 루프백 포트. 다시 로그인 때 사용 중이면 빈 포트로 (문서: 포트만 달라도 됨)
    std::string reasoningEffort;        // 대화·관찰 요청의 reasoning.effort ("" = 보내지 않음 = 모델 기본값)
    bool petChat = false;               // 펫끼리 만나면 AI 로 한마디씩 주고받기 (요청이 나가므로 기본 끔, 2026-10-20)
    std::string petChatEffort = "low";  // 펫끼리 대화의 effort (짧은 한마디라 낮게)
    int petChatIntervalMin = 10;        // 펫끼리 AI 대화 사이 최소 간격 (분)
    friend bool operator==(const AiConfig&, const AiConfig&) = default;
};

// 화면 관찰·자율 발화 (P4). 켜고 끄는 상태는 동의 기록(observe_state.json)에 있다 — 설정 파일만으로는 켜지지 않는다.
// 값은 P4 계획 §2.7 (2026-10-19 확정: 기본값 + 사용자가 설정 파일에서 변경, 제외 목록에 메신저 추가)
struct ObserveConfig {
    int intervalMinMin = 15;         // 다음 시도까지 무작위 간격 (분)
    int intervalMaxMin = 30;
    int maxPerHour = 2;              // 요청 전송 기준. 0 = 무제한 (2026-10-22)
    int maxPerDay = 16;              // 0 = 무제한
    int requireActivityMin = 5;      // 최근 입력이 이보다 오래되면(자리 비움) 관찰 안 함. 0 = 검사 안 함
    bool sendAppName = true;         // 전경 창 실행 파일 이름을 텍스트로 함께 보냄 (P4-R5)
    int imageMaxPx = 1024;           // 긴 변 (P4-R6)
    int jpegQuality = 80;
    int maxSpeechChars = 120;        // P4-R8
    std::vector<std::string> excludeExe = {"keepass.exe", "keepassxc.exe", "1password.exe", "bitwarden.exe",
                                           "kakaotalk.exe", "discord.exe", "slack.exe", "telegram.exe"};
    // 모델이 요청할 수 있는 행동. 행동 기계가 받는 정지 행동(idle·sit·look)과 walk 만 유효, 캐릭터에 클립이 있어야 함
    std::vector<std::string> allowedBehaviors = {"sit", "look"};
    friend bool operator==(const ObserveConfig&, const ObserveConfig&) = default;
};

// 로컬 TTS (P4). 기본 꺼짐. 로컬 AI TTS 는 엔진을 동봉하지 않고 사용자가 설치한 것에 연결한다 (2026-10-19 결정)
// 실패하면 그 발화는 읽지 않는다 (Windows 음성으로 대체하지 않음, 2026-10-19 결정)
struct TtsConfig {
    bool enabled = false;
    // "winrt" = Windows 음성, "http" = OpenAI 호환 로컬 서버(/v1/audio/speech), "command" = WAV 를 쓰는 실행 파일
    std::string engine = "winrt";
    std::string voice;               // Windows 음성 표시 이름 일부 (비면 시스템 기본)
    double rate = 1.0;               // 0.5~2.0
    double volume = 0.8;             // 0~1
    bool readChatReplies = false;    // P4-R10: 대화 응답도 읽기
    // http: 127.0.0.1·localhost·[::1] 만 허용 (PC 밖으로 보내지 않음)
    std::string httpUrl = "http://127.0.0.1:7788/v1/audio/speech";
    std::string httpModel;           // 비면 보내지 않음
    std::string httpVoice;           // 비면 보내지 않음. 캐릭터 [voice] ai_voice 가 우선
    std::string httpExtraJson;       // 요청 본문에 합칠 JSON 객체 (예: {"lang":"ko"})
    int httpTimeoutSec = 60;         // 첫 요청은 모델 로드로 느릴 수 있다
    // command: {text} {text_file} {out} {voice} {speed} 를 바꿔 실행. {out} 필수 (WAV 파일)
    std::string commandPath;
    std::string commandArgs;
    bool commandStdin = false;       // 텍스트를 표준 입력(UTF-8)으로도 넘김
    int commandTimeoutSec = 60;
    friend bool operator==(const TtsConfig&, const TtsConfig&) = default;
};

// 전역 단축키 "Ctrl+Alt+P" 형식. 키는 A~Z, 0~9, F1~F12
struct Hotkey {
    bool ctrl = false, alt = false, shift = false, win = false;
    char key = 0;   // 'A'~'Z', '0'~'9'
    int fkey = 0;   // 1~12 (key 대신)
};
std::optional<Hotkey> ParseHotkey(std::string_view text);
// http://127.0.0.1·localhost·[::1] 인가 (로컬 서비스 주소 검사)
bool IsLoopbackUrl(std::string_view url);
// TTS 명령 실행 파일 종류 (확장자, 대소문자 무시). 배치(.bat/.cmd)는 cmd.exe 가 명령줄을 해석하므로
// 발화·음성 값을 인수로 넘기지 않는다 ({text_file}·command_stdin 만, 보안 점검 2026-10-20)
enum class CommandKind { Exe, Batch, Unsupported };
CommandKind CommandKindOf(std::string_view path);

// 사용자 설정 (%LOCALAPPDATA%\DesktopPet\config.toml). 경로 결정·파일 입출력 위치는 플랫폼 계층이 정한다.
// 기본값은 P1 계획 §6.1 결정(P1-D1~D6)과 §6.2 초기 제안값.
struct Config {
    // 표시
    double scale = 1.0;            // 사용자 배율 (DPI 배율과 곱해짐)
    bool proximityReaction = true; // 커서 근접 반응
    int frameRate = 30;            // 화면 갱신 상한 (물리 이동의 창 반영·애니메이션 Present). 물리 계산은 physics.hz
    std::string language = "auto"; // UI 언어: auto(Windows 표시 언어) | ko | en
    std::string theme = "auto";    // 창 테마: auto(Windows 앱 모드) | light | dark
    bool updateCheck = true;       // [update] check: 하루 1번 GitHub 릴리스의 latest.json 확인 (알림만, P6-03)

    // 입력
    int dragThresholdPx = 4;       // 96 DPI 기준
    int alphaThreshold = 32;       // hit-test 불투명 판정 (0~255)
    // hit-test Region 단위: "frame" = 프레임마다 정확히 (기본, ADR-09), "clip" = 동작(클립) 프레임들의 합집합을
    // 동작이 바뀔 때만 설정 (SetWindowRgn 호출 감소, 투명 부분 일부가 클릭을 받을 수 있음). M1 측정용 비교 옵션
    std::string hitRegion = "frame";

    // 물리 (P1-D6)
    double physicsHz = 60.0;       // 활동 중에만
    double gravityPxPerSec2 = 2400.0;  // 96 DPI 기준
    double walkSpeedPxPerSec = 70.0;   // 96 DPI 기준. P2부터 캐릭터 behavior.toml 값이 우선 (패키지가 항상 값을 가짐)

    // 월드 (P1-D1, P1-D2, P1-D6)
    double footSupportRatio = 0.5; // 착지에 필요한 발 지지 폭 = 스프라이트 폭 × 비율 (미확정 초기값). P2부터 캐릭터 값 우선
    int occlusionGraceMs = 250;    // 부착 지점이 가려져도 이 시간 동안은 낙하하지 않음 (미확정 초기값)
    int resyncIntervalSec = 30;    // 창 목록 전체 재동기화

    // 전체화면 숨김 (기준선 §4.3)
    double fullscreenCoverage = 0.98;           // 전경 창이 모니터를 이 비율 이상 덮으면 전체화면 후보
    std::vector<std::string> alwaysShowExe;     // 전체화면이어도 숨기지 않음 (실행 파일 이름, 소문자 비교)
    std::vector<std::string> alwaysHideExe;     // 전경이면 항상 숨김
    std::vector<std::string> excludedSurfaceExe;// 이 앱의 창에는 착지하지 않음

    // 캐릭터 (P2). 합계는 kMaxPets 로 제한 (P2-R6)
    std::vector<PetSpawn> pets = {{"windowling", 1}};  // 예제 캐릭터 (릴리스에 따로 올림)
    static constexpr int kMaxPets = 8;

    AiConfig ai;
    ObserveConfig observe;
    TtsConfig tts;
    friend bool operator==(const Config&, const Config&) = default;
};

struct ConfigLoadResult {
    Config config;
    std::vector<std::string> warnings;  // 잘못된 값은 기본값으로 대체하고 경고만 남긴다
    bool parsed = false;                // false 면 TOML 문법 오류 → 전체 기본값
};

// TOML 문자열 → Config. 알 수 없는 키는 경고, 범위 밖 값은 기본값.
ConfigLoadResult ParseConfig(const std::string& tomlText);
// Config → TOML 문자열 (기본 설정 파일 생성·저장용)
std::string SerializeConfig(const Config& config);

// 설정 창(UI-07)이 다루는 항목만 from → to 로 복사. 저장할 때 디스크의 설정 파일을 다시 읽어 이 항목만 덮어쓴다
// (펫 목록 [[pets]] 은 캐릭터 관리 창, 물리·입력·월드 등은 설정 파일에서만 바꾼다)
void CopyUiFields(const Config& from, Config& to);
// 설정 창이 다루는 항목 중 a 와 b 가 다른 것의 설정 파일 키 (로그·즉시 반영 판단용)
std::vector<std::string> DiffUiFields(const Config& a, const Config& b);

}  // namespace dp::core
