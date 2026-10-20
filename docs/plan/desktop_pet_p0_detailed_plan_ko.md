# Windows Desktop Pet — P0 상세 실행 계획 (+ P1 개요)

**버전:** 0.5  
**작성일:** 2026-10-13  
**기준 문서:** `desktop_pet_development_plan_v1_3_ko.md` (이하 "기준선")  
**문서 상태:** 초안 / §0.5에 기록된 항목은 사용자 승인 완료, 그 외 추천안은 **미확정 — 사용자 승인 필요**

> 이 문서는 기준선 §10(P0·P1), §13(첫 개발 사이클), §14(추가 결정 사항)를 실행 가능한 수준으로 분해한다. 기준선에서 "검증 필요"로 표시된 기술 사항은 이 문서에서도 **가설**로 취급하며, 아래의 API 동작 설명 중 `[검증]` 표시가 붙은 항목은 P0 실험으로 확인하기 전까지 사실로 간주하지 않는다. 기준선과 달라진 결정은 §0.5에 명시한다.

---

## 0. 목적 및 범위

### 0.1 P0의 목적

P0는 "제품 기능 구현"이 아니라 **설계를 고정하기 위한 측정·검증 단계**다. P0 종료 시 다음 4가지 질문에 데이터로 답하고, 그 결과를 ADR로 확정한다.

| ID | P0 판정 질문 | 결정되는 것 |
|---|---|---|
| Q1 | 투명 영역 클릭 통과를 어떤 방식으로 구현할 것인가 | Hit-test 전략 (ADR-09 신규) |
| Q2 | 캐릭터별 소형 창 vs 단일 대형 오버레이 중 무엇을 쓸 것인가 | 오버레이 창 전략 (기준선 §14 1행) |
| Q3 | D3D11 + DComp 경로의 CPU·RAM·VRAM·DWM 비용이 목표 범위 안인가 | ADR-02 확정 또는 대안 경로 |
| Q4 | 동일 합성 경로에 3D(깊이 버퍼 포함)를 투명하게 출력할 수 있는가 | 3D 확장 경로 유효성 (P5 전제) |

### 0.2 P0 범위

- 최소 Win32 실행 파일, 트레이 아이콘, 종료 메뉴
- D3D11 Device + DComp 합성 투명 오버레이, 테스트 스프라이트 1~5개 출력
- 클릭 통과(hit-test) 후보 방식 비교 구현
- Per-Monitor DPI V2 검증 — **단일 모니터 범위만**(D10, §3.5)
- `SetWinEventHook` 기반 **단일 창 추종 spike** (§0.4)
- 성능 측정 하네스 및 측정 로그 (시나리오별 3회 이상 반복)
- 3D 테스트 도형(큐브) 투명 출력
- D2D/DirectWrite 텍스트 1줄 출력 — 동일 Device interop 확인

### 0.3 P0 비범위

- 2D 물리 본 구현, 캐릭터 패키지 로더, FSM/Lua, 다중 캐릭터 관리 로직
- AI, 화면 캡처, TTS, 설정 UI, 전역 단축키
- 다중 모니터·혼합 DPI·음수 좌표·모니터 연결/해제 검증 (D10, P1 이후 또는 다른 환경)
- 테스트 코드 품질·아키텍처 완성도 (P0 코드는 **폐기 가능 spike**를 원칙으로 하되, 측정 하네스와 렌더링 초기화 코드는 재사용을 고려)

### 0.4 P0 / P1 경계 (확정)

- **P0:** `SetWinEventHook` 기반 **단일 창 추종 spike**. 물리 없음, 즉시 스냅.
- **P1:** WindowObserver / WorldManager 정식 창 관리 및 2D 물리 본 구현.

기준선 §13 5번(P0 항목)과 §10(창 감시 = P1) 사이의 표현 차이는 위 해석으로 정리한다.

### 0.5 결정 기록 (2026-10-13 사용자 승인)

| # | 항목 | 결정 | 기준선 대비 |
|---|---|---|---|
| D1 | P0/P1 경계 | §0.4 해석 승인 | 해석 명확화 |
| D2 | 공식 기본 캐릭터팩 라이선스 | **CC BY 4.0 (조건부 승인)**. 서드파티 캐릭터팩은 개별 라이선스 허용 | 기준선 §14 3행 구체화 |
| D3 | 배포 구성 | **배포물에 기본 캐릭터팩을 포함하지 않는다. 테스트 에셋만 첨부** | 기준선 §14 3행 추천("독자 제작 애셋을 기본 배포")과 **다름** |
| D4 | AI 대화 이력 | 최근 **20턴** 유지. **디스크 저장 기본 OFF**, 사용자가 활성화한 경우 **7일 보관** | 기준선 §14 5행 구체화 |
| D5 | 코드 라이선스 | **Apache-2.0** (P6 이전 LICENSE 파일 작성 시 최종 반영) | 기준선 §14 9행 확정 |
| D6 | 성능 임계값 | CPU·RAM 수치 유지. Present 0회는 **상태 변화가 없는 정지 구간**에만 적용. Present mode 변화는 **실제 성능 영향과 함께** 판정. 빨강은 **자동 개발 중단이 아닌 설계 재검토 조건** | §4.3 반영 |
| D7 | D3D11 Device 플래그 | `D3D11_CREATE_DEVICE_BGRA_SUPPORT` **초기부터 활성화** | §3.2 반영 |
| D8 | 측정 반복 | 성능 측정은 가능하면 **시나리오별 3회 이상 반복** | §4.2 반영 |
| D9 | 측정 PC / 테스트 앱 | i5-13400F, RTX 3060 12GB, QHD 모니터 1대(Windows 배율 100%). 전체화면 테스트 앱: Mindustry | §1, §4, §5 반영 |
| D10 | 다중 모니터 검증 | **P0에서는 단일 모니터 범위만 검증**. 혼합 DPI·음수 좌표·모니터 연결/해제는 P1 이후 또는 다른 환경으로 연기 | 기준선 §10 P0 완료 기준 "DPI"는 단일 모니터 범위로 한정 |
| D11 | 테스트 에셋 라이선스 | **CC0** | 신규 |
| D12 | 캐릭터 로드 실패 시 처리 | **대체 캐릭터 없이 오류 표시** | 기준선 §6.2 "안전한 기본 캐릭터로 폴백"과 **다름** |
| D13 | 로드 실패 세부 동작 | 오류는 **로그에만 기록**. 로드된 캐릭터가 0개여도 **트레이 상주 유지** | D12 구체화 |
| D14 | C1 타이머 | latency waitable 대체 승인. **기본값 hires 확정**: 애니메이션 중에만 고해상도 Waitable Timer one-shot, 정지·숨김·잠금 시 타이머 해제 후 이벤트 대기. msgwait는 호환성 폴백·비교용 유지. CPU 시간·wake-up·실제 Present 지터로 전력 효율 확인 | §3.6 |
| D15 | C2 CPU 측정 | 조건부 승인. 판정은 `GetProcessTimes` 30~60초 이상 집계 + Windows 성능 카운터 교차 검증. 사이클 기반은 보조 지표 | §4.3 |
| D16 | C9 S0 정의 | S0-A(트레이만, GPU 장치 없음) / S0-B(GPU 장치, 창 없음)로 분리. 기존 S0 결과는 S0-B | §4.2 |
| D17 | C3~C8 | 미지원 조합은 실제 제약과 폴백 문서화. 캡처·측정 도구 변경은 목적 유지 범위에서 승인. **미수행 Device Lost 실험은 완료 처리하지 않고 보류** | §10 |
| D18 | 설치 | PresentMon 공식 최신 안정 버전 승인(2.6.0 CLI 사용). Graphics Tools 설치 승인. **`dxcap -forcetdr`는 미승인** — 별도 승인 후 검증 | §10 C8 |
| D19 | 측정 공통 | debug layer 활성 측정값은 정식 판정 제외. 주요 시나리오 3회 반복. 확인된 API 동작과 다른 환경 미검증 동작을 구분 기록 | §4.3, 보고서 |
| D20 | 단축 측정 | 핵심 10개 시나리오(S0-A, S0-B, S1/A·B·C, S2/A-hires·A-msgwait·C-hires, S3/small-5·single-5) × 3회 × 60초(안정화 10초, 약 38분, `-Core`). **예비 판정용**이며, 계획서 기준(10분 × 3회) 정식 측정은 별도 수행 | §4.2 |
| D21 | 측정 배치 | 측정 중 펫은 **작업 영역 좌상단**에 배치(`--anchor=top-left`, 스크립트 기본값). 이동 시나리오도 좌상단 구역 안에서만 왕복 | §4.2 |

