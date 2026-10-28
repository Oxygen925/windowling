# ADR-07 세부: Lua 행동 스크립트 샌드박스 — 잠정

- 상태: **잠정** (2026-10-13). 기준선 ADR-07("Lua는 행동 스크립트, 네이티브 플러그인은 제한적으로", 세부 정책 추후)의 P2 세부안. P2-R2~R4 는 사용자 확인 전 추천안
- 관련: [P2 계획 §2.2](../plan/desktop_pet_p2_detailed_plan_ko.md), `src/script/lua_behavior.*`

## 결정

- 런타임: vcpkg 기준선의 **Lua 5.5.0** (port 5.5.0#1). 바인딩 라이브러리 없이 C API 를 직접 쓴다 (P2-R2).
- 실행 단위: **펫 인스턴스마다 Lua 상태 1개** (P2-R3).
  - 같은 캐릭터 펫끼리도 전역 변수를 공유하지 않는다.
- 훅은 둘뿐이다. 반환값은 `(activity, duration[, dir])` 이고, `nil` 을 돌려주면 기본 FSM 이 결정한다.
  - `on_rest(ctx)`
  - `on_event(name, ctx)`. `name` 은 click·landed·drag_start·drag_end·cursor_near 중 하나
  - `ctx` 는 `{now, mode, last}` 를 담는다.
  - 결과는 C++ 쪽에서 다시 검증한다. activity 는 idle/walk/sit/look/jump/climb (jump·climb 은 2026-10-20 추가, ADR-13), duration 은 0.1~60초, walk 일 때 dir ±1, jump 일 때 -1/0/+1. 검증을 통과하지 못하면 버린다 (`rejected` 카운트).
- 샌드박스 (P2-R4):
  - 텍스트 청크만 로드한다 (`"t"`). 바이트코드는 거부한다.
  - 허용 라이브러리: `base`·`table`·`string`·`math`.
  - 제거: `io`·`os`·`package`·`debug`·`load`·`loadfile`·`dofile`·`require`·`string.dump`·`collectgarbage`.
  - 호출 1회당 명령 **100만 개**. 1000개마다 count hook 으로 확인한다. 최상위 청크 실행에도 같은 제한을 건다.
  - 상태당 메모리 **4MB**. 사용자 할당자로 제한하고, 넘으면 할당이 실패한다.
  - 오류가 **3회 연속**이면 그 펫의 스크립트를 끄고 기본 FSM 으로 돌아간다. 앱과 다른 펫은 영향을 받지 않는다.
  - 로그: `[pet{}] lua error count=.. last=..`, `[pet{}] lua disabled`. 로드에 실패하면 `Lua 로드 실패 → 기본 행동`.
- 스레드: UI 스레드에서 동기 호출한다. 훅은 행동을 결정하거나 이벤트가 있을 때만 불리므로 프레임마다 호출되지 않는다.

## 근거 (확인한 것)

- 단위 테스트:
  - 금지 함수가 `nil` 이다.
  - 무한 루프는 명령 수 제한으로 중단된다.
  - 메모리 폭주는 할당 실패로 중단된다.
  - 로드 오류와 바이트코드를 거부한다.
  - 동봉한 `test-blue/custom.lua` 가 FSM 에 반영된다.
- 시나리오:
  - S3a: 파랑 캐릭터를 클릭하면 Lua 가 sit 2초를 정하고, 주황(Lua 없음)은 기본 look 을 한다.
  - S4: 최상위 무한 루프 → 로드 실패, 기본 행동으로 실행했다.
  - S4: 훅 안 무한 루프 → `instruction limit exceeded` 3회 → 비활성화 → 기본 FSM 으로 계속 동작했다.

## 미확정·미검증

- 명령 100만 개의 실제 시간 상한. 이 PC 기준 수 ms 수준으로 추정하며 측정하지 않았다.
- 4MB 상한이 복잡한 스크립트에 충분한지 (테스트 스크립트는 수십 KB)
- 반복 할당 폭주는 단위 테스트로 확인했다(512KB 제한). 명령 1개로 큰 메모리를 한 번에 요구하는 패턴(예: `string.rep('x', 1e9)`)과 그 시간 측면의 최악 사례는 확인하지 않았다(할당자 거부로 막힐 것으로 예상).
- 스크립트 API 확장(창 정보 읽기, 타이머 등)과 버전 관리: P3 이후
