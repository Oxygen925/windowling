#pragma once
// 펫끼리 상호작용 (인사·따라가기). 상태·위치를 받아 결정만 돌려주는 순수 함수 — 실행(말풍선·이동)은 앱이 한다
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "core/physics.h"

namespace dp::core {

struct SocialPet {
    int id = -1;
    PointD foot;
    int width = 128;
    BodyMode mode = BodyMode::Falling;
    WindowId host = kNoWindow;
    bool available = false;  // 상호작용할 수 있는 상태 (쉬는 중, 끌기·말하기·말풍선·숨김·점프·벽 타기 아님)
    bool greets = false;     // 캐릭터가 인사를 켬 (behavior.toml greet)
    bool bumps = false;      // 걷다 마주치면 튕김 (behavior.toml bump)
    bool leans = false;      // 둘 다 앉아 있으면 붙어 앉음 (behavior.toml lean)
    int walkDir = 0;         // 걷는 방향 (-1/0/+1)
    bool sitting = false;    // 앉아 있음 (sit)
};

// 같은 면 위인가 (같은 창 위, 또는 둘 다 바닥이고 높이가 같음)
bool SameSurface(const SocialPet& a, const SocialPet& b);

// 쌍마다 마지막 인사 시각 (같은 쌍이 계속 인사하지 않게)
class GreetBook {
public:
    bool Ready(int a, int b, double now, double cooldownSec) const;
    void Mark(int a, int b, double now);
    void Forget(int id);  // 펫이 없어지면

private:
    std::map<std::pair<int, int>, double> last_;
};

// 지금 인사할 쌍 하나 (가장 가까운): 둘 다 available·greets, 같은 면, 가로 간격 ≤ 두 폭 평균 × distFactor, 쿨다운 지남
std::optional<std::pair<int, int>> FindGreeting(const std::vector<SocialPet>& pets, const GreetBook& book, double now,
                                                double cooldownSec, double distFactor = 1.1);

// 걷다가 마주침: 둘 다 available·bumps, 같은 면, 간격 ≤ 두 폭 평균 × distFactor, 적어도 한쪽이 상대 쪽으로 걷는 중, 쿨다운 지남
std::optional<std::pair<int, int>> FindBump(const std::vector<SocialPet>& pets, const GreetBook& book, double now,
                                            double cooldownSec, double distFactor = 0.75);
// 붙어 앉기: 둘 다 available·leans·앉아 있음, 같은 면, 간격 ≤ 두 폭 평균 × maxFactor, 쿨다운 지남 (가장 가까운 쌍)
std::optional<std::pair<int, int>> FindLean(const std::vector<SocialPet>& pets, const GreetBook& book, double now,
                                            double cooldownSec, double maxFactor = 3.0);

// 따라가기 한 단계
struct FollowStep {
    enum class Kind { Walk, Jump, Wait, Caught } kind = Kind::Wait;
    int dir = 0;  // Walk: -1 / +1 (Jump 이 안 되면 앱이 이 방향으로 걷는다)
};
// target 이 공중·벽·끌림이면 기다림. 같은 면이면 걸어가고, 닿으면(두 폭 평균 × catchFactor) 잡음. 다른 면이면 점프
FollowStep DecideFollow(const SocialPet& me, const SocialPet& target, double catchFactor = 0.9);

}  // namespace dp::core