---

## 1. 개발·측정 환경 기준 (P0)

### 1.1 개발 환경

| 항목 | 기준 | 비고 |
|---|---|---|
| OS | Windows 11 x64 (현재 개발 PC: 10.0.26200) | 최소 지원 버전은 P6 이전 확정 |
| 컴파일러 | MSVC (VS 2022 이상, `/std:c++20`) | Rider/VS Code는 CMake Presets로 동일 빌드 |
| 빌드 | CMake ≥ 3.25 + `CMakePresets.json` | `x64-debug`, `x64-release`, `x64-relwithdebinfo` |
| 패키지 | vcpkg manifest 모드 | P0는 외부 의존성 0개 목표 (아래 참고) |
| SDK | Windows SDK (d3d11, dxgi1_3+, dcomp, d2d1, dwrite, shcore, dwmapi) | 모두 OS 내장 |

**P0 외부 의존성 추천:** 없음.

- COM 스마트 포인터: `Microsoft::WRL::ComPtr` (SDK 포함). `wil`은 P1 이후 도입 검토. — *추천, 미확정*
- 테스트 이미지 디코딩: WIC (`IWICImagingFactory`, OS 내장). `stb_image`는 불필요. — *추천, 미확정*
- 셰이더: `fxc`/`dxc` 빌드 타임 컴파일 후 바이트코드 헤더 포함, 런타임 `D3DCompile` 미사용 (`d3dcompiler_47.dll` 의존 제거).

**앱 매니페스트 (P0에서 확정):**

```xml
<!-- 초안 -->
<application xmlns="urn:schemas-microsoft-com:asm.v3">
  <windowsSettings>
    <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
  </windowsSettings>
</application>
```

### 1.2 측정 PC

| 항목 | 사양 | 측정상 의미 |
|---|---|---|
| CPU | Intel Core i5-13400F (6 P코어 + 4 E코어, 16 스레드) | P/E 하이브리드 → EcoQoS·E코어 배치 관찰 가능. "전체 CPU 0.2%"는 논리 프로세서 1개 기준 약 3.2%에 해당 |
| GPU | NVIDIA RTX 3060 12GB (iGPU 없음, F 모델) | 단일 어댑터 → 교차 어댑터 복사 변수 없음 |
| 모니터 | 1대, QHD(2560×1440), Windows 배율 100%(96 DPI) | 혼합 DPI·다중 모니터·음수 좌표 시나리오는 P0 비범위(D10) |
| 전체화면 테스트 앱 | Mindustry (`C:\Program Files (x86)\Steam\steamapps\common\Mindustry\Mindustry.exe`) | Java/LWJGL 기반 OpenGL 앱으로 추정 → PresentMon 계측 범위 `[검증]` (§4.2) |

측정 시 고정할 조건: 전원 모드, GPU 드라이버 버전, Windows 배율, 백그라운드 앱 최소화, 측정 전 2분 안정화 대기.

---

## 2. WBS (작업 분해)

기준선 §13의 7개 항목을 `P0-xx`로 매핑한다. 각 항목은 **산출물**과 **완료 기준**을 가진다.

