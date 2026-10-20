# Windows Desktop Pet — P2 상세 계획 및 작업 계획서 (캐릭터 시스템)

**버전:** 0.2 (구현·검증 결과 반영)  
**작성일:** 2026-10-13  
**기준 문서:** 기준선 v1.3 §6(캐릭터 패키지 규격)·§10(P2), [P1 계획](desktop_pet_p1_detailed_plan_ko.md), [P1 보고서](../p1_report.md)  
**문서 상태:** 사용자가 "필요/불필요 조사 → 작업 계획서 → 테스트 포함 구현"을 위임(2026-10-13). 기준선에 근거가 없는 선택은 **추천 (미확정)** 으로 표시하고, 구현은 그 추천안 기본값으로 진행한다. `[검증]`은 미확인 동작.  
**진행 상태:** P2-01~P2-10 구현·자동 검증 완료, 수동 확인과 P2-R1~R8 사용자 확인 대기 → [P2 보고서](../p2_report.md)

> P2 목표(기준선 §10): **패키지 로더, FSM, Lua, 3~5마리, 리소스 공유.** 완료 판정: **코드 수정 없이 캐릭터 교체 및 다중 실행 가능.**

---

## 1. 현황 조사 — P2에 필요한 것 / 필요 없는 것

### 1.1 현재 코드(P1)에서 캐릭터가 코드에 박혀 있는 지점

| 위치 | 현재 | P2에서 바꿀 것 |
|---|---|---|
| `PetApp::Run` | `assets/test/pet_sheet.png` 고정 경로, 64px 프레임 고정 | 패키지 로더가 찾은 캐릭터의 `render/sprite/atlas.png`·`animations.toml` |
| `pet_app.cpp` `enum Frame` | 프레임 번호(걷기 0~3, look 4 …) 상수 | 패키지의 애니메이션 이름 → 프레임 목록 |
| `PetApp::UpdateBehavior` | 대기 45%·걷기 40%·앉기 15%, 지속 시간 상수 | `behavior.toml` FSM 파라미터 + Lua 훅 |
| `kBaseSize = 128`, `kWalkFrameSec` | 상수 | 패키지 `manifest`/`animations` 값 |
| `PhysicsParams` | 설정 전역값 | 캐릭터별 속도·발 지지 폭 (`behavior.toml`) |
| `SpriteRenderer` 1개 | 시트 1장 전역 | 캐릭터별 텍스처를 ResourceManager가 공유 |
| `--pets=N` | 같은 그림 N마리 (개발 옵션) | 설정의 `[[pets]]` 목록 (캐릭터·마릿수), 트레이에서 추가·제거·교체 |

### 1.2 P2에 필요한 것 (구현 대상)

| # | 항목 | 근거 |
|---|---|---|
| N1 | 패키지 폴더 규격: `manifest.toml`, `render/sprite/atlas.png`, `render/sprite/animations.toml`, `behaviors/behavior.toml`, `behaviors/custom.lua`(선택) | 기준선 §6.1 |
| N2 | 로더 검증: 스키마 버전, 필수 파일·필드, **경로 정규화(패키지 밖 참조 금지)**, 파일·이미지 크기 제한, 라이선스·출처 필드 필수 | 기준선 §6.2, P0 계획 §8 #3 후속 |
| N3 | 실패 처리: 검증 실패 패키지는 **로그만 남기고 로드하지 않음**, 남은 캐릭터로 실행, 0개여도 트레이 상주 | D12·D13 |
| N4 | 캐릭터 패키지(읽기 전용 에셋)와 인스턴스 상태(위치 등) 분리 | 기준선 §6.2, §6.3, ADR-04 |
| N5 | ResourceManager: 같은 패키지는 텍스처·CPU 마스크를 1번만 로드해 공유 | 기준선 §6.3, ADR-04 |
| N6 | FSM: 공통 행동 `idle/walk/sit/fall/drag` + 클릭·근접 반응, 패키지 파라미터로 가중치·시간 조정 | 기준선 §5.1, §6.2 |
| N7 | Lua 행동 스크립트: 기본 행동을 재정의(다음 행동 결정, 이벤트 훅). **권한 제한(샌드박스), 실행 시간·메모리 제한** | 기준선 §6.2, ADR-07, §11.1 "악성 Lua" |
| N8 | 다중 캐릭터 3~5마리, 서로 다른 캐릭터 동시 실행 | 기준선 §10 |
| N9 | 설정: `[[pets]]` 목록(캐릭터 id, 마릿수), 캐릭터 검색 경로(앱 내장 + `%LOCALAPPDATA%\DesktopPet\characters`) | 기준선 §6.2 |
| N10 | 트레이: 펫 추가·제거, 캐릭터 교체, 패키지 다시 읽기 | "코드 수정 없이 캐릭터 교체" |
| N11 | 테스트: 패키지 검증(정상·누락·경로 이탈·큰 이미지·악성 Lua), FSM, Lua 샌드박스, 리소스 공유, 다중 실행 시나리오, 1/3/5마리 성능 | 기준선 §11.1 "패키지"·"렌더링" 행, §8.2 |
| N12 | 두 번째 테스트 캐릭터(색·애니메이션이 다른 CC0 패키지) — 교체 검증용 | 완료 판정 |

