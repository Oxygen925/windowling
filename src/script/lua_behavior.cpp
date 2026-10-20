#include "script/lua_behavior.h"

#include <cmath>
#include <cstdlib>

#include <lua.hpp>

#include "core/log.h"

namespace dp::script {

namespace {
constexpr int kHookEvery = 1000;  // 명령 1000개마다 예산 확인
}

void* LuaBehavior::Alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* self = static_cast<LuaBehavior*>(ud);
    const size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        self->memory_ -= old;
        std::free(ptr);
        return nullptr;
    }
    // 메모리 제한: 넘으면 할당 실패 → Lua 가 "not enough memory" 오류를 낸다
    if (self->memory_ - old + nsize > self->limits_.maxMemoryBytes) return nullptr;
    void* p = std::realloc(ptr, nsize);
    if (p) self->memory_ = self->memory_ - old + nsize;
    return p;
}

void LuaBehavior::CountHook(lua_State* L, lua_Debug*) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    auto* self = static_cast<LuaBehavior*>(ud);
    if (self->budget_ <= kHookEvery) {
        self->budget_ = 0;
        luaL_error(L, "instruction limit exceeded");
    }
    self->budget_ -= kHookEvery;
    if (std::chrono::steady_clock::now() > self->deadline_) luaL_error(L, "time limit exceeded");
}

// string.find·match·gmatch·gsub 앞에서 대상·패턴 크기와 수량자(* + - ?) 수를 확인한다.
// Lua 패턴 매칭은 수량자마다 되돌아가며 시도하므로(지수적), 셋 다 묶어야 한 번의 호출이 짧다
int LuaBehavior::GuardedPattern(lua_State* L) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    const auto* self = static_cast<LuaBehavior*>(ud);
    const LuaLimits& lim = self->limits_;
    // 명령 수 hook 은 명령 1000개마다라, 그 사이 패턴 호출이 수백 번 들어갈 수 있다 → 여기서도 시간 확인
    if (std::chrono::steady_clock::now() > self->deadline_) return luaL_error(L, "time limit exceeded");
    size_t subjectLen = 0, patternLen = 0;
    luaL_checklstring(L, 1, &subjectLen);
    const char* pattern = luaL_checklstring(L, 2, &patternLen);
    if (subjectLen > lim.maxPatternSubjectBytes) return luaL_error(L, "pattern subject too long (max %d bytes)", static_cast<int>(lim.maxPatternSubjectBytes));
    if (patternLen > lim.maxPatternBytes) return luaL_error(L, "pattern too long (max %d bytes)", static_cast<int>(lim.maxPatternBytes));
    int quantifiers = 0;
    for (size_t i = 0; i < patternLen; ++i) {
        if (pattern[i] == '%') ++i;  // 이스케이프 다음 글자는 건너뛴다
        else if (pattern[i] == '*' || pattern[i] == '+' || pattern[i] == '-' || pattern[i] == '?') ++quantifiers;
    }
    if (quantifiers > lim.maxPatternQuantifiers) return luaL_error(L, "too many pattern quantifiers (max %d)", lim.maxPatternQuantifiers);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

LuaBehavior::~LuaBehavior() {
    if (L_) lua_close(L_);
}

