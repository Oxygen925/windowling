# Windows Desktop Pet — P5 상세 계획 (3D 확장 검증)

**버전:** 0.2 (2026-10-19 사용자 결정 반영. **P5 완료** 2026-10-19 → [P5 보고서](../p5_report.md))  
**작성일:** 2026-10-19  
**기준 문서:** 기준선 v1.3 §2(ADR-03)·§4.2·§5.1·§5.3·§10(P5)·§14, [ADR-02](../adr/ADR-02-rendering-path-update.md), [ADR-09](../adr/ADR-09-hit-test.md), [DComp 아틀라스 실험](../exp/dcomp_atlas_report_ko.md)  
**2026-10-19 사용자 결정**
- 테스트 모델: **Khronos Fox** (모델 CC0 + 리깅·애니메이션 CC-BY-4.0, 개발용).
- 3D hit-test: **GPU 알파 readback → Region** (최대 10Hz).
- 갱신률·시점: **모두 30fps(`display.frame_rate`), 3/4 시점** (idle 15fps 상한 안은 쓰지 않음).
- 나머지는 추천안으로 구현 (아래 추천안은 계속 미확정).

**표기**
- **추천 (미확정)**: 기준선에 근거가 없는 선택. 사용자 확인 전까지 확정이 아니다.
- `[검증]`: 문서나 추론에 근거했지만 실제로 확인하지 않은 동작.

> **P5 목표 (기준선 §10):** glTF 테스트, 직교 카메라, 공통 행동/충돌 연결.
> **완료 판정:** 기존 물리/행동 코어 변경 없이 테스트 3D 동작.
> - 측정 가능한 판정: `src/core/` 의 물리·월드·행동(`physics.*`, `world.*`, `behavior/`) 파일이 P5 동안 바뀌지 않는다 (`git diff` 0줄). 패키지 규격(`character/package.*`)에는 3D 항목 추가만 허용한다.

---

## 1. 현황 조사 (2026-10-19)

### 1.1 기준선이 정한 것

| 항목 | 내용 | 출처 |
|---|---|---|
| ADR-03 | 물리는 2D, 표시만 2D/3D(2.5D). 3D 캐릭터는 `foot_anchor` 를 화면 2D 좌표에 투영해 같은 물리를 쓴다 | §2, §4.2 |
| 카메라 | 직교 카메라 기본. 발의 화면 좌표와 2D 충돌체 일치 | §5.3 |
| 순서 | glTF 2.0 → VRM 1.0 (MToon·Expression·LookAt·Spring Bone 은 VRM 단계) | §5.3, §14 |
| 공통 행동 | idle·walk·sit·climb·fall·drag·talk. 미지원 애니메이션은 기본 동작으로 대체 | §5.1 |
| 자원 | 쓰지 않는 모듈(3D)은 초기화하지 않음. 정적 캐릭터는 렌더 중단 | §8.1 |
| 입력 | 투명 픽셀이 자동으로 클릭 통과된다고 가정하지 않는다 | §5.1 |

### 1.2 이미 확인된 것

| 항목 | 내용 | 출처 |
|---|---|---|
| 3D 출력 경로 | P0 에서 깊이 버퍼·직교 투영·반투명 면 큐브를 같은 D3D11 + DComp 경로로 출력 확인 | P0-10, ADR-02 |
| 렌더러 | 스프라이트: 펫마다 DComp composition swapchain (premultiplied). 프레임이 바뀔 때만 Present | `RenderAndMove` |
| hit-test | 방식 B (`SetWindowRgn`, 프레임마다 정확한 Region). 비용 대부분은 `SetWindowRgn` 자체 (M1) | ADR-09, B8 결정 |
| ADR-09 메모 | "3D 는 매 프레임 마스크 생성(GPU readback 또는 근사 형상)이 필요 → P5 에서 재평가" | ADR-09 |
| DComp 아틀라스 | 2D 에서는 명확히 유리하지 않아 보류. "3D 처럼 그리기 비용이 큰 렌더러가 생기면 다시 본다" | 실험 보고서 |

### 1.3 라이브러리 (vcpkg 고정 baseline `9e593bb` 에서 확인)

