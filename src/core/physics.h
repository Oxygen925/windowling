#pragma once
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/world.h"

namespace dp::core {

// Riding = 다른 펫 머리 위에 올라탐 (위치는 앱이 그 펫을 따라 맞춘다, 물리 스텝 없음)
enum class BodyMode { Falling, Grounded, Attached, Dragged, Climbing, Riding };
const char* ToString(BodyMode m);

struct PhysicsParams {
    double gravity = 2400.0;      // px/s² (DPI 반영은 호출자가)
    double maxFallSpeed = 3000.0; // px/s
    double walkSpeed = 70.0;      // px/s
    double footSupportRatio = 0.5;  // 발 지지 폭 = width × 비율 (P1-D1)
    double occlusionGraceSec = 0.25;// 부착 지점이 가려져도 이 시간 동안은 낙하하지 않음 (P1-D2)
    double jumpHeight = 320.0;    // 다른 창 위로 뛸 때 오를 수 있는 최대 높이 (px). 내려갈 때는 1.5배까지
    double jumpReach = 640.0;     // 다른 창으로 뛸 때 가로 최대 거리 (px)
    double hopHeight = 56.0;      // 제자리·앞으로 뛰기 높이 (px)
    double climbSpeed = 50.0;     // 벽 오르기 속도 (px/s)
    double stepSec = 1.0 / 60.0;  // 고정 스텝 (점프 계획을 같은 스텝으로 미리 시뮬레이션해 확인)
};

// 벽 타기 계획: 서 있는 면에서 startX 까지 걸어간 뒤 벽(창 옆면 또는 화면 바깥 끝)을 오른다
struct ClimbPlan {
    WindowId wall = kNoWindow;  // 오를 창 (kNoWindow = 화면 끝)
    int side = 0;               // +1 = 벽이 펫 오른쪽, -1 = 왼쪽
    double wallX = 0;           // 벽 x
    double startX = 0;          // 오르기 시작할 발 x (= wallX - side × width/2)
    double topY = 0;            // 화면 끝: 여기까지 오르면 뛰어내린다 (창은 창 상단)
    double leapToY = 0;         // 떠 있는 창: 벽 아래끝보다 발이 낮으면 여기까지 뛰어 벽에 붙는다 (0 = 없음)
    bool leaping = false;       // 벽에 뛰어 붙는 중 (Climbing, 위로 감속하며 오름)
};

// 점프: 처음 속도. toWindow 면 target 창 위로 (아니면 제자리·앞으로 뛰기)
struct JumpPlan {
    double vx = 0, vy = 0;
    WindowId target = kNoWindow;
    bool toWindow = false;
    double landX = 0;           // 시뮬레이션으로 확인한 착지 x
};

// 펫 1마리의 물리 상태. 좌표는 가상 화면 물리 픽셀, foot = 스프라이트 하단 중앙.
struct PetBody {
    PointD foot;
    double vy = 0;
    double vx = 0;              // 점프·벽에서 뛰어내릴 때 가로 속도 (착지하면 0)
    int32_t width = 128, height = 128;
    BodyMode mode = BodyMode::Falling;
    WindowId host = kNoWindow;  // Attached 일 때 서 있는 창
    double localX = 0;          // host.left 기준 발 x (기준선 §4.2: WindowId + 로컬 x + 접촉면)
    int walkDir = 0;            // -1 / 0 / +1 (행동 계층이 정함)
    double occludedSince = -1;  // 가림 유예 시작 시각 (-1 = 가려지지 않음)
    std::optional<ClimbPlan> climb;  // 벽으로 걸어가는 중(Grounded/Attached) 또는 오르는 중(Climbing)
};

// 상태 전이 기록 (로그·시나리오 테스트 판정용)
struct BodyEvent {
    enum class Kind { Landed, Detached, Turned, Relocated, Jumped, Climbing } kind;
    WindowId window = kNoWindow;  // Landed: 착지한 창 (바닥이면 kNoWindow). Climbing: 오르는 창
    std::string reason;
};

// 올라타기: 이번 스텝에 내려오며 머리 높이(headY)를 지났고 가로로 tolerance 안이면 true
inline bool CrossesHead(double prevFootY, double newFootY, double footX, double headX, double headY, double tolerance) {
    return prevFootY <= headY && newFootY >= headY && footX >= headX - tolerance && footX <= headX + tolerance;
}
// 올라탄 펫의 발 위치 = 아래 펫 발에서 키 × headHeight 위
inline PointD RiderFoot(const PetBody& mount, double headHeight) {
    return {mount.foot.x, mount.foot.y - mount.height * headHeight};
}

// 점프·벽 타기 계획에서 후보가 빠진 이유 (로그 진단용). 계획 함수가 부를 때마다 새로 채운다
struct PlanStats {
    int candidates = 0;  // 살펴본 면 구간·벽
    int height = 0;      // 높이·내려가는 거리가 한도 밖, 또는 키보다 낮은 벽
    int reach = 0;       // 가로 거리가 한도 밖·방향이 다름
    int room = 0;        // 올라설 자리(여유 높이·폭) 없음
    int blocked = 0;     // 비행 시뮬레이션에서 다른 면에 먼저 내려앉음 / 벽 옆면이 가려짐 / 걸어갈 길이 끊김
    int ok = 0;
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