std::unique_ptr<LuaBehavior> LuaBehavior::Create(const std::string& source, const std::string& chunkName, uint32_t seed,
                                                 const LuaLimits& limits, std::string& error) {
    std::unique_ptr<LuaBehavior> self(new LuaBehavior());
    self->limits_ = limits;
    self->chunkName_ = chunkName;
    self->L_ = lua_newstate(&LuaBehavior::Alloc, self.get(), seed);
    if (!self->L_) {
        error = "lua_newstate 실패";
        return nullptr;
    }
    lua_State* L = self->L_;
    // 허용 라이브러리만 연다
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_settop(L, 0);
    // base 중 파일·코드 로드·GC 조작 제거
    for (const char* name : {"dofile", "loadfile", "load", "require", "collectgarbage", "print"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    // 패턴 함수를 감싼다: string 표 자체를 바꾸므로 s:gsub(...) 같은 메서드 호출도 같은 검사를 거친다
    for (const char* name : {"find", "match", "gmatch", "gsub"}) {
        lua_getfield(L, -1, name);
        lua_pushcclosure(L, &LuaBehavior::GuardedPattern, 1);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
    // math.random 은 Lua 상태 시드로 결정적 (seed)

    lua_sethook(L, &LuaBehavior::CountHook, LUA_MASKCOUNT, kHookEvery);
    self->budget_ = limits.maxInstructionsPerCall;
    self->deadline_ = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                            std::chrono::duration<double>(limits.maxCallSeconds));
    // 텍스트 청크만 허용 (바이트코드 거부)
    int rc = luaL_loadbufferx(L, source.data(), source.size(), ("@" + chunkName).c_str(), "t");
    if (rc == LUA_OK) rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_OK) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        return nullptr;
    }
    lua_getglobal(L, "on_rest");
    self->hasOnRest_ = lua_isfunction(L, -1);
    lua_getglobal(L, "on_event");
    self->hasOnEvent_ = lua_isfunction(L, -1);
    lua_settop(L, 0);
    return self;
}

void LuaBehavior::PushContext(const core::BehaviorContext& ctx) {
    lua_createtable(L_, 0, 3);
    lua_pushnumber(L_, ctx.now);
    lua_setfield(L_, -2, "now");
    lua_pushstring(L_, ctx.attached ? "attached" : "grounded");
    lua_setfield(L_, -2, "mode");
    lua_pushstring(L_, ctx.last.c_str());
    lua_setfield(L_, -2, "last");
}

void LuaBehavior::Fail(const std::string& message) {
    lastError_ = message;
    totalErrors_++;
    if (++consecutiveErrors_ >= limits_.disableAfterConsecutiveErrors && !disabled_) {
        disabled_ = true;
        core::LogWarn("[lua] {} 연속 오류 {}회 → 스크립트 비활성, 기본 행동 사용: {}", chunkName_, consecutiveErrors_, message);
    } else {
        core::LogWarn("[lua] {} 오류 (기본 행동으로 대체): {}", chunkName_, message);
    }
}

std::optional<core::BehaviorDecision> LuaBehavior::Call(const char* fn, const char* eventName,
                                                        const core::BehaviorContext& ctx) {
    if (disabled_) return std::nullopt;
    lua_settop(L_, 0);
    lua_getglobal(L_, fn);
    if (!lua_isfunction(L_, -1)) {
        lua_settop(L_, 0);
        return std::nullopt;
    }
    int nargs = 1;
    if (eventName) {
        lua_pushstring(L_, eventName);
        nargs = 2;
    }
    PushContext(ctx);
    budget_ = limits_.maxInstructionsPerCall;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                       std::chrono::duration<double>(limits_.maxCallSeconds));
    if (lua_pcall(L_, nargs, 3, 0) != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        Fail(msg ? msg : "unknown error");
        lua_settop(L_, 0);
        return std::nullopt;
    }
    std::optional<core::BehaviorDecision> out;
    if (lua_type(L_, 1) == LUA_TSTRING) {  // nil 이면 기본 처리
        core::BehaviorDecision d;
        d.activity = lua_tostring(L_, 1);
        d.durationSec = lua_isnumber(L_, 2) ? lua_tonumber(L_, 2) : 1.0;
        d.walkDir = lua_isinteger(L_, 3) ? static_cast<int>(lua_tointeger(L_, 3)) : 0;
        if (d.activity == "walk" && d.walkDir == 0) d.walkDir = 1;
        out = d;
    }
    consecutiveErrors_ = 0;
    lua_settop(L_, 0);
    return out;
}

std::optional<core::BehaviorDecision> LuaBehavior::OnRest(const core::BehaviorContext& ctx) {
    return hasOnRest_ ? Call("on_rest", nullptr, ctx) : std::nullopt;
}

std::optional<core::BehaviorDecision> LuaBehavior::OnEvent(std::string_view event, const core::BehaviorContext& ctx) {
    if (!hasOnEvent_) return std::nullopt;
    const std::string e(event);
    return Call("on_event", e.c_str(), ctx);
}

}  // namespace dp::script