| 후보 | 버전 | 라이선스 | 의존성 | 비고 |
|---|---|---|---|---|
| **cgltf** | 1.15 | MIT | 없음 | C 단일 헤더. glb·embedded 이미지·animation·skin 해석. 이미지 디코딩은 하지 않음 → 기존 WIC 사용 |
| fastgltf | 0.9.0 | MIT | simdjson (Apache-2.0 OR MIT) | 빠르지만 의존성 추가. 테스트 모델 크기에서는 이점이 작음 |
| tinygltf | 3.0.0 | (port 표기 없음, 원본 MIT) | nlohmann-json, stb | stb 를 새로 들임 |

- 추천 (미확정): **cgltf** — 의존성 0, 우리 코드가 JSON·이미지 디코딩을 이미 가지고 있다 (P5-R1).
- 수학: DirectXMath (Windows SDK 포함, 추가 의존성 없음).

### 1.4 테스트 모델 후보 (Khronos glTF-Sample-Assets `metadata.json` 확인)

| 모델 | 라이선스 | 내용 | 비고 |
|---|---|---|---|
| **Fox** | 모델 CC0-1.0 (PixelMannen) + 리깅·애니메이션 CC-BY-4.0 (tomkranis, AsoboStudio·scurest) | 4족 여우, 애니메이션 Survey·Walk·Run, 스킨·텍스처 | 귀여운 펫에 가깝다. 출처 표기 필요. sit 없음 |
| CesiumMan | CC-BY-4.0 + Cesium 상표(로고 텍스처) | 사람형, Walk 1개 | 상표가 들어 있어 배포에 부적합 |
| RiggedFigure / RiggedSimple | CC-BY-4.0 (Cesium) | 단순 리그 | 시각적으로 펫답지 않음 |
| 직접 생성 | CC0 (우리) | 스크립트로 만든 단순 캐릭터(캡슐 몸·머리, 뼈 3~5개, idle·walk·sit) | 품질은 낮지만 라이선스 문제 없음, 공통 행동 이름을 모두 가질 수 있음 |

- 테스트 모델은 P2 결정대로 **개발용으로만** 저장소에 두고 일반 배포판에는 넣지 않는다.

### 1.5 P5 에서 하지 않는 것

| 항목 | 이유 |
|---|---|
| VRM 1.0, MToon, Expression, LookAt, Spring Bone | 기준선 순서상 glTF 다음 단계 |
| PBR 전체(금속성·거칠기·IBL), 그림자, 후처리 | 펫 표시에 과함. 자원 목표와 충돌 |
| Live2D | SDK 라이선스 별도 검토 (기준선 §5.3) |
| 립싱크·표정 | VRM 단계 (P4 TTS 는 단어 시각만 확보) |
| 3D 물리(충돌·IK) | ADR-03: 물리는 2D |
| morph target (blend shape) | 테스트 모델에 불필요. 로더가 만나면 경고 후 무시 `[검증]` |

---

## 2. 설계

### 2.1 구조

```text
CharacterPackage (renderers = ["gltf"] + [model])        ← 패키지 규격에 3D 항목 추가만
 └─ CharacterAsset: Sprite(기존) | Model3D(새로)          ← 앱 계층
      Model3D = cgltf 로드 → 메시·스킨·애니메이션(CPU) + GPU 버퍼·텍스처(공유)
PetActor: 같은 물리(PetBody)·행동(FSM/Lua)·AnimationPlayer 이름
 └─ 렌더: Sprite → 기존 SpriteRenderer / Model3D → ModelRenderer(펫마다 포즈만)
```

- **물리·행동은 그대로:** 펫의 `body.width/height`·`foot` 는 지금과 같은 의미다. 3D 모델은 이 사각형 안에 직교 투영으로 그린다.
- **행동 → 애니메이션:** `ActionFor()` 가 주는 공통 행동 이름을 manifest 의 매핑으로 glTF 애니메이션에 연결한다. 없으면 `idle`, 그것도 없으면 바인드 포즈.
- **방향:** 스프라이트의 좌우 반전 대신 모델을 좌우로 돌린다 (기본 3/4 시점, 왼쪽을 볼 때 yaw 반전) (P5-R4).

