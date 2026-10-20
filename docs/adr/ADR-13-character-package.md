# ADR-13 캐릭터 패키지 형식 (schema_version 1) — 잠정

- 상태: **잠정** (2026-10-13). P2-R1·R6·R7·R8 은 사용자 확인 전 추천안
- 관련: 기준선 §6.2·§6.3, ADR-04(에셋과 인스턴스 분리), [P2 계획 §2.1](../plan/desktop_pet_p2_detailed_plan_ko.md), [P2 보고서](../p2_report.md)

## 결정

- 형식: **폴더 패키지만** 지원 (P2-R1). zip 은 배포 단계에서 다시 검토.
  ```
  <id>/manifest.toml
       render/sprite/atlas.png, animations.toml
       behaviors/behavior.toml (선택), custom.lua (선택)
  ```
- `manifest.toml` 필수 항목:
  - `schema_version = 1`
  - `id`: `[a-z0-9-]{3,40}`
  - `name`, `version`
  - `[license] name·author`: 배포 에셋의 출처를 기록 (D11)
  - `renderers` 에 `"sprite"`
  - `[sprite] atlas(.png)·animations`
  - `idle` 클립
  - 그 밖의 키(persona·ai·voice 등)는 읽지 않고 무시한다. 경고도 남기지 않는다. `behavior.toml` 의 알 수 없는 activity 만 경고한다.
- 경로: 모든 상대 경로는 패키지 폴더 안으로만 해석한다.
  - `..` 이나 절대 경로가 폴더 밖을 가리키면 거부한다 (`ResolveInside`).
- 크기 제한 (P2-R7):
  - 아틀라스 ≤ 4096×4096 이고 파일 ≤ 16MB
  - 프레임 16~512px
  - Lua 파일 ≤ 256KB
  - 디코딩한 아틀라스 크기와 애니메이션 프레임 범위가 맞는지 다시 확인한다.
- 실패: 그 패키지만 거부하고 로그를 남긴다 (`[library] rejected dir=.. errors=N` + 사유 목록). 앱은 계속 실행한다 (D12).
  - 펫이 0마리가 되어도 트레이에 상주한다 (D13).
- 검색 경로 (P2-R8), 뒤에 오는 경로가 우선:
  1. `<exe>\characters`
  2. 설정 파일 옆 `characters` (기본값 `%LOCALAPPDATA%\DesktopPet\characters`)
  3. `--characters-dir` (테스트용)
  - 같은 id 는 뒤 경로가 이긴다. 뒤 패키지가 깨져 있으면 앞의 같은 id 패키지가 남는다.
- 공유: 패키지 1개 = `CharacterAsset`(파싱 결과 + CPU 사본 + GPU 텍스처)을 `shared_ptr` 로 펫들이 공유한다.
  - 펫마다 따로 갖는 것은 위치·물리·FSM·Lua 상태·창이다 (ADR-04).
- 펫 구성: `config.toml` 의 `[[pets]] character·count`, 합계 상한 8마리 (P2-R6).
  - 트레이에서 런타임에 추가·교체·제거·다시 읽기를 할 수 있다. 이 변경은 저장하지 않는다.

## 근거 (확인한 것)

- 시나리오 S1~S6 은 23/23 통과 ([P2 보고서 §3](../p2_report.md)):
  - 5마리 2종을 동시에 실행했다.
  - 같은 캐릭터의 에셋 참조 수가 1 + 펫 수였다.
  - 사용자 폴더 재정의, 깨진 재정의일 때의 폴백을 확인했다.
  - 경로 이탈, 라이선스 누락, 큰 Lua 파일을 거부했다.
  - 펫 0마리일 때 트레이에 상주했다.
- 단위 테스트: 필수 항목 누락, 경로 이탈(`..`·절대 경로), id 형식, 스키마 버전, 크기 제한, 실제 동봉 캐릭터 2종 로드.

## 미확정·미검증

- zip 패키지, 패키지 서명, 파일 감시 자동 다시 읽기 (P2 범위 밖)
- 4096px 근처 큰 아틀라스의 메모리·로드 시간 (테스트 캐릭터는 512×64)
- 펫마다 다른 크기의 캐릭터를 섞었을 때 `petHeightForWorld_`(가장 큰 펫 높이)로 표면을 계산하는 방식의 영향

## 추가 (2026-10-19, P5: 3D 패키지)

```toml
renderers = ["gltf"]
[model]
path = "render/model/fox.glb"   # .glb 만 (외부 버퍼·이미지 파일 거부)
base_height = 80                # 100% 배율 화면 높이, 너비는 모델 비율
yaw_deg = 60                    # 3/4 시점 (-90~90), 왼쪽을 볼 때 반전
[model.animations]              # 공통 행동 → glTF 애니메이션 (없으면 idle, 그것도 없으면 기본 자세)
idle = "Survey"
walk = "Walk"
```

- 제한(추천, 미확정): glb 32MB, 삼각형 50k, 관절 128, 애니메이션 32, 이미지 8, 텍스처 2048².
- `[sprite]` 는 3D 패키지에서 선택. 둘 다 있으면 모델을 쓴다.
- 물리·행동은 스프라이트와 같다 (ADR-03). `behavior.toml`·`persona.md`·`[voice]` 도 같다.
