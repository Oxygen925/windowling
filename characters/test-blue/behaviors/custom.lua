-- 파랑 테스트 캐릭터 행동 스크립트 (P2 Lua 훅 예시, CC0)
-- 정의하지 않은 경우·nil 을 돌려준 경우는 behavior.toml 기본 FSM 이 결정한다.

local walks = 0

function on_rest(ctx)
  -- 창 위에서는 세 번 걸으면 한 번 앉는다
  if ctx.mode == "attached" then
    walks = walks + 1
    if walks % 3 == 0 then return "sit", 4 end
  end
  return nil
end

function on_event(name, ctx)
  if name == "click" then return "sit", 2 end   -- 클릭하면 잠깐 앉는다 (주황은 look)
  return nil
end