### 2.2 패키지 규격 (schema_version 1 에 선택 항목 추가)

```toml
renderers = ["gltf"]

[model]
path = "render/model/fox.glb"   # .glb 만 (외부 파일 참조 없음 → 경로 이탈 검사 단순)
base_height = 128               # 100% 배율 화면 높이 (px). 너비는 바인드 포즈 비율
yaw_deg = 35                    # 기본 3/4 시점
foot = "bounds_bottom"          # 발 기준: 바인드 포즈 바운딩 박스 바닥 중앙
[model.animations]              # 공통 행동 → glTF 애니메이션 이름
idle = "Survey"
walk = "Walk"
fall = "Run"
```

- 제한 (추천, 미확정): glb 32MB, 삼각형 50k, 관절 128, 텍스처 2048², 애니메이션 32개 (P5-R6).
- `pack-check` 가 3D 패키지도 검사한다.

### 2.3 렌더러 (D3D11)

- **스키닝:** 정점 셰이더(관절 4개/정점), 관절 행렬은 상수 버퍼. 애니메이션 샘플링(LINEAR·STEP, CUBICSPLINE 은 LINEAR 근사 `[검증]`)은 CPU.
- **셰이딩:** base color 텍스처 + 간단한 half-Lambert 1광원 (P5-R3). 알파: OPAQUE·MASK 지원, BLEND 는 정렬 없이 그림.
- **출력:** 기존 composition swapchain(premultiplied). 가장자리 품질을 위해 4x MSAA 텍스처에 그린 뒤 resolve (P5-R3). 깊이 버퍼는 P0 경로 재사용.
- **공유:** 메시·텍스처는 캐릭터마다 1번만 올리고 펫마다 포즈(관절 행렬)만 다르다 (기준선 §6.3).
- **갱신률:** 스프라이트는 프레임이 바뀔 때만 그리지만 3D 애니메이션은 매 프레임 바뀐다 → 상한을 둔다 (P5-R5).
- **Device Lost:** 기존 `RecreateGpu` 에 3D 자원을 포함한다.

### 2.4 hit-test (ADR-09 재평가)

| 방식 | 정확도 | 비용 | 비고 |
|---|---|---|---|
| **A. GPU 알파 readback → Region** | 프레임 정확 (최대 1프레임 지연) | 그릴 때마다 작은 staging 복사(128²×4B) + Region 생성. `SetWindowRgn` 비용이 큼(M1) | 다음 프레임에 Map 해서 GPU 대기 없음. Region 은 최대 10Hz·바뀔 때만 (P5-R2) |
| B. 바운딩 형상 (애니메이션별 합집합) | 투명 부분 일부가 클릭을 받음 | 거의 0 | B8 결정(정확성 우선)과 충돌 |
| C. 저해상도 실루엣 별도 렌더 | A 와 비슷 | 렌더 1회 추가 | A 대비 이점 작음 |

- 추천 (미확정): **A**, Region 갱신 최대 10Hz. 설정 `hit_region` 의 `clip`(합집합) 옵션을 3D 에도 같은 의미로 제공.

### 2.5 성능 목표 (미검증)

| 항목 | 목표 |
|---|---|
| 3D 모델 없음 | P4 와 차이 없음 (3D 모듈 미초기화) |
| 3D 펫 1마리 idle | CPU 1% 미만 (30fps 기준) `[검증]` |
| 3D 펫 3마리 walk | 측정해 스프라이트 대비 기록 |
| VRAM | 테스트 모델 1종 + 펫 3마리 20MB 이하 `[검증]` |

- 측정 결과에 따라 DComp 공유 visual 구조 재검토 여부를 판단한다 (실험 보고서 결론).

---

## 3. 작업 목록