| ID | 작업 | §13 매핑 | 의존 | 산출물 | 완료 기준 |
|---|---|---|---|---|---|
| P0-01 | 빌드 골격: CMake Presets, vcpkg manifest(빈 의존성), 매니페스트, 경고 수준(`/W4 /permissive-`) | 1 | — | 빌드되는 빈 Win32 exe | 3개 Preset 모두 클린 빌드 |
| P0-02 | 메시지 루프 + 트레이(`Shell_NotifyIconW`, `NOTIFYICON_VERSION_4`) + 종료 메뉴 + 단일 인스턴스(`CreateMutexW`) | 1 | 01 | 트레이 상주 exe | 트레이 종료 시 프로세스·아이콘 잔류 없음, 탐색기 재시작(`TaskbarCreated`) 후 아이콘 복구 |
| P0-03 | D3D11 Device(`BGRA_SUPPORT` 활성) + DComp 투명 오버레이, 테스트 스프라이트(premultiplied alpha) 출력 | 2 | 02 | 투명 창 위 스프라이트 | 검은 사각형·테두리·잔상 없이 알파 합성 |
| P0-04 | 렌더 루프: 변경 시에만 Present, 30fps 애니메이션 모드, 정지 시 0fps | 2 | 03 | Present 카운터 | **상태 변화가 없는 정지 구간** 10분간 Present 0회 |
| P0-05 | Hit-test 후보 A/B/C 구현 및 비교 (§3.3) | 3 | 03 | 방식별 토글 빌드 | §5 E1 판정표 작성 |
| P0-06 | DPI 검증(단일 모니터 범위, D10): 배율·해상도 런타임 변경, 작업 표시줄 변경 | 4 | 03 | 좌표 로그 | §5 E5 시나리오 통과. 다중 모니터 항목은 "연기됨"으로 보고서에 기록 |
| P0-07 | WinEventHook 단일 창 추종 spike | 5 | 03 | 추종 데모 | 창 이동·최소화·종료 시 크래시/유령 위치 없음 (물리 없음, 즉시 스냅) |
| P0-08 | 측정 하네스: 인앱 카운터 + CSV 로그 + 반복 실행 스크립트 + 외부 도구 절차서 | 6 | 04 | `p0_metrics_*.csv`, 절차서 | §4 시나리오별 3회 이상 로그 확보 |
| P0-09 | 오버레이 전략 비교: 소형 창 N개 vs 단일 대형 오버레이 (N=1/3/5) | 6 | 05, 08 | 비교 로그 | §5 E2 판정표 작성 |
| P0-10 | 3D 테스트 도형: 깊이 버퍼 + 직교 카메라 + 투명 합성 | 7 | 03 | 회전 큐브 데모 | 알파 경계 정상, 깊이 정상, 2D와 동일 합성 경로 |
| P0-11 | D2D/DirectWrite 텍스트 1줄을 동일 Device에 출력 | — | 03 | 텍스트 데모 | D2D interop 정상 동작, 텍스트 알파 경계 정상 |
| P0-12 | Device Lost 복구 검증 | — | 04 | 복구 로그 | 강제 TDR 후 자동 복구, 누수 없음 |
| P0-13 | 결과 정리: 측정 로그, OS/그래픽 제약 목록, ADR-02 갱신·ADR-09 작성 | 산출물 | 전체 | `docs/adr/*`, `docs/p0_report.md` | §6 판정 완료 |

**권장 진행 순서:** 01 → 02 → 03 → 04 → 08 → 05 → 09 → 06 → 07 → 10 → 11 → 12 → 13  
측정 하네스(08)를 hit-test(05)·전략 비교(09)보다 먼저 두어, 비교 실험이 처음부터 같은 기준으로 기록되도록 한다.

---

## 3. 기술 설계 (P0)

> 이 장의 구현 선택(단일 UI 스레드, UI 스레드 EcoQoS 미적용, `WH_MOUSE_LL` 제외, 빌드 타임 셰이더 컴파일 등)은 **P0 spike 기본값**이며 실험 결과나 사용자 판단에 따라 변경할 수 있다. 단, `BGRA_SUPPORT` 활성화는 결정 사항(D7)이다.

### 3.1 오버레이 창 구성

```cpp
// 초안 — P0 spike 기준
DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP  // DComp 전용, GDI 리디렉션 서피스 미생성
              | WS_EX_TOPMOST
              | WS_EX_TOOLWINDOW            // 작업 표시줄/Alt+Tab 비표시
              | WS_EX_NOACTIVATE;           // 클릭 시 포커스 비탈취
DWORD style   = WS_POPUP;
// WM_MOUSEACTIVATE → MA_NOACTIVATE 반환
// ShowWindow(hwnd, SW_SHOWNOACTIVATE)
```

- 캐릭터 오버레이는 키보드 포커스를 갖지 않는다(기준선 §5.2). 대화 입력은 P3의 별도 창.
- `WS_EX_TOPMOST`는 다른 topmost 창과 경쟁한다. 주기적 Z-order 재확인 필요 여부를 P0에서 관찰만 하고, 정책은 P1에서 결정한다.
- `WS_EX_LAYERED`와 `WS_EX_NOREDIRECTIONBITMAP`의 동시 사용 가능 여부 및 동작은 `[검증]` 항목이다 (§3.3 방식 A의 전제).

### 3.2 렌더링 경로

```text
D3D11CreateDevice
     Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT   (D7: 초기부터 활성)
           | D3D11_CREATE_DEVICE_DEBUG          (Debug Preset 한정)
 → IDXGIFactory2::CreateSwapChainForComposition
     Format     = DXGI_FORMAT_B8G8R8A8_UNORM
     SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
     AlphaMode  = DXGI_ALPHA_MODE_PREMULTIPLIED
     Scaling    = DXGI_SCALING_STRETCH
     BufferCount= 2
     Flags      = FRAME_LATENCY_WAITABLE_OBJECT (E4에서 비교)
 → DCompositionCreateDevice(dxgiDevice)
 → CreateTargetForHwnd(hwnd, TRUE) → CreateVisual → SetContent(swapChain) → SetRoot → Commit
 → (P0-11) ID2D1Factory1 → CreateDevice(dxgiDevice) → ID2D1DeviceContext
          → back buffer의 IDXGISurface로 CreateBitmapFromDxgiSurface → DrawText
```

- 모든 셰이더 출력은 **premultiplied alpha**. 클리어 색은 `(0,0,0,0)`. D2D 비트맵도 `D2D1_ALPHA_MODE_PREMULTIPLIED`.
- 스프라이트는 텍스처 1장(Atlas 가정) + 정점 4개 쿼드. P0는 배치 렌더러 불필요.
- 대안 콘텐츠: `IDCompositionSurface`(`BeginDraw`/`EndDraw`)가 소형 창에서 메모리상 유리한지 E3에서 비교한다. `[검증]`
- 공유 원칙: `ID3D11Device` / `ID3D11DeviceContext` / `IDCompositionDevice` / `ID2D1Device`는 프로세스당 1개. 창마다 swapchain(또는 surface) + target + visual만 생성.

### 3.3 Hit-test(클릭 통과) 후보

전제: DComp + `WS_EX_NOREDIRECTIONBITMAP` 창은 **픽셀 알파가 아니라 창 사각형(또는 Region) 단위로 입력을 받는다.** `[검증]` — P0-05 첫 단계에서 이 전제부터 확인한다.

또한 `WM_NCHITTEST`에서 `HTTRANSPARENT`를 반환하는 방식은 **같은 스레드의 창에만** 입력을 넘기므로 다른 프로세스 창으로의 클릭 통과 수단이 될 수 없다. 후보에서 제외한다.

