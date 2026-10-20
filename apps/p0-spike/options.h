#pragma once
#include <string>

#include "platform/windows/strings.h"
#include "rendering/d3d11/content_mode.h"

namespace dp {

enum class HitTestMode { None, A, B, C };    // §3.3 후보 (None = 처리 없음, 전제 확인용)
enum class OverlayMode { Small, Single };    // §3.4
enum class TimerMode { MsgWait, HiRes, SetTimer };  // E4
enum class AnchorMode { Bottom, TopLeft };  // 초기 배치 (top-left: 측정 중 커서 간섭 방지)

struct Options {
    HitTestMode hitTest = HitTestMode::A;
    OverlayMode overlay = OverlayMode::Small;
    ContentMode content = ContentMode::SwapChain;
    TimerMode timer = TimerMode::HiRes;
    AnchorMode anchor = AnchorMode::Bottom;

    int pets = 1;
    int animFps = 0;         // 0 = 정지(상태 변화 시에만 렌더)
    bool move = false;       // 애니메이션 중 좌우 이동
    bool proximity = true;   // 커서 근접 반응
    bool cube = false;       // E8: 3D 큐브
    bool text = false;       // P0-11: D2D 텍스트
    bool ecoQos = false;     // 프로세스 EcoQoS
    bool gpu = true;         // false: GPU 장치 미생성 (S0-A, pets=0 전용)
#ifdef _DEBUG
    bool debugLayer = true;   // D3D11 debug layer 시도 (Debug 빌드 기본, 없으면 자동 폴백)
#else
    bool debugLayer = false;  // Release 측정에 debug layer 비용이 섞이지 않도록 기본 OFF
#endif
    float scale = 1.0f;      // 사용자 배율

    int pollNearMs = 33;     // 커서 근접 시 확인 주기
    int pollFarMs = 200;     // 커서가 멀 때 확인 주기
    int alphaThreshold = 32; // hit-test 불투명 판정 임계값 (0~255)

    int durationSec = 0;     // 0 = 무제한. 측정 구간 길이
    int warmupSec = 0;       // 로그 기록 전 안정화 시간
    int followDelaySec = -1; // >=0 이면 시작 후 N초 뒤 전경 창 추종 시작

    std::wstring logPath;    // CSV 경로 (비어 있으면 측정 로그 없음)
    std::wstring capturePath;  // 지정 시 펫 영역 화면 캡처 PNG 저장 (자체 검증용)
    int captureDelayMs = 1500;
    int recreateAtSec = -1;  // >=0 이면 N초 후 GPU 재생성 시뮬레이션 (P0-12 자동 점검)
    std::wstring scenario = L"adhoc";
    std::wstring variant;
    int runIndex = 1;
};

// 실패 시 false, error에 사유(UTF-8)
bool ParseOptions(int argc, wchar_t** argv, Options& out, std::string& error);
// 위험하거나 미지원 조합 검사 (예: 단일 오버레이 + hit-test 없음)
bool ValidateOptions(const Options& o, std::string& error);
std::string DescribeOptions(const Options& o);

const char* ToString(HitTestMode m);
const char* ToString(OverlayMode m);
const char* ToString(TimerMode m);


}  // namespace dp