    // 점프 계획 (쉬는 중일 때만). toWindow 면 닿는 다른 창 위(높은·같은·낮은 창)를 먼저 찾고, 없으면 dir 방향으로 작게 뛴다.
    // 후보는 같은 고정 스텝으로 비행을 미리 돌려 처음 내려앉는 면이 목표일 때만 쓴다. pick(0~1) 은 가까운 후보 3개 중 고르기
    std::optional<JumpPlan> PlanJump(const PetBody& body, const PhysicsParams& p, int dir, bool toWindow, double pick = 0) const;
    // 특정 면(창 또는 바닥 kNoWindow) 의 targetX 근처로 뛰기 (따라가기). 닿지 않으면 nullopt
    std::optional<JumpPlan> PlanJumpTo(const PetBody& body, const PhysicsParams& p, WindowId target, double targetX) const;
    static void StartJump(PetBody& body, const JumpPlan& plan);
    // 벽 타기 계획 (쉬는 중일 때만): 지금 면에서 걸어서 닿는 가장 가까운 벽. 없으면 nullopt
    std::optional<ClimbPlan> PlanClimb(const PetBody& body, const PhysicsParams& p) const;
    // 점 (x, y) 를 지나 내려오는 점프 (다른 펫 머리 위로). 꼭대기는 출발·목표 중 높은 쪽보다 키의 40% 위, 화면 위로는 넘지 않음
    std::optional<JumpPlan> AimAtPoint(const PetBody& body, const PhysicsParams& p, double x, double y) const;
    // 화면 바깥 끝 중 side 쪽(-1 왼쪽·+1 오른쪽)으로만 (벽 타기 경주). 걸어서 닿지 않거나 맞닿은 모니터 끝이면 nullopt
    std::optional<ClimbPlan> PlanClimbEdge(const PetBody& body, const PhysicsParams& p, int side) const;
    // 벽으로 걸어가기 시작 (이미 그 자리면 다음 스텝에서 오르기 시작)
    static void StartClimb(PetBody& body, const ClimbPlan& plan);
    const PlanStats& LastPlanStats() const { return stats_; }

private:
    const Surface* SurfaceOf(WindowId owner, double x) const;
    void Detach(PetBody& body, std::vector<BodyEvent>& events, const char* reason);
    void ClampIntoScreen(PetBody& body, std::vector<BodyEvent>& events);
    void ValidateResting(PetBody& body, const PhysicsParams& p, double now, std::vector<BodyEvent>& events);
    void ValidateClimb(PetBody& body, const PhysicsParams& p, double now, std::vector<BodyEvent>& events);
    void StepClimb(PetBody& body, const PhysicsParams& p, double dt, std::vector<BodyEvent>& events);
    bool WallVisible(const PetBody& body, double y0, double y1) const;
    // 낙하 1스텝 (중력·가로 이동·착지 면 찾기). 착지하면 그 면 (발 y 를 맞춤), 아니면 nullptr. Step 과 점프 계획이 함께 쓴다
    const Surface* AdvanceFall(PetBody& body, const PhysicsParams& p, double dt) const;
    // 점프를 끝까지 미리 돌려 처음 내려앉는 면 (없으면 nullptr)
    const Surface* SimulateJump(const PetBody& body, const PhysicsParams& p, const JumpPlan& plan, double& landX) const;
    // 그 면의 노출 구간 하나를 노리는 점프 (꼭대기는 출발·도착 중 높은 쪽보다 조금 위). 안 되면 nullopt
    std::optional<JumpPlan> AimAt(const PetBody& body, const PhysicsParams& p, const Surface& s, double x) const;
    // 면 s 의 노출 구간마다 착지 x 를 골라(chooseX: 착지 가능 범위 lo~hi → x, 닿지 않으면 nullopt) 조준하고,
    // 비행 시뮬레이션에서 처음 내려앉는 면이 s 일 때만 visit(계획, x). 빠진 이유는 stats_ 에 센다 (PlanJump·PlanJumpTo 공용)
    void JumpCandidates(const PetBody& body, const PhysicsParams& p, const Surface& s,
                        const std::function<std::optional<double>(double lo, double hi)>& chooseX,
                        const std::function<void(JumpPlan& plan, double x)>& visit) const;
    bool IsOuterEdge(double x, const RectI& work) const;

    WorldSnapshot world_;
    std::vector<Surface> surfaces_;
    mutable PlanStats stats_;
    std::unordered_map<WindowId, size_t> windowIndex_;
};

}  // namespace dp::core