| 후보 | 방식 | 장점 | 우려 |
|---|---|---|---|
| **A. 동적 투명 토글** | 기본 `WS_EX_LAYERED \| WS_EX_TRANSPARENT`(전체 통과). 커서 위치를 저주기로 확인해 불투명 픽셀 위에 오면 `WS_EX_TRANSPARENT` 해제, 벗어나면 재설정. 판정은 CPU 측 알파 마스크(프레임별 1bit/픽셀 또는 저해상도 마스크) | 픽셀 단위 정확도, 렌더 경로 변경 없음 | `LAYERED`+`NOREDIRECTIONBITMAP` 공존 `[검증]`, 커서 확인 주기와 유휴 CPU 상충, 토글 직후 첫 클릭 유실 가능성 |
| **B. 윈도우 Region** | 알파 마스크로 `SetWindowRgn` 생성, 애니메이션 프레임 변경 시 갱신 | 펫 위 입력은 OS가 판정 | Region 갱신 비용(프레임마다), DComp 콘텐츠가 Region으로 클리핑되는지 `[검증]`, 안티앨리어싱 가장자리 처리 |
| **C. UpdateLayeredWindow 폴백** | DComp 대신 `UpdateLayeredWindow` + 32bpp DIB. OS가 알파 기반 hit-test | 픽셀 단위 통과가 OS 기본 동작 | GPU→CPU 복사 또는 CPU 렌더 필요, 3D/고해상도 비용 증가, ADR-02 경로 이탈 |

**커서 확인 주기와 근접 반응의 결합:** 기준선 §5.2의 "커서 근접 반응"도 커서 위치가 필요하다. 방식 A를 택할 경우 근접 탐지와 hit-test용 커서 확인을 **하나의 저주기 타이머**로 통합한다. 주기는 "커서가 캐릭터 근처일 때만 고주기, 멀면 저주기"의 적응형으로 하고, E1에서 주기별 CPU 비용과 체감 지연을 측정한다. `WH_MOUSE_LL` 저수준 훅은 시스템 전체 입력 지연 위험 때문에 **비교 대상으로만** 두고 기본 후보에서 제외한다.

근접 반응용 커서 확인은 A/B/C 모두에 존재하는 **공통 기준선**이다(§3.6). 따라서 E1은 "공통 커서 확인 비용 위에 각 방식이 추가하는 비용 + 입력 정확도"를 비교한다.

**판정 기준 (E1):**
1. 투명 픽셀 위 클릭/휠/드래그가 뒤 창(다른 프로세스, 관리자 권한 창 포함)에 정상 전달
2. 불투명 픽셀 위 클릭·드래그가 펫에 전달, 포커스 비탈취
3. 커서 확인 포함 유휴 CPU가 §4.3 임계값 이내
4. 토글/Region 갱신으로 인한 깜빡임·입력 유실 없음

### 3.4 오버레이 전략 비교 (소형 창 vs 단일 오버레이)

| 관점 | 캐릭터별 소형 창 | 화면 전체 단일 오버레이 |
|---|---|---|
| VRAM | 창 크기 × 버퍼 수 × N | 화면 전체 크기 × 버퍼 수 (측정 PC QHD 기준 버퍼 1장 약 14.7MB, 2장 약 29.5MB) |
| Present 비용 | 변경된 캐릭터 창만 | Dirty rect 사용 여부에 따라 전체 |
| Hit-test | 창 단위로 단순 | 전체 화면 크기의 창이 입력을 다뤄야 하므로 방식 A/B 의존도 높음 |
| 혼합 DPI | 창마다 자기 모니터 DPI | 단일 창이 여러 DPI 모니터에 걸침, 자체 스케일 로직 필요 (측정 PC에서는 재현 불가) |
| 전체화면 앱 상호작용 | 영향 범위가 캐릭터 영역으로 한정 | 화면 전체를 덮는 topmost 창이 다른 앱의 합성 방식에 영향을 줄 가능성 `[검증]` |
| 캐릭터 간 Z 순서 | 창 Z-order로 관리 | 내부 정렬로 관리 |
| 창 이동 | `SetWindowPos` 빈번 호출 | 창 고정, 내부 좌표만 변경 |

기준선 §14의 추천("캐릭터별 작은 창 우선")을 유지하고, E2에서 N=1/3/5의 비용을 측정해 확정한다.

**말풍선 처리:** 말풍선은 캐릭터보다 크고 수명이 짧다. 캐릭터 창 확장 vs 별도 말풍선 창은 P0-11 결과와 함께 P1 초기에 결정한다(§9 오픈 이슈).

### 3.5 DPI 및 좌표계

- 프로세스 DPI: Per-Monitor V2(매니페스트). 내부 좌표는 **물리 픽셀, 가상 화면 기준**(음수 허용).
- 캐릭터 표시 크기: `기본 크기 × (모니터 DPI / 96) × 사용자 배율`. 이 계산은 Rendering/Windows Adapter 계층에서만 수행하고 Core는 물리 픽셀만 다룬다.
- `WM_DPICHANGED` 수신 시 권장 rect를 그대로 쓰지 않고 **발 위치(foot anchor) 기준으로 재계산** 후 swapchain `ResizeBuffers`. 권장 rect를 무시해도 되는지는 `[검증]`.
- 모니터 정보: `EnumDisplayMonitors` + `GetMonitorInfoW`(`rcWork`로 작업 표시줄 제외 영역), `GetDpiForMonitor`. 변경 감지: `WM_DISPLAYCHANGE`, `WM_DPICHANGED`, `WM_SETTINGCHANGE`.
- **P0 검증 범위(D10, 단일 QHD·기본 배율 100%):**
  - 수행: Windows 배율 런타임 변경(100% → 125% → 150% → 100% 복귀), 해상도 변경, 작업 표시줄 위치·자동 숨김 변경
  - 연기: 혼합 DPI 모니터 간 이동, 음수 좌표 모니터, 모니터 연결/해제 — P1 이후 또는 다른 환경. 단, 좌표계 설계(가상 화면 기준·음수 허용)는 P0부터 유지한다.

### 3.6 메인 루프 및 프레임 페이싱

```text
UI 스레드 (P0는 단일 스레드 원칙):
  loop:
    timeout = (애니메이션 중) ? 다음 프레임까지 남은 시간 : 커서 확인 주기
    MsgWaitForMultipleObjectsEx(handles=[frameTimer, (latencyWaitable)], timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE)
    메시지 처리 (WinEvent 콜백 포함 — 훅은 이 스레드에서 설치)
    커서 확인 (근접/hit-test)
    if (dirty) Render + Present
```