### 1.3 P2에 필요 없는 것 (범위 밖, 이유)

| 항목 | 이유 / 시점 |
|---|---|
| `persona.md`, `ai/rules.toml`, `voice/voice.toml` 해석 | AI·TTS는 P3·P4. P2는 **존재해도 무시**(경고 없음), 경로 검증만 |
| Live2D·3D 렌더러 (`render/live2d`, `render/3d`) | P5 이후. `manifest`에서 지원 렌더러 목록만 읽고 `sprite`가 없으면 로드 실패 |
| 캐릭터 간 충돌·협동 | 기준선 §14: P2 이후 옵션, 비활성으로 시작 |
| 네이티브 플러그인 | P6 이후 (ADR-07 결정 #7) |
| 말풍선·대화 | P3 (창 구조는 P1-D6로 결정됨) |
| 패키지 핫 리로드(파일 감시) | 필요 없음 — 트레이 "패키지 다시 읽기"로 대체 (추천, 미확정) |
| 공개 배포용 기본 캐릭터팩 | D3: 배포에는 테스트 에셋만 |
| 다중 모니터 실환경 | D10 연기 |

### 1.4 판단이 필요한 항목 — 추천안으로 진행 (모두 **미확정**)

| # | 항목 | 추천안 (구현 기본값) | 대안 |
|---|---|---|---|
| P2-R1 | 패키지 형식 | **폴더만 지원.** zip 패키지는 배포 단계(P6)에서 검토 | zip 동시 지원(압축 해제 경로 검증 필요) |
| P2-R2 | Lua 버전·바인딩 | vcpkg 기준선의 **Lua 5.5.0**, **C API 직접 사용**(추가 바인딩 라이브러리 없음) | sol2 등 바인딩 라이브러리 추가 |
| P2-R3 | Lua 실행 단위 | **펫 인스턴스마다 Lua 상태 1개**(격리·상태 분리, 1개 약 수십 KB) | 패키지당 1개 공유 |
| P2-R4 | Lua 제한 | 호출 1회당 명령 **100만 개**, 상태당 메모리 **4MB**, 허용 라이브러리 `base`(일부 제외)·`table`·`string`·`math`. `io`·`os`·`package`·`debug`·`load`·`dofile`·`require` 제거 | 값 조정 |
| P2-R5 | 등반(`climb`) | **P2 범위 밖** — `behavior.toml`에 속성 자리만 두고 동작은 미구현 | P2에서 구현 |
| P2-R6 | 최대 마릿수 | 설정 합계 **8마리** 상한(P2 목표 3~5마리 측정) | 5마리 상한 |
| P2-R7 | 크기 제한 | 아틀라스 ≤ 4096×4096, 파일 ≤ 16MB, 프레임 크기 16~512px, Lua 파일 ≤ 256KB | 값 조정 |
| P2-R8 | 캐릭터 검색 경로 | `<exe>\characters\*` (테스트 캐릭터 2종 동봉) + `%LOCALAPPDATA%\DesktopPet\characters\*`. 같은 id면 사용자 폴더 우선 | 사용자 폴더만 |

---

## 2. 설계

### 2.1 패키지 규격 (schema_version = 1)

```text
characters/<id>/
  manifest.toml
  render/sprite/atlas.png          # 가로 배열 프레임 시트
  render/sprite/animations.toml
  behaviors/behavior.toml          # 선택 (없으면 기본 FSM 값)
  behaviors/custom.lua             # 선택
  persona.md, ai/, voice/          # P2에서는 무시
```

```toml
# manifest.toml
schema_version = 1
id = "test-orange"            # 영문 소문자·숫자·하이픈, 3~40자
name = "Test Orange"
version = "1.0.0"
renderers = ["sprite"]
[license]                     # 필수 (배포 애셋 라이선스·출처 기록, 기준선 §6.2)
name = "CC0-1.0"
author = "Desktop Pet project"
source = "tools/assets/gen_test_sprite.ps1"
[sprite]
atlas = "render/sprite/atlas.png"
animations = "render/sprite/animations.toml"
base_size = 128               # 100% 배율 표시 크기(px)
```

```toml
# animations.toml
frame_width = 64
frame_height = 64
[animations]                  # 공통 행동 이름 → 프레임 목록. 없는 행동은 idle 로 대체 (기준선 §5.1)
idle = { frames = [0], fps = 1 }
walk = { frames = [0, 1, 2, 3], fps = 8 }
sit  = { frames = [6], fps = 1 }
fall = { frames = [7], fps = 1 }
drag = { frames = [5], fps = 1 }
look = { frames = [4], fps = 1 }
```

```toml
# behavior.toml
walk_speed = 70.0             # px/s @96DPI
foot_support_ratio = 0.5
climb = false                 # P2-R5: 예약 (미구현)
[activities]                  # 쉬는 중 다음 행동 선택 가중치·지속 시간(초)
idle = { weight = 45, min = 3, max = 7 }
walk = { weight = 40, min = 2, max = 6 }
sit  = { weight = 15, min = 5, max = 10 }
[script]
file = "behaviors/custom.lua"  # 선택
```

### 2.2 Lua 훅 (기본 행동 재정의, P2-R2~R4)

```lua
-- 모두 선택. 정의하지 않으면 behavior.toml 가중치 기반 기본 FSM 사용
function on_rest(ctx)        -- 쉬는 상태에서 다음 행동 결정
  -- ctx: { now, mode = "grounded"|"attached", last = "idle"|..., rand = function() }
  return "walk", 3.0, -1     -- 행동 이름, 지속 시간(초), 방향(-1/1, walk 만)
end
function on_event(name, ctx) -- "click" | "landed" | "drag_start" | "drag_end" | "cursor_near"
  return "look", 1.0         -- 선택: 반응 행동과 시간 (nil 이면 기본 처리)
end
```

- 반환값 검증: 알 수 없는 행동 이름·범위 밖 시간(0.1~60초)은 무시하고 기본 FSM으로(기준선 §7.3과 같은 "검증 후 실행" 원칙).
- 오류·제한 초과 시: 그 호출은 기본 FSM으로 대체, 3회 연속 오류면 그 인스턴스의 스크립트 비활성(로그).

### 2.3 모듈 배치

| 모듈 | 추가 내용 | Windows 의존 |
|---|---|---|
| `dp_core` | `character/package`(파싱·검증·경로 정규화), `character/animation`(이름→프레임·재생), `behavior/fsm`(행동 기계, 훅 인터페이스) | 없음 |
| `dp_script` (신규) | Lua 샌드박스 호스트 — `fsm` 훅 인터페이스 구현 | 없음 (Lua는 C) |
| `dp_platform_win` | 기존 `SpriteSheet`(WIC) | 있음 |
| `apps/desktop-pet` | `CharacterLibrary`(검색·로드·공유), `PetManager`(인스턴스 생성·제거·교체), 기존 루프에 연결 | 있음 |

리소스 공유: `CharacterAsset`(패키지 정의 + `SpriteSheet` + `SpriteRenderer`)를 id별 `shared_ptr`로 1개만 만들고, 펫 인스턴스는 참조만 가진다.

---

## 3. 작업 분해 (WBS)

| ID | 작업 | 산출물 | 완료 기준 |
|---|---|---|---|
| P2-01 | 패키지 모델·파서·검증 (`dp_core`) | `character/package.*` | 단위 테스트: 정상·필수 누락·경로 이탈(`..`, 절대 경로)·id 형식·스키마 버전·크기 제한 |
| P2-02 | 애니메이션 정의·재생기 | `character/animation.*` | 단위 테스트: 누락 행동 → idle 대체, fps 진행, 범위 밖 프레임 거부 |
| P2-03 | FSM (`dp_core`) | `behavior/fsm.*` | 단위 테스트: 가중치 선택, 지속 시간, 이벤트 반응, 훅 결과 검증 |
| P2-04 | Lua 샌드박스 (`dp_script`), vcpkg `lua` 추가·버전 고정 | `script/lua_behavior.*` | 단위 테스트: 훅 호출, 금지 함수 없음, 무한 루프 중단, 메모리 폭주 중단, 오류 3회 비활성 |
| P2-05 | 테스트 캐릭터 2종 패키지화 (주황·파랑, CC0) | `characters/test-orange`, `characters/test-blue` | 로더 검증 통과 |
| P2-06 | CharacterLibrary·ResourceManager (앱) | 검색·로드·공유 | 같은 캐릭터 N마리에서 텍스처 1개(로그), 실패 패키지 건너뜀 |
| P2-07 | PetManager·설정 `[[pets]]`·트레이 (추가·제거·교체·다시 읽기) | 앱 | 설정만 바꿔 캐릭터 교체, 트레이로 런타임 추가·교체 |
| P2-08 | 시나리오 테스트 | `tests/scenario/p2_scenarios.ps1` | 5마리 2종 동시 실행, 교체, 잘못된 패키지 건너뛰기, 악성 Lua 차단, P1 시나리오 12개 회귀 통과 |
| P2-09 | 성능 1/3/5마리 | `tools/measure/p2_pets.ps1` | 증가량 기록(기준선 §8.2), 정지 Present 0 유지 |
| P2-10 | 결과 정리 | `docs/p2_report.md`, ADR | — |

---

## 4. 검증 계획

- 단위 테스트(Catch2): P2-01~04 항목 + 기존 12개 회귀.
- 시나리오(자동): 앱 로그 판정. 대상 창·마우스 조작은 P1 하네스 재사용.
- 성능: P1 비회귀 방식(60초 × 3회), 1/3/5마리 정지·걷기. CPU 판정 규칙 D15.
- 수동: 체감(다른 캐릭터 동작 차이), 트레이 메뉴 사용성.

---

## 5. 구현 중 바뀐 점 (v0.2)

- 패키지 검색: 폴더 이름이 아니라 **id 기준**으로 우선순위를 정한다. 뒤 경로가 우선하고, 뒤 패키지가 깨져 있으면 앞의 같은 id 가 남는다. 시나리오 S1c·S4-override 에서 발견해 고쳤다.
- `--no-behavior`: 자율 결정만 막는다. 클릭 등 이벤트 반응(짧은 sit/look)은 그대로 둔다(걷기는 막음). Lua 클릭 반응을 시나리오로 확인하기 위해서다.
- 걷기 창 이동 30Hz 제한: 펫마다 따로 두던 주기를 **공통 틱**으로 바꿨다. 여러 펫의 이동을 `DeferWindowPos` 한 번으로 묶는다.
- 걷기 방향: 새 결정이 나올 때만 FSM 값을 적용한다. 가장자리에서 방향을 바꾸는 것은 물리가 한다.
- 테스트용 명령행 추가: `--character=id`, `--characters-dir=path`, `--config=path`, `--spawn-step=DX`. 시나리오가 사용자 `config.toml` 을 건드리지 않게 하기 위해서다.
- 설정의 `physics.walk_speed`·`world.foot_support_ratio` 는 캐릭터 패키지 값이 우선한다. P2 에서는 사실상 쓰이지 않는 키다.
