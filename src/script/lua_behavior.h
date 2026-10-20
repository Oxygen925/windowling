#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/behavior/fsm.h"

struct lua_State;
struct lua_Debug;

namespace dp::script {

// P2-R4 제한 (미확정 초기값)
struct LuaLimits {
    uint64_t maxInstructionsPerCall = 1'000'000;
    size_t maxMemoryBytes = 4 * 1024 * 1024;
    int disableAfterConsecutiveErrors = 3;
    // 보안 점검 (2026-10-20): 명령 수 제한은 C 함수(문자열 패턴 매칭) 안의 시간을 막지 못한다.
    // 패턴 함수(find·match·gmatch·gsub)의 대상·패턴 길이와 수량자 수를 묶어 한 번의 매칭을 짧게 하고,
    // 호출마다 실제 시간 상한을 둔다 (명령 1000개마다 확인)
    size_t maxPatternSubjectBytes = 256;
    size_t maxPatternBytes = 64;
    int maxPatternQuantifiers = 2;
    double maxCallSeconds = 0.05;
};

// 캐릭터 behaviors/custom.lua 를 실행하는 샌드박스 (P2-R3: 펫 인스턴스마다 1개).
// 허용: base(일부 제외)·table·string·math. 제거: io·os·package·debug·load·loadfile·dofile·require·string.dump·collectgarbage.
// 각 호출은 명령 수·실제 시간 제한, 상태 전체는 메모리 제한, 문자열 패턴 함수는 길이·수량자 제한. 오류가 연속되면 스크립트를 끄고 기본 FSM 으로 돌아간다.
class LuaBehavior final : public core::BehaviorHooks {
public:
    ~LuaBehavior() override;
    // 실패 시 nullptr, error 에 사유
    static std::unique_ptr<LuaBehavior> Create(const std::string& source, const std::string& chunkName, uint32_t seed,
                                               const LuaLimits& limits, std::string& error);

    std::optional<core::BehaviorDecision> OnRest(const core::BehaviorContext& ctx) override;
    std::optional<core::BehaviorDecision> OnEvent(std::string_view event, const core::BehaviorContext& ctx) override;

    bool Disabled() const { return disabled_; }
    size_t MemoryBytes() const { return memory_; }
    uint64_t Errors() const { return totalErrors_; }
    const std::string& LastError() const { return lastError_; }

private:
    LuaBehavior() = default;
    static void* Alloc(void* ud, void* ptr, size_t osize, size_t nsize);
    static void CountHook(lua_State* L, ::lua_Debug* ar);
    static int GuardedPattern(lua_State* L);  // 원래 패턴 함수 = upvalue 1
    std::optional<core::BehaviorDecision> Call(const char* fn, const char* eventName, const core::BehaviorContext& ctx);
    void PushContext(const core::BehaviorContext& ctx);
    void Fail(const std::string& message);

    lua_State* L_ = nullptr;
    LuaLimits limits_;
    size_t memory_ = 0;
    uint64_t budget_ = 0;
    std::chrono::steady_clock::time_point deadline_{};
    uint64_t totalErrors_ = 0;
    int consecutiveErrors_ = 0;
    bool disabled_ = false;
    bool hasOnRest_ = false, hasOnEvent_ = false;
    std::string lastError_;
    std::string chunkName_;
};

}  // namespace dp::script