- 정지 상태 목표: **상태 변화가 없으면 Present 0**(D6). 커서 근접으로 표정·시선이 바뀌는 것은 상태 변화이므로 Present가 발생해도 위반이 아니다. 상시 타이머는 **커서 확인 타이머 1개(적응형 주기)만 허용**한다. 근접 반응은 캐릭터 영역 밖의 커서 위치가 필요하고, 그 영역에서는 창/Region이 마우스 메시지를 받지 못하므로 hit-test 방식과 무관하게 저주기 커서 확인이 존재한다(근접 반응 OFF 시에는 방식 B/C에서 제거 가능).
- **타이머 기본값: hires (D14 확정).** 모든 모드가 `MsgWaitForMultipleObjectsEx`로 대기하고, 깨우는 수단만 다르다.
  - hires(기본): **애니메이션 중에만** `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)`을 one-shot으로 걸어 다음 프레임에 깨운다. 커서 확인·측정 샘플 같은 그 외 마감은 일반 timeout으로 깨운다.
  - 정지·숨김(트레이 "펫 숨기기")·화면 잠금(`WTS_SESSION_LOCK`) 상태에서는 고해상도 타이머를 해제(`CancelWaitableTimer`)하고 이벤트 기반으로 대기한다. 숨김·잠금 중에는 커서 확인·렌더도 멈추고, 복귀 시 누적 시간은 버린다.
  - msgwait(timeout만): 호환성 폴백(고해상도 타이머 생성 실패 시 자동 사용) 및 성능 비교용으로 유지.
  - SetTimer: 비교용.
  - 측정 지표: CPU 사용 시간, **wake-up 횟수**(`wakeups_delta`), **실제 Present 간격·지터**(`present_interval_ms_*`, `present_jitter_ms_p95`), 타이머 기상 지연(`tick_jitter_ms_*`). 이 값으로 전력 효율 문제 여부를 판단한다.
- EcoQoS(ADR-08): P0에서는 UI 스레드에 적용하지 않고, 측정 비교용으로 "프로세스 전체 EcoQoS ON/OFF" 시나리오만 기록한다. 측정 PC가 P/E 하이브리드이므로 WPA의 코어별 CPU 사용 뷰로 E코어 배치 경향을 함께 관찰한다(배치 보장은 기대하지 않음).

### 3.7 WinEventHook 단일 창 추종 spike (P0-07)

```cpp
// 초안
SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                nullptr, OnWinEvent, 0, 0,
                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
// 콜백 필터:
//   idObject == OBJID_WINDOW && idChild == CHILDID_SELF
//   hwnd == 추적 대상 (P0는 단일 창)
```

- `EVENT_OBJECT_LOCATIONCHANGE`는 캐럿·커서 등에서도 대량 발생하므로 `OBJID_WINDOW` / `CHILDID_SELF` 필터가 필수다.
- `WINEVENT_OUTOFCONTEXT` 훅은 **설치한 스레드에 메시지 루프가 있어야** 콜백이 전달된다.
- 추가 구독(spike 범위): `EVENT_SYSTEM_MINIMIZESTART/END`, `EVENT_OBJECT_DESTROY`, `EVENT_OBJECT_HIDE`, `EVENT_OBJECT_CLOAKED/UNCLOAKED`.
- 창 경계: `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`(그림자 제외). `GetWindowRect`와의 차이를 로그로 남긴다.
- P0에서 측정할 것: 빠른 드래그 시 이벤트 빈도, 콜백→Present 지연, 이벤트 병합 필요성.

### 3.8 3D 테스트 경로 (P0-10)

- 별도 `ID3D11Texture2D`(`D24_UNORM_S8_UINT`) 깊이 버퍼를 만들어 swapchain back buffer RTV와 함께 바인딩. 직교 투영, 회전 큐브(정점 컬러).
- 출력은 premultiplied alpha로 통일. 반투명 면 1개를 포함해 블렌딩 경계 확인.
- 2D 스프라이트와 같은 프레임에 함께 그려 "공통 경로"를 확인한다. glTF 로딩은 P5.

### 3.9 Device Lost (P0-12)

- `Present`/`ResizeBuffers`의 `DXGI_ERROR_DEVICE_REMOVED` / `DXGI_ERROR_DEVICE_RESET` 처리.
- 복구 범위: D3D Device → swapchain → (dxgiDevice로 생성한) DComp Device·D2D Device → target/visual/D2D 리소스 재생성. DComp Device를 재생성해야 하는지 `[검증]`.
- 강제 TDR 도구 후보: Graphics Tools의 `dxcap -forcetdr`. `[검증]`

### 3.10 P0 코드 구조 (재사용 고려 최소 분리)

```text
desktop-pet/
  apps/p0-spike/main.cpp            # 실험 토글(명령행 인자)로 E1~E8 전환
  src/platform/windows/             # 창 생성, 트레이, DPI, WinEvent spike
  src/rendering/d3d11/              # Device, Composition, Sprite, Cube, D2D Text
  src/diagnostics/                  # 카운터, CSV 로거
  tools/measure/                    # 반복 측정 스크립트(PowerShell)
  assets/test/                      # 테스트 스프라이트·텍스처 (배포 포함 대상, D3 / CC0, D11)
  docs/adr/  docs/p0_report.md
```

명령행 예시(초안): `p0-spike.exe --hittest=A --overlay=small --pets=3 --anim=30 --run=2 --log=out.csv`

---

## 4. 측정 계획

### 4.1 측정 항목과 수단

| 항목 | 수단 | 비고 |
|---|---|---|
| 프로세스 CPU | `GetProcessTimes`, `QueryProcessCycleTime` (인앱, 1초 간격) | 점유율과 누적 시간 모두 기록 |
| 메모리 | `GetProcessMemoryInfo` → `WorkingSetSize`, `PrivateUsage` | 기준선 §8.2: 두 값 모두 기록 |
| VRAM | `IDXGIAdapter3::QueryVideoMemoryInfo` (Local/Non-local) | 프로세스 기준 사용량 |
| Present | 인앱 카운터 | 상태 변화 없는 정지 구간에서 0 확인 |
| **DWM 비용** | `\Process(dwm)\% Processor Time`, `\GPU Engine(*)\Utilization Percentage`(pid 필터) via `Get-Counter` | 합성 비용 일부가 `dwm.exe`에 귀속되므로 **자체 프로세스만 보면 과소 측정** |
| 프레임/합성 방식 | PresentMon (present mode, frame time, 지연) | 게임 병행 시나리오 핵심 |
| 게임 성능 | PresentMon frame time(평균, 1% low) + 보조로 게임 내 FPS 표시 | Mindustry가 OpenGL 앱이면 PresentMon 계측 가능 여부 `[검증]` |
| 상세 분석 | WPR/WPA, GPUView | 이상치 발생 시에만 |
| 입력 지연 | 인앱 타임스탬프(입력 메시지 수신 → Present 반환) | 체감 지연 보조 지표 |

### 4.2 시나리오

