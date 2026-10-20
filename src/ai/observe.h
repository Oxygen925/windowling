#pragma once
// P4 화면 관찰·자율 발화의 Windows 비의존 부분 (P4 계획 §2.2~§2.5):
// Privacy Guard 판정, 시도 간격·전송 횟수, 관찰 요청 생성, 응답(JSON, 기준선 §7.3) 검증.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "ai/responses.h"

namespace dp::ai {

// ---- 동의 범위 (보안 점검 2026-10-20): 동의할 때 보여 준 값. 이보다 넓어지면 관찰을 끄고 다시 동의를 받는다 ----
struct ObserveScope {
    int maxPerHour = 2, maxPerDay = 16;
    int intervalMinMin = 15, intervalMaxMin = 30;
    int requireActivityMin = 5;  // 0 = 자리 비움 검사 안 함 (가장 넓음)
    int imageMaxPx = 1024;
    bool sendAppName = true;
    std::vector<std::string> excludeExe;  // 소문자
};
// now 가 consented 보다 넓은 항목의 설정 키 (상한·이미지 크기↑, 간격↓, 자리 비움 검사 완화, 앱 이름 켜기, 제외 앱 삭제)
std::vector<std::string> WiderScope(const ObserveScope& consented, const ObserveScope& now);

// ---- Privacy Guard: 캡처 직전에 매번 판정한다. 하나라도 어긋나면 캡처하지 않는다 ----
struct GuardInput {
    bool enabled = false;            // G1 관찰 켜짐
    bool consented = false;          // G1 현재 동의 버전에 동의
    bool signedIn = false;           // G2
    bool planUsage = false;          // G2
    bool imageSupported = true;      // 선택한 모델이 이미지를 거부한 적 없음
    bool usageLimited = false;       // 사용량 한도 오류 뒤 (그날 중지)
    bool paused = false;             // G3 사용자 일시정지
    bool locked = false;             // G4 잠금·화면 보호기·펫 숨김·절전
    bool fullscreen = false;         // G5 대상 모니터가 전체화면 숨김 상태
    bool notificationsBusy = false;  // G5 프레젠테이션·전체화면 D3D 등 (SHQueryUserNotificationState)
    bool excludedAppVisible = false; // G6 대상 모니터에 제외 앱 창이 보임
    double idleSec = 0;              // G7 마지막 사용자 입력 이후 시간
    bool chatBusy = false;           // G9 대화 스트림·로그인 진행 중
};

struct GuardLimits {
    int perHour = 2;
    int perDay = 16;
    double requireActivitySec = 300;  // 0 = 검사 안 함
};

// 요청 전송 시각 기록 (최근 24시간). 상한은 "시도" 가 아니라 "전송" 으로 센다
class ObserveCounter {
public:
    void RecordSent(double nowSec);
    int SentWithin(double nowSec, double windowSec) const;
    void Clear() { sent_.clear(); }
    // 재실행해도 상한이 초기화되지 않도록 저장·복원 (시각 기준은 호출자가 맞춘다)
    std::vector<double> Times() const { return {sent_.begin(), sent_.end()}; }
    void Restore(std::vector<double> times, double nowSec);

private:
    std::deque<double> sent_;
};

// 허용이면 nullptr, 아니면 건너뛴 사유 코드 (로그용, 예: "paused")
const char* EvaluateGuard(const GuardInput& in, const ObserveCounter& counter, const GuardLimits& limits, double nowSec);

// 다음 시도 시각: 직전 시도(또는 켜기·복귀) + [minSec, maxSec] 무작위
class ObserveSchedule {
public:
    void Configure(double minSec, double maxSec);
    void ScheduleNext(double nowSec, double rand01);
    void Clear() { next_ = -1; }
    bool Armed() const { return next_ >= 0; }
    double NextAt() const { return next_; }
    bool Due(double nowSec) const { return next_ >= 0 && nowSec >= next_; }

private:
    double min_ = 900, max_ = 1800, next_ = -1;
};

// ---- 요청 ----
inline constexpr std::string_view kEmotions[] = {"neutral", "happy", "curious", "sleepy", "surprised", "sad"};

// behaviors: 캐릭터 패키지에 있는 행동 이름 (이 목록 밖의 행동은 실행하지 않는다)
std::string BuildObserveInstructions(std::string_view characterName, std::string_view persona, size_t maxChars,
                                     const std::vector<std::string>& behaviors);
// 구조화 출력 스키마 (strict: 모든 필드 필수, 추가 필드 없음)
std::string ObserveSchema(size_t maxChars, const std::vector<std::string>& behaviors);

struct ObserveContext {
    std::string appName;    // 전경 창 실행 파일 이름 (보내지 않으면 빈 값)
    std::string localTime;  // "14:05" 형식
    std::string locale;     // 발화 언어 ("ko-KR")
};
// structured=false: text.format 을 거부한 모델 → 안내문으로 JSON 을 요청한다
AiRequest BuildObserveRequest(std::string_view model, std::string instructions, const ObserveContext& ctx,
                              std::string imageDataUrl, bool structured, size_t maxChars,
                              const std::vector<std::string>& behaviors);

// ---- 응답 검증 (기준선 §7.3: 허용된 행동·길이 확인. 화면 속 글은 권한을 줄 수 없다) ----
struct ObserveReply {
    bool ok = false;
    std::string error;     // ok=false 일 때 코드 (invalid_json, missing_field …)
    bool speak = false;
    std::string text;      // 정리된 발화 (제어 문자 제거, maxChars 이내)
    std::string emotion = "neutral";
    std::string behavior;  // 허용 목록에 있을 때만, 아니면 빈 값
};
ObserveReply ParseObserveReply(std::string_view modelText, const std::vector<std::string>& behaviors, size_t maxChars);

// ---- 기타 ----
std::string Base64Encode(const uint8_t* data, size_t size);  // 표준 base64 (패딩 포함)

}  // namespace dp::ai