| ID | 작업 | 산출물 | 테스트 |
|---|---|---|---|
| P5-01 | 패키지 규격 3D 항목·검증 (`[model]`, 애니메이션 매핑, 제한) | `core/character/package.*` | 단위 테스트 |
| P5-02 | glTF 로드 (cgltf): 메시·스킨·애니메이션·텍스처(WIC) | `rendering/model/*` | 단위(로드·샘플링) |
| P5-03 | 애니메이션 샘플링·포즈 계산 (Windows 비의존) | 같음 | 단위(키프레임 보간) |
| P5-04 | ModelRenderer (스키닝 셰이더, MSAA, premultiplied) | `rendering/d3d11/model_renderer.*` | 캡처 비교 |
| P5-05 | 직교 카메라·발 기준점·방향 | 같음 | 시나리오(착지 위치) |
| P5-06 | 앱 연결: CharacterAsset 종류, 렌더 분기, 갱신률 상한, Device Lost | `apps/desktop-pet` | P1·P2 회귀 |
| P5-07 | 3D hit-test (readback → Region) | 같음 | 클릭 통과 시나리오 |
| P5-08 | 테스트 3D 캐릭터 패키지 (개발용) | `characters/test-3d` | pack-check |
| P5-09 | 성능 측정 (1·3마리, idle·walk, 스프라이트 대비) | 측정 스크립트 | 1회 |
| P5-10 | 문서: 보고서·ADR(3D 렌더·hit-test)·패키지 규격 | `docs/` | — |

## 4. 테스트 계획 (필요한 만큼만)

| 범주 | 시나리오 | 기준 |
|---|---|---|
| 코어 불변 | P5 시작 커밋 대비 `src/core/physics.*`·`world.*`·`behavior/` diff | 0줄 |
| 착지 | 3D 펫이 대상 창 위에 착지 | 발 y = 창 상단 (±1px) |
| 행동 | walk·idle·fall·drag → 애니메이션 전환 로그 | 매핑대로, 없는 행동은 idle |
| 클릭 통과 | 모델 바깥 투명 부분 클릭 → 뒤 창이 받음 / 몸 클릭 → 펫이 받음 | P1 클릭 통과 시나리오 방식 |
| 복구 | GPU 재생성 강제 | 3D 펫이 다시 보임 |
| 회귀 | P1·P2 시나리오 | 통과 |

## 5. 사용자 확인이 필요한 추천안 (모두 미확정)

| # | 항목 | 추천 | 대안 |
|---|---|---|---|
| P5-R1 | glTF 로더 | cgltf | fastgltf / tinygltf |
| P5-R2 | 3D hit-test | **결정: GPU 알파 readback → Region, 최대 10Hz.** 구현 중 Region 이 보이는 부분도 잘라서 마스크를 4px(DPI 비례) 넓힘 (계획에 없던 처리, 확인 필요) | — |
| P5-R3 | 셰이딩·가장자리 | 텍스처 + half-Lambert, 4x MSAA | unlit / MSAA 없음 |
| P5-R4 | 시점·방향 | **결정: 3/4 시점**, 왼쪽을 볼 때 yaw 반전. yaw 는 패키지 값 (test-fox 60°) | — |
| P5-R5 | 3D 갱신률 | **결정: 모두 30fps** (`display.frame_rate`) | — |
| P5-R6 | 3D 패키지 제한 | glb 32MB·삼각형 50k·관절 128·텍스처 2048² | 다른 값 |
| P5-R7 | 테스트 모델 | **결정: Khronos Fox** | — |

## 6. 위험

| 위험 | 대응 |
|---|---|
| 3D 렌더 비용이 매 프레임 발생 | 갱신률 상한, 숨김·정지 시 중단, 측정 후 공유 visual 재검토 |
| `SetWindowRgn` 비용 증가 (3D 는 Region 이 자주 바뀜) | 10Hz 상한, 같은 마스크면 생략 (해시 비교) |
| 직교 투영에서 모델 크기·발 위치가 애니메이션마다 흔들림 | 바인드 포즈 기준 고정 스케일, 발 기준점 고정 |
| glTF 변형(좌표계·단위·노드 변환) | Y-up·미터 → 화면 변환 한곳에서, 테스트 모델로 확인 |
| 테스트 모델 라이선스 | 출처·라이선스를 manifest `[license]` 에 기록, 개발용으로만 |