| ID | 시나리오 | 1회 시간 | 변수 |
|---|---|---|---|
| S0-A | 트레이만, **GPU 장치 미생성** (`--pets=0 --gpu=off`) | 10분 | — |
| S0-B | **GPU 장치 생성**, 펫 창 없음 (`--pets=0`). 2026-10-13 이전 "S0" 결과는 S0-B로 취급 (D16) | 10분 | — |
| S1 | 1마리 정지, 근접 반응 ON | 10분 | hit-test A/B |
| S1′ | 1마리 정지, 근접 반응 OFF | 10분 | hit-test A/B — 커서 확인 비용 분리용 |
| S2 | 1마리 30fps 애니메이션 | 10분 | 타이머 방식(E4) |
| S3 | 3마리 / 5마리 애니메이션 | 각 10분 | 소형 창 vs 단일 오버레이(E2) |
| S4 | 창 추종 중 대상 창 연속 드래그 | 2분 | 이벤트 병합 유무 |
| S5 | Mindustry 전체화면 병행 | 10분 | 게임 모드(전체화면 / 테두리 없는 창 `[검증]`) × 펫 상태(**숨김 / 정지 표시 / 애니메이션 표시**) |
| S6 | 3D 큐브 30fps | 10분 | 2D와 비교 |

**반복 규칙 (D8):**
- 각 시나리오·변수 조합을 **3회 이상** 실행한다. 회차 사이 앱 재시작, 2분 안정화 대기.
- 보고값: 회차별 값 + **중앙값**, 최소/최대. 회차 간 편차가 큰 항목(예: 중앙값 대비 ±20% 초과 — 제안값)은 추가 반복하거나 원인을 기록한다.
- 전체 조합 기준 총 측정 시간은 약 10시간 이상으로 예상되므로 `tools/measure/`의 스크립트로 무인 반복 실행한다. 시간 제약이 있으면 판정에 직접 쓰이는 S1·S2·S3·S5를 우선한다.

**S5 주의:**
- 측정 PC는 모니터 1대이므로 전체화면 게임 위에 펫이 표시되면 항상 겹친다. 기준선의 "다른 모니터" 조건은 이 PC에서 측정하지 않는다.
- topmost 펫이 겹치면 게임이 independent flip에서 composed 모드로 바뀔 가능성이 있다 `[검증]`. present mode 변화 자체만으로 판정하지 않고 **frame time·1% low·입력 지연 변화와 함께** 판정한다(D6).
- Mindustry의 전체화면/테두리 없는 창 설정 지원 여부와 모드별 present mode는 첫 측정에서 확인한다.
- 이 결과는 기준선 §4.3(전체화면 숨김 정책)의 근거 데이터가 된다.

### 4.3 판정 임계값

기준선 §8.2의 목표치를 녹색 기준으로 사용한다(D6: CPU·RAM 수치 유지).

| 항목 | 녹색(목표) | 노랑(조사 후 진행 가능) | 빨강(설계 재검토) |
|---|---|---|---|
| 1마리 정지 CPU (전체 대비, 중앙값) | ≤ 0.2% | ≤ 0.5% | > 0.5% |
| 1마리 30fps CPU (전체 대비, 중앙값) | ≤ 1% | ≤ 2% | > 2% |
| 1마리 Working Set (중앙값) | ≤ 60MB | ≤ 90MB | > 90MB |
| 상태 변화 없는 정지 구간의 Present | 0 | — | > 0 |
| DWM CPU 증가분(S2−S0) | 측정 후 기준 설정 | — | — |
| S5 게임 영향 (펫 표시 vs 숨김) | present mode 변화 없음, 또는 변화가 있어도 frame time·1% low 차이가 측정 편차 이내 | 측정 편차를 넘는 차이가 있으나 체감 영향 미미 | 체감 가능한 frame time 악화·끊김 발생 |

- **빨강은 자동 개발 중단 조건이 아니라 설계 재검토 조건**이다(D6). 빨강 판정 시 원인 분석 → 대안 검토 → 진행/수정 결정을 사용자와 함께 한다.
- S5의 "측정 편차"는 숨김 조건 3회 반복의 최소~최대 범위로 정의한다. 수치 기준(예: 1% low n% 이내)은 S5 1차 측정 후 제안한다.
- CPU 점유율과 함께 **CPU 누적 시간(ms/분)** 을 보고한다.
- **CPU 판정 규칙(D15):** 표의 CPU 값은 `GetProcessTimes`를 **60초 창으로 집계**한 CPU%(창 평균)다. Windows 성능 카운터(`\Process(...)\% Processor Time` ÷ 논리 프로세서)와 교차 검증하고, `QueryProcessCycleTime` 기반 값은 **보조 지표로만 기록**한다(단독 판정 금지).
  - 주의(측정 PC에서 확인): 성능 카운터도 `GetProcessTimes`와 같은 틱 기반 집계라, 극저부하(예: 방식 C 30fps)에서는 둘 다 0%이고 사이클 기준만 0.027%처럼 0이 아니게 나온다. 두 값의 일치는 이 구간에서 독립 근거가 아니므로 보고서에 사이클 값을 함께 적는다.
- **D3D11 debug layer 활성 실행은 정식 판정에서 제외(D19).** `summarize.ps1`이 CSV 헤더 `d3d_debug_layer=true`인 실행을 자동 제외한다.
- 주요 성능 시나리오는 **3회 반복**하며, 3회 미만 집계는 "판정 보류"로 표시한다.

### 4.4 로그 형식 (CSV, 초안)

```text
timestamp_utc, scenario, variant, run_index, pet_count, anim_fps,
proc_cpu_pct, proc_cpu_ms_delta, proc_cycles_delta,
working_set_mb, private_mb, vram_local_mb, vram_nonlocal_mb,
present_count_delta, state_changed, dwm_cpu_pct, gpu_util_pct_proc, gpu_util_pct_dwm,
input_latency_ms_p50, input_latency_ms_p95, notes
```

- `run_index`: 반복 회차(D8). `state_changed`: 해당 구간의 상태 변화 여부(Present 0 판정 대상 구분, D6).
- S5는 PresentMon CSV를 별도 파일로 저장하고 `scenario`/`variant`/`run_index`로 연결한다.
- 측정 환경 메타데이터(§1.2 항목, GPU 드라이버 버전, Windows 배율, 전원 모드, OS 빌드)는 로그 파일 헤더에 기록한다.

---

## 5. P0 실험 매트릭스

