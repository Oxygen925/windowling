#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "core/world.h"

namespace dp::core {

enum class BodyMode { Falling, Grounded, Attached, Dragged };
const char* ToString(BodyMode m);

struct PhysicsParams {
    double gravity = 2400.0;      // px/s² (DPI 반영은 호출자가)
    double maxFallSpeed = 3000.0; // px/s
    double walkSpeed = 70.0;      // px/s
    double footSupportRatio = 0.5;  // 발 지지 폭 = width × 비율 (P1-D1)
    double occlusionGraceSec = 0.25;// 부착 지점이 가려져도 이 시간 동안은 낙하하지 않음 (P1-D2)
};

// 펫 1마리의 물리 상태. 좌표는 가상 화면 물리 픽셀, foot = 스프라이트 하단 중앙.
struct PetBody {
    PointD foot;
    double vy = 0;
    int32_t width = 128, height = 128;
    BodyMode mode = BodyMode::Falling;
    WindowId host = kNoWindow;  // Attached 일 때 서 있는 창
    double localX = 0;          // host.left 기준 발 x (기준선 §4.2: WindowId + 로컬 x + 접촉면)
    int walkDir = 0;            // -1 / 0 / +1 (행동 계층이 정함)
    double occludedSince = -1;  // 가림 유예 시작 시각 (-1 = 가려지지 않음)
};

// 상태 전이 기록 (로그·시나리오 테스트 판정용)
struct BodyEvent {
    enum class Kind { Landed, Detached, Turned, Relocated } kind;
    WindowId window = kNoWindow;  // Landed: 착지한 창 (바닥이면 kNoWindow)
    std::string reason;
};

class Physics {
public:
    // 월드가 바뀔 때마다 호출 (표면 재계산). 착지 위 공간은 펫마다 자기 키(body.height)로 판정한다
    void SetWorld(const WorldSnapshot& world);
    const WorldSnapshot& World() const { return world_; }
    const std::vector<Surface>& Surfaces() const { return surfaces_; }

    // 고정 스텝 1회. now 는 가림 유예 판정용 시각(초)
    std::vector<BodyEvent> Step(PetBody& body, const PhysicsParams& p, double dt, double now);
    // 창 변화 직후 위치·부착 재검증 (스텝 없이)
    std::vector<BodyEvent> Revalidate(PetBody& body, const PhysicsParams& p, double now);

    // 물리 스텝이 필요한 상태인가 (아니면 이벤트까지 정지 — P1-D6)
    static bool NeedsStepping(const PetBody& body);

    // 드래그 시작/끝
    static void BeginDrag(PetBody& body);
    static void EndDrag(PetBody& body);

private:
    const Surface* SurfaceOf(WindowId owner, double x) const;
    void Detach(PetBody& body, std::vector<BodyEvent>& events, const char* reason);
    void ClampIntoScreen(PetBody& body, std::vector<BodyEvent>& events);
    void ValidateResting(PetBody& body, const PhysicsParams& p, double now, std::vector<BodyEvent>& events);

    WorldSnapshot world_;
    std::vector<Surface> surfaces_;
    std::unordered_map<WindowId, size_t> windowIndex_;
};

}  // namespace dp::core
