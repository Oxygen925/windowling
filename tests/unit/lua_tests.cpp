#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "core/character/package.h"
#include "script/lua_behavior.h"

using namespace dp;

namespace {
std::unique_ptr<script::LuaBehavior> Make(const std::string& src, std::string& err, script::LuaLimits lim = {}) {
    return script::LuaBehavior::Create(src, "test.lua", 42, lim, err);
}
core::BehaviorContext Ctx() { return {1.0, true, "idle"}; }
}  // namespace

TEST_CASE("Lua hooks return decisions", "[lua]") {
    std::string err;
    auto lb = Make(R"(
function on_rest(ctx)
  if ctx.mode == "attached" then return "walk", 3.5, -1 end
  return "sit", 2
end
function on_event(name, ctx)
  if name == "click" then return "look", 1.5 end
  return nil
end
)", err);
    REQUIRE(lb);
    auto d = lb->OnRest(Ctx());
    REQUIRE(d);
    CHECK(d->activity == "walk");
    CHECK(d->durationSec == 3.5);
    CHECK(d->walkDir == -1);
    auto e = lb->OnEvent("click", Ctx());
    REQUIRE(e);
    CHECK(e->activity == "look");
    CHECK_FALSE(lb->OnEvent("landed", Ctx()).has_value());  // nil → 기본 처리
}

TEST_CASE("Lua sandbox removes dangerous functions", "[lua]") {
    std::string err;
    auto lb = Make(R"(
function on_rest(ctx)
  local found = {}
  if io then found[#found+1] = "io" end
  if os then found[#found+1] = "os" end
  if package then found[#found+1] = "package" end
  if debug then found[#found+1] = "debug" end
  if load then found[#found+1] = "load" end
  if loadfile then found[#found+1] = "loadfile" end
  if dofile then found[#found+1] = "dofile" end
  if require then found[#found+1] = "require" end
  if string.dump then found[#found+1] = "dump" end
  if #found > 0 then return table.concat(found, ","), 1 end
  return "idle", 1
end
)", err);
    REQUIRE(lb);
    auto d = lb->OnRest(Ctx());
    REQUIRE(d);
    CHECK(d->activity == "idle");  // 아무것도 남아 있지 않다
}

TEST_CASE("Lua instruction limit stops infinite loop", "[lua]") {
    std::string err;
    script::LuaLimits lim;
    lim.maxInstructionsPerCall = 100'000;
    auto lb = Make("function on_rest(ctx) while true do end end", err, lim);
    REQUIRE(lb);
    CHECK_FALSE(lb->OnRest(Ctx()).has_value());
    CHECK(lb->LastError().find("instruction limit") != std::string::npos);
    CHECK_FALSE(lb->Disabled());

    SECTION("연속 3회 오류면 비활성") {
        lb->OnRest(Ctx());
        lb->OnRest(Ctx());
        CHECK(lb->Disabled());
    }
}

TEST_CASE("Lua memory limit", "[lua]") {
    std::string err;
    script::LuaLimits lim;
    lim.maxMemoryBytes = 512 * 1024;
    auto lb = Make("function on_rest(ctx) local t = {} for i = 1, 1e7 do t[i] = string.rep('x', 100) .. i end return 'idle', 1 end", err, lim);
    REQUIRE(lb);
    CHECK_FALSE(lb->OnRest(Ctx()).has_value());
    CHECK(lb->MemoryBytes() <= lim.maxMemoryBytes);
}

TEST_CASE("Lua load errors", "[lua]") {
    std::string err;
    CHECK_FALSE(Make("function on_rest(", err));
    CHECK_FALSE(err.empty());
    err.clear();
    CHECK_FALSE(Make("while true do end", err, script::LuaLimits{100'000}));  // 최상위 무한 루프도 제한
    CHECK(err.find("instruction limit") != std::string::npos);
    err.clear();
    CHECK_FALSE(Make("\x1bLua", err));  // 바이트코드 거부
}

TEST_CASE("Shipped test-blue script drives FSM", "[lua]") {
    auto r = core::LoadPackage(std::filesystem::path(DP_SOURCE_DIR) / "characters" / "test-blue");
    REQUIRE(r.package);
    std::string err;
    auto lb = script::LuaBehavior::Create(r.package->behavior.scriptSource, "test-blue/custom.lua", 7, {}, err);
    REQUIRE(lb);
    core::BehaviorMachine fsm(r.package->behavior, 7, lb.get());
    fsm.OnEvent("click", 0.0, true);
    CHECK(fsm.Activity() == "sit");  // 주황(기본)은 look
    // 부착 상태 휴식 결정 3번 중 1번은 Lua 가 sit 4초
    int sits = 0;
    double t = 10.0;
    for (int i = 0; i < 3; ++i) {
        fsm.Force({"idle", 0.5, 0}, t);
        t += 1.0;
        fsm.Update(t, true, true);
        if (fsm.Activity() == "sit") sits++;
    }
    CHECK(sits >= 1);
    CHECK(fsm.HookDecisions() >= 2);
    CHECK(fsm.RejectedHookDecisions() == 0);
}

// 보안 점검 (2026-10-20): C 함수 안의 시간 — 패턴 함수 제한과 실제 시간 상한
TEST_CASE("Lua pattern functions are bounded", "[lua][security]") {
    std::string err;
    auto timeCall = [&](const char* body, std::string& lastError) {
        const std::string src = std::string("function on_rest(ctx)\n") + body + "\nreturn 'idle', 1\nend\n";
        auto lua = script::LuaBehavior::Create(src, "t", 1, {}, err);
        REQUIRE(lua);
        const auto t0 = std::chrono::steady_clock::now();
        lua->OnRest(Ctx());
        lastError = lua->LastError();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    std::string last;
    // 큰 대상·많은 수량자는 바로 거부
    timeCall("local s = ('a'):rep(4000); s:gsub('a*b', '')", last);
    CHECK(last.find("subject too long") != std::string::npos);
    timeCall("local s = ('a'):rep(100); s:find(('a*'):rep(10) .. 'b')", last);
    CHECK(last.find("quantifiers") != std::string::npos);
    timeCall("local s = ('a'):rep(100); string.match(s, ('a'):rep(100))", last);
    CHECK(last.find("pattern too long") != std::string::npos);
    // 허용 범위 안의 최악(대상 256·수량자 2, 모든 시작 위치를 시도하는 gsub)도 짧게 끝난다
    const double worst = timeCall("local s = ('a'):rep(256); s:gsub('a*a*b', '')", last);
    CHECK(last.empty());
    CHECK(worst < 0.25);  // 릴리스 약 0.02초, 디버그 빌드(CI) 약 0.09초 — 제한 전에는 수십 초였다
    // 패턴 함수를 계속 부르는 반복은 실제 시간 상한(0.05초)에 걸린다
    const double loop = timeCall("local s = ('a'):rep(256); for i = 1, 1e9 do s:find('a*a*b') end", last);
    CHECK((last.find("time limit") != std::string::npos || last.find("instruction limit") != std::string::npos));
    CHECK(loop < 0.5);
    // 보통 쓰임은 그대로
    timeCall("assert(('key=value'):match('(%w+)=(%w+)') == 'key')", last);
    CHECK(last.empty());
}