| ID | 실험 | 비교 대상 | 판정 기준 | 관련 WBS |
|---|---|---|---|---|
| E1 | Hit-test | A / B / C (+ `WH_MOUSE_LL` 참고 측정) | §3.3 판정 기준 1~4 | P0-05 |
| E2 | 오버레이 전략 | 소형 창 N개 / 단일 오버레이, N=1/3/5 | VRAM·CPU·DWM 증가량, 구현 복잡도 | P0-09 |
| E3 | 콘텐츠 방식 | Composition swapchain / `IDCompositionSurface` | 소형 창 VRAM, Present 비용 | P0-03 |
| E4 | 프레임 타이머 | High-res waitable timer / `SetTimer` / msgwait timeout (latency waitable 대체, §10 C1) | 30fps 지터, CPU 누적 시간 | P0-04 |
| E5 | DPI (단일 모니터, D10) | 배율 런타임 변경(100→125→150→100%), 해상도 변경, 작업 표시줄 변경 | 크기·위치 복원, 깨짐·잔상 없음 | P0-06 |
| E6 | 전체화면 상호작용 | Mindustry 모드 × 펫 숨김/정지 표시/애니메이션 표시 | §4.3 S5 행 | P0-08 (S5) |
| E7 | Device Lost | 강제 TDR | 자동 복구, 핸들·메모리 누수 없음 | P0-12 |
| E8 | 3D 경로 | 2D 단독 / 2D+3D | 알파·깊이 정상, 비용 증가량 | P0-10 |

---

## 6. 판정 및 ADR 확정 절차

### 6.1 판정 흐름

```text
E1 hit-test
 ├─ A 또는 B가 기준 충족 → ADR-09: 채택 방식 기록, ADR-02(D3D11+DComp) 확정
 └─ A·B 모두 실패
      ├─ C(UpdateLayeredWindow)로 2D 비용이 §4.3 녹색/노랑 → ADR-02 수정안: 2D=ULW, 3D 경로 별도 검토
      └─ C도 빨강 → 오버레이 전략 설계 재검토 (기준선 §13 3번 "실패 시 재검토")
E2 → 기준선 §14 "오버레이 창 전략" 확정
E3/E4 → 렌더러 내부 구현 방침(ADR 부속 노트)
E6 → 기준선 §4.3 전체화면 숨김 기본값 재확인
E8 → P5 전제 유효성 기록
```

- 모든 "빨강" 분기는 **설계 재검토 회의 안건**이며 자동 중단이 아니다(D6). ADR 수정안은 사용자 승인 후 확정한다.

### 6.2 P0 종료 산출물

1. `p0-spike.exe` 및 실험 토글 사용법
2. 측정 로그(CSV, 시나리오별 3회 이상) + 측정 환경 메타데이터
3. `docs/p0_report.md`: 실험별 결과(중앙값·범위), 임계값 대비 판정, 알려진 OS/그래픽 제약 목록, 연기된 항목(다중 모니터 등, D10)
4. ADR: ADR-02 갱신(확정/수정), ADR-09 Hit-test 전략(신규), 오버레이 전략 ADR(신규)
5. P1 일정 추정(기준선 §10 "일정은 P0 측정 이후 추정")

---

## 7. P1 개요 → 별도 문서

P1 상세 계획은 [`desktop_pet_p1_detailed_plan_ko.md`](desktop_pet_p1_detailed_plan_ko.md)로 옮겼다(2026-10-13). 이 절의 이전 작업 분해(P1-01~P1-11)는 그 문서의 WBS(P1-00~P1-13)로 대체된다.

---

## 8. 기준선 §14 결정 사항 — 상태

| # | 의사결정 | 내용 | 상태 | 확정 데이터/시점 | 폴백 |
|---|---|---|---|---|---|
| 1 | 오버레이 창 전략 | 캐릭터별 소형 창, 공유 D3D/DComp Device | 추천 (미확정) | E2·E6 결과 / P0 종료 | 5마리에서 비용 초과 시 모니터별 오버레이 1개 |
| 2 | 정밀한 창 가림 계산 | MVP는 **창 상단 가장자리 1D 구간 빼기**: 대상 창 상단 선분에서 Z-order상 위에 있는 보이는 창들의 x 구간을 제거 | 추천 (미확정) | P1-03 구현 후 겹침 테스트 / P1 | 측면 등반 시 좌우 가장자리도 동일 방식 확장 |
| 3 | 캐릭터팩 라이선스·배포 | 공식 기본 캐릭터팩 CC BY 4.0, 서드파티 팩은 개별 라이선스 허용. **배포물에는 기본 캐릭터팩 미포함, 테스트 에셋(CC0)만 첨부**. 로드 실패 시 대체 캐릭터 없이 오류 표시 | **승인 (D2·D3·D11·D12)** | — | — |
| 4 | 캐릭터 간 충돌·협동 | 비활성으로 시작, P2 이후 옵션 플래그 | 추천 (미확정) | P2 이후 | — |
| 5 | AI 대화 이력 | 최근 20턴 유지. 디스크 저장 기본 OFF, 활성화 시 7일 보관. 장기 기억은 Opt-in | **승인 (D4)** — 장기 기억 형식은 P3에서 결정 | P3 | — |
| 6 | 기본 TTS 엔진 | `Windows.Media.SpeechSynthesis`(WinRT) 우선, SAPI 5는 폴백. 근거: 단어 경계 메타데이터로 립싱크 타이밍 인터페이스(기준선 §7.4) 대응 `[검증]` | 추천 (미확정) | P4 | SAPI 5 |
| 7 | 외부 네이티브 플러그인 | 초기: 내장 모듈 + Lua. P6 이후 **별도 프로세스 IPC 우선**, C ABI는 신뢰 플러그인 한정 | 추천 (미확정) | P6 이후 | C ABI 단독 |
| 8 | 3D 공식 지원 범위 | glTF 2.0(정적 메시 → 스키닝·애니메이션) → VRM 1.0(Expression·LookAt → MToon → SpringBone) 순 | 추천 (미확정) | P5 | — |
| 9 | 코드 라이선스 | Apache-2.0 | **승인 (D5)** | P6 이전 LICENSE 반영 | — |

**#3 결정에 따른 후속 영향:**
- 캐릭터 패키지 로더(P2)는 팩별 라이선스 메타데이터를 필수 필드로 검증한다(기준선 §6.2 "라이선스/출처 기록"과 일치).
- 배포 ZIP 구성: 코어 바이너리(Apache-2.0) + `assets/test/`(CC0) + 라이선스 고지. `characters/` 기본 팩 없음.
- 기준선 §6.2의 "안전한 기본 캐릭터로 폴백"은 **"검증 실패 사유를 로그에 기록하고 해당 캐릭터를 로드하지 않음"** 으로 대체한다(D12·D13).
  - 오류는 **로그에만 기록**한다(트레이 알림·대화 상자 없음).
  - 나머지 정상 캐릭터는 그대로 실행한다.
  - 로드된 캐릭터가 0개여도 **트레이 상주를 유지**한다(종료하지 않음).

---

## 9. 오픈 이슈

| # | 이슈 | 선택지 | 필요한 것 |
|---|---|---|---|
| 1 | 말풍선 표시 방식(캐릭터 창 확장 vs 별도 창) | — | P0-11 결과 후 P1 초기 결정 |
| 2 | topmost 경쟁(다른 topmost 앱, 작업 표시줄)에 대한 재확인 정책 | — | P0 관찰 → P1 결정 |
| 3 | 최소 지원 Windows 버전(Windows 11 전용 vs Windows 10 포함) | — | P6 이전 결정, API 선택에 영향 |
| 4 | ~~리포지토리 초기화 시점~~ → 2026-10-13 로컬 `git init`만 수행(원격·커밋 없음), 문서는 `docs/plan/`으로 이동 | — | 해결 |
| 5 | S5 수치 판정 기준(frame time·1% low 허용 차이) | — | S5 1차 측정 후 제안 → 사용자 승인 |
| 6 | 다중 모니터 실환경 검증 환경 확보 방법 (D10 연기분, P1-11) | 보조 모니터/TV 임시 연결 / 가상 디스플레이 드라이버 / 다른 PC | 해당 시점에 결정 |

---

## 10. 구현 중 변경·대체 사항 (2026-10-13 제기 → 같은 날 사용자 결정 D14~D19 반영)

P0 spike 구현 결과 계획과 달라진 부분이다. 결과 상세는 `docs/p0_report.md`.

| # | 계획 | 실제 | 사유 | 결정 |
|---|---|---|---|---|
| C1 | E4 타이머 후보: high-res waitable / `SetTimer` / **frame latency waitable** | latency waitable 대신 **msgwait(timeout만)** 비교 | latency waitable은 Present마다 신호되는 구조라 디스플레이 주사율(측정 PC 180Hz)에 묶이고, 30fps 고정·"변경 시에만 Present"와 맞지 않음 | **승인(D14)**. 기본값 hires 확정, msgwait는 폴백·비교용 |
| C2 | CPU% = `GetProcessTimes` | 사이클 기반 `proc_cpu_pct_cycles` 추가 | `GetProcessTimes`가 15.6ms 틱 단위라 저부하에서 0 또는 과소 측정 | **조건부 승인(D15)**. 판정은 60초 집계 + 성능 카운터 교차 검증, 사이클은 보조 |
| C3 | 방식 C 비교 범위 | small·swapchain 기본값 전용 | C는 GPU 경로가 아닌 별도 창 유형 | **문서화(D17)** — 아래 제약·폴백 표 |
| C4 | E3 surface 경로 + 3D | surface + 큐브 미지원 | BeginDraw 오프셋/깊이 버퍼 크기 불일치 | **문서화(D17)** |
| C5 | 자체 검증 캡처(계획 외) | DXGI Desktop Duplication | GDI BitBlt가 DComp 콘텐츠를 캡처하지 못함 | **승인(D17)** |
| C6 | 측정 하네스 | 외부 수집기 `p0-counters.exe`(PDH `PdhAddEnglishCounterW`) | 인앱 `dwm.exe` OpenProcess 거부, `Get-Counter` 이름 지역화 | **승인(D17)** |
| C7 | 단일 오버레이 + hit-test 없음 | 10초 이하 실행만 허용 | 데스크탑 입력을 전부 가로채는 위험 | **문서화(D17)** |
| C8 | E7 강제 TDR | 앱 내부 재생성 시뮬레이션만 확인 | Graphics Tools 미설치, `dxcap -forcetdr` 미승인 | **보류(D17·D18)** — 완료 처리하지 않음 |
| C9 | S0 = "펫 없음(트레이만)" | S0-A / S0-B 분리 구현 (`--gpu=off`) | 기존 S0는 GPU 장치를 생성하고 있었음 | **수정 승인(D16)** |

### 10.1 미지원 조합의 실제 제약과 폴백 (D17)

| 조합 | 실제 제약 | 현재 동작 | 제품(P1 이후) 폴백 방향 |
|---|---|---|---|
| 방식 C + 3D 큐브 / D2D 텍스트 / 단일 오버레이 / surface | `UpdateLayeredWindow`는 GDI DIB를 OS가 합성하는 창 유형이라 D3D·DComp 출력과 같은 창에 쓸 수 없음 | 옵션 검증에서 거부(종료 코드 2) | C 채택 시 3D는 GPU 렌더 후 CPU readback → DIB 복사가 필요(비용 증가). 말풍선은 별도 창 또는 GDI/D2D DC 렌더 |
| `content=surface` + 3D 큐브 | `IDCompositionSurface::BeginDraw`가 큰 아틀라스 텍스처 안의 오프셋을 돌려줘, 펫 크기 깊이 버퍼를 같은 RTV에 바인딩할 수 없음 | 옵션 검증에서 거부 | 3D는 swapchain 콘텐츠 사용. surface를 써야 하면 오프스크린 텍스처(깊이 포함)에 그린 뒤 surface로 복사 |
| `overlay=single` + `hittest=none` | 화면 전체 topmost 창이 모든 입력을 받음 | 10초 이하만 허용 | 제품에서는 금지 조합 |
| 방식 B + Region 밖 콘텐츠 | `SetWindowRgn`이 DComp 출력도 클리핑(확인됨) | 텍스트 라벨이 보이지 않음 | 말풍선·라벨을 Region에 포함하거나 별도 창 |
| D3D11 debug layer | Graphics Tools 미설치 시 `DXGI_ERROR_SDK_COMPONENT_MISSING` | 경고 로그 후 debug 없이 생성 | 동일(자동 폴백). Release 기본값은 OFF, 측정 스크립트는 `--debug-layer=off` 명시 |
| 고해상도 Waitable Timer | `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` 미지원 OS(Windows 10 1803 이전 추정) | 생성 실패 시 경고 후 msgwait로 자동 폴백 | 동일 |
| 인앱 dwm.exe CPU | 일반 권한에서 `OpenProcess` 거부(err=5) | CSV 열 비움, `p0-counters`로 측정 | 측정 전용, 제품 기능 아님 |
| PresentMon | ETW 세션 시작에 관리자 권한(또는 Performance Log Users 그룹) 필요 `[검증]` | S5는 관리자 터미널에서 실행 | 측정 전용 |

---

**요약:** P0는 13개 작업(P0-01~13)과 8개 실험(E1~E8)으로 구성된다. 핵심 판정은 E1(클릭 통과 방식)과 E2(오버레이 전략)이며, 측정은 시나리오별 3회 이상 반복하고 자체 프로세스뿐 아니라 `dwm.exe` 비용과 Mindustry 병행 시 present mode·frame time 변화까지 포함한다. 빨강 판정은 설계 재검토 조건이다. 결과는 ADR-02 갱신과 ADR-09 신규 작성으로 고정하고, 그 뒤 P1 일정을 추정한다.
