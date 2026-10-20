# Windows Desktop Pet 개발 계획서

**버전:** 1.3 (기획 기준선)  
**작성일:** 2026-10-13  
**플랫폼:** Windows 11 x64  
**핵심 기술:** C++20 · Win32 · Direct3D 11 · DirectComposition  
**배포 형태:** 오픈소스 / 로컬 우선  
**문서 상태:** 요구사항 확정 / 일부 기술 검증 대기

> **프로젝트 정의:** 일반 애플리케이션 창의 가장자리에서 움직이고, 마우스와 상호작용하며, 사용자가 허용한 화면을 가끔 관찰해 캐릭터의 페르소나에 맞는 발화를 하는 저자원 데스크탑 펫. 첫 릴리스는 2D 스프라이트 중심이지만 다중 캐릭터, 선택적 TTS, SIWC, 향후 Live2D·3D를 고려한다.

## 1. 목표 및 범위

### 1.1 제품 목표

- Windows 11에서 상시 실행해도 CPU·RAM·GPU 부담이 낮은 데스크탑 펫을 제공한다.
- 브라우저, 탐색기 등 일반 창의 상단·측면을 캐릭터의 물리적 지형으로 취급한다.
- 클릭·드래그·커서 추적에 반응하며, 여러 캐릭터를 한 프로세스에서 실행할 수 있도록 한다.
- 캐릭터별 스프라이트·페르소나·행동·음성을 별도 패키지로 관리한다.
- AI 사용 여부와 관계없이 기본 캐릭터 동작을 독립적으로 유지한다.
- 사용자 동의 시 SIWC 등을 통해 화면을 제한적으로 인식하고 가끔 자율 발화한다.
- 2D 물리 및 행동 로직을 바꾸지 않고 Live2D/3D 렌더러를 추가할 수 있게 한다.

### 1.2 초기 버전에 포함하지 않을 것

- 외부 프로그램 내부 버튼의 자동 조작, 관리자 권한이 필요한 제어, 게임 프로세스 후킹.
- 상시 화면 스트리밍, 사용자 몰래 실행되는 화면 분석, 항상 켜진 음성 인식.
- 복잡한 범용 3D 물리 엔진, 네트워크 멀티플레이, 확장 플러그인의 무제한 OS 권한.
- SIWC 토큰을 범용 LLM API처럼 다른 앱으로 재배포하는 중계 기능.

### 1.3 사용자 경험 정책

| 항목 | 확정 정책 |
|---|---|
| 창 표시 | 일반 창 위에 표시, 전체화면 게임에서는 자동 숨김; 사용자 예외 목록 제공 |
| 캐릭터 수 | 다중 캐릭터 인스턴스를 고려한 단일 프로세스 구조 |
| 입력 | 좌클릭 대화, 우클릭 메뉴, 드래그, 커서 근접 반응 |
| 대화 진입 | 캐릭터 클릭 및 전역 단축키(기본 후보: Ctrl+Alt+P) |
| AI 발화 | 말풍선 기본; TTS 선택적 활성화 |
| 화면 관찰 | 최초 OFF; 명시적 허용, 범위 선택, 즉시 중지 가능 |
| 그래픽 | 처음은 2D 스프라이트; 차후 Live2D 및 glTF/VRM 3D |
| 오픈소스 | 코어 소스 공개; 캐릭터 및 독점 SDK 라이선스 별도 적용 |

## 2. 핵심 기술 결정(ADR 요약)

| ID | 결정 | 근거 | 상태 |
|---|---|---|---|
| ADR-01 | C++20 / MSVC / CMake / vcpkg | Win32·DirectX·Native SDK 연동, 낮은 런타임 오버헤드 | 확정 |
| ADR-02 | D3D11 중심, DirectComposition 합성 | 2D/3D 공용 GPU 경로와 투명 데스크탑 합성 | 기술 검증 필요 |
| ADR-03 | 물리는 2D, 표시만 2D/3D(2.5D) | Windows 창 경계와 충돌 구조 재사용 | 확정 |
| ADR-04 | 캐릭터 에셋과 인스턴스 상태 분리 | 동일 패키지 중복 로딩 방지, 다중 캐릭터 확장 | 확정 |
| ADR-05 | 이벤트 중심 Win32 창 감시 | 전역 고주기 폴링 최소화 | 확정 |
| ADR-06 | AI Provider·Capture·TTS 분리 | 선택적 실행, SIWC 외 공급자 확장 | 확정 |
| ADR-07 | Lua는 행동 스크립트, 네이티브 플러그인은 제한적으로 | 쉬운 커스터마이징과 코어 안정성의 균형 | 확정(세부 정책 추후) |
| ADR-08 | 백그라운드 스레드에 EcoQoS 힌트 | E코어 선호 및 전력 효율, 강제 고정은 기본 미사용 | 확정 |

## 3. 논리 아키텍처

```text
Desktop Pet Application (설정·트레이·단축키)
    │
    ├─ Core Runtime: EventBus / Scheduler / Permissions / Config
    │     ├─ PetManager: PetInstance N개 / ResourceManager 공유
    │     ├─ WorldManager: 모니터·창·가상 데스크탑 / Z-order
    │     ├─ Physics: 충돌·부착·낙하 (2D)
    │     ├─ Behavior: FSM / Lua / 행동 명령
    │     └─ Input: 클릭·드래그·커서 근접
    │
    ├─ Rendering: IRenderer / D3D11 / DComp
    │     ├─ Sprite (MVP)
    │     ├─ Live2D (추후)
    │     └─ 3D glTF/VRM (추후)
    │
    ├─ AI Orchestrator: 페르소나 / Context / ActionValidator
    │     ├─ SIWC Provider (우선)
    │     ├─ 일반 API Provider (선택)
    │     └─ Local Model Provider (선택)
    │
    ├─ ScreenObserver: Windows.Graphics.Capture / 개인정보 필터
    └─ Speech: 말풍선 / TTS Provider / 오디오 큐
```

**의존성 규칙:** Core 도메인 모델은 `HWND`, COM 포인터, GPU 리소스, OAuth 토큰을 직접 보유하지 않는다. 플랫폼 객체는 Windows Adapter와 렌더링/AI 구현체에서만 관리한다. AI는 위치를 직접 수정하지 않고 검증 가능한 의미적 행동 명령만 발행한다.

### 3.1 주요 데이터와 책임

| 컴포넌트 | 핵심 책임 | 금지할 책임 |
|---|---|---|
| PetManager | 인스턴스 생성·삭제·일시정지, 캐릭터 전환 | 스프라이트 직접 디코딩 |
| WorldManager | 창/모니터 좌표, 보이는 충돌 표면 | 캐릭터 행동 선택 |
| PhysicsSystem | 위치·속도·착지·창 부착 | OS 창을 임의로 이동 |
| BehaviorSystem | FSM, 이벤트 대응, Lua 실행 | SIWC 토큰 접근 |
| RenderingSystem | 상태를 픽셀로 출력, 리소스 캐싱 | 행동 정책 결정 |
| ScreenObserver | 동의된 화면 1프레임 획득·가공 | 무허가 화면 저장·전송 |
| AiOrchestrator | Provider 호출·페르소나·발화 판단 | Win32·그래픽 직접 조작 |
| SpeechManager | 말풍선·TTS 재생 정책 | 모델 로그인 처리 |

## 4. Windows 창·물리 상호작용

### 4.1 창 감시 및 좌표

- 시작 시 `EnumWindows`로 창 목록을 수집한다.
- `SetWinEventHook(WINEVENT_OUTOFCONTEXT)`으로 생성·종료·전경 변경·위치 변경을 구독하며 자체 프로세스 이벤트는 필터링한다.
- 창 경계는 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 등을 검토해 취득한다.
- DPI 인식 수준은 Per-Monitor DPI Awareness V2로 하고, 내부 물리 좌표는 물리 픽셀로 통일한다.
- 최소화·cloaked·현재 가상 데스크탑 외부·사용자 제외 목록을 필터링한다.
- 이벤트 연속 발생 시 동일 창의 위치 갱신은 최신 값으로 병합한다. 주기적 재동기화로 누락 상태를 복구한다.

### 4.2 물리 규칙

| 상황 | 기본 반응 |
|---|---|
| 창 상단 | 착지 / 걷기 / 앉기 |
| 창 좌우 | 충돌 / 등반(캐릭터 속성에 따라) |
| 창 하단 | MVP에서는 비충돌 |
| 창 이동 | 해당 창에 부착된 캐릭터가 상대 좌표 유지 |
| 창 종료·최소화 | 부착 해제 후 낙하 |
| 창 크기 변경 | 접촉 지점 재검증 / 부착 해제 가능 |
| 다른 창에 가려진 표면 | 노출 구간만 충돌 대상으로 고려(단계적 정밀화) |
| 화면 하단 | 월드 바닥 또는 작업 표시줄 기준으로 착지 |
| 화면 밖·모니터 분리 | 가장 가까운 유효 영역으로 안전 복귀 |
| 캐릭터 간 충돌 | 초기는 미지원, 향후 옵션 |

- 물리는 고정 시간 간격(활동 시 목표 30~60Hz), 렌더링은 변경될 때만 갱신한다.
- 창 부착 상태는 `WindowId + 로컬 x 오프셋 + 접촉면`으로 관리한다.
- 창의 Z-order와 실제 보이는 표면 일치 여부는 빠른 드래그·겹침 테스트에서 검증한다.
- 3D 캐릭터는 `foot_anchor`를 화면 2D 좌표에 투영해 같은 물리 시스템을 사용한다.

### 4.3 전체화면 게임 숨김

- 전경 창이 해당 모니터를 거의 전부 덮는지, 사용자 앱 분류 규칙에 일치하는지 조합해 판정한다.
- 일반적인 전체화면 브라우저·동영상과 게임을 완벽히 자동 분류할 수 없으므로 사용자별 예외·강제 숨김 설정을 둔다.
- 기본값은 **게임이 실행 중인 모니터의 캐릭터만 숨기기**. 선택적으로 전체 모니터 숨김을 지원한다.
- 숨김과 함께 Present·물리·관찰·TTS의 불필요한 작업을 중지한다. 복귀 시 시뮬레이션 누적시간은 버린다.
- 화면 공유, 잠금, 절전, 해상도 변경 등 상태 전환 테스트가 필요하다.

## 5. 렌더링 및 입력 설계

### 5.1 렌더링

- 공통 D3D11 Device와 DirectComposition 출력 경로를 검증한다. 스프라이트는 텍스처/Atlas 기반으로 구현한다.
- 말풍선과 UI 텍스트는 Direct2D/DirectWrite를 활용한다.
- 모든 캐릭터는 공통 행동(`idle`, `walk`, `sit`, `climb`, `fall`, `drag`, `talk`)과 감정 상태를 받는다.
- 렌더러는 행동의 시각적 표현만 구현한다. 모델 종류별 미지원 애니메이션은 기본 동작으로 대체한다.
- 캐릭터 애셋은 해시/경로 기반으로 공유하고, GPU Device Lost 상황에서 안전하게 복구한다.
- 창 표시는 GPU 투명 합성과 별도로 Windows 입력 판정을 다룬다. **투명 픽셀이 자동으로 클릭 통과된다고 가정하지 않는다.**

### 5.2 마우스 및 대화

| 이벤트 | 처리 |
|---|---|
| Hover | 커서 추적 / 시선·표정 전환 |
| Click | 말풍선 또는 캐릭터 대화 UI 열기 |
| Right click | 컨텍스트 메뉴 |
| Drag | 창 부착 해제 → 위치 추적 → 놓기 시 중력 복구 |
| Double click | 캐릭터별 동작 매핑 |
| Hotkey | `RegisterHotKey` 사용; 후보 `Ctrl+Alt+P`, 충돌 시 사용자 변경 |

캐릭터 오버레이는 기본적으로 키보드 포커스를 취하지 않는다. 실제 텍스트 입력은 별도의 대화 창에서 처리한다. 드래그 임계값, 마우스 근접 탐지 주기, 클릭 영역은 설정 가능하도록 만든다.

### 5.3 3D 확장 전략

- 첫 단계에서는 2D Sprite Renderer만 완성한다.
- 다만 초기 기술 검증에서 단순 3D 삼각형 또는 테스트 모델의 투명 오버레이 출력까지 확인한다.
- 향후 glTF 2.0 → VRM 1.0 순으로 로더 및 전용 표현(MToon·Expression·LookAt·Spring Bone 등)을 추가한다.
- 3D에는 직교 카메라를 기본으로 하고, 캐릭터 발의 화면 좌표와 2D 충돌체를 일치시킨다.
- Live2D Cubism SDK 및 캐릭터 모델 배포 라이선스는 별도 검토한다.

## 6. 캐릭터 패키지 규격

### 6.1 폴더 구조

```text
characters/
  sample-character/
    manifest.toml
    persona.md
    render/
      sprite/atlas.png
      sprite/animations.toml
      live2d/                   # 선택
      3d/                       # 선택
    behaviors/
      behavior.toml
      custom.lua
    ai/rules.toml
    voice/voice.toml
```

### 6.2 구성 원칙

- `manifest.toml`에 스키마 버전, 고유 ID, 버전, 지원 렌더러 및 파일 경로를 선언한다.
- 행동 스크립트는 Lua를 사용하고 기본 행동을 상속/재정의한다.
- `persona.md`는 발화 성격·말투·선호 행동을 지정하지만 권한 제어에는 영향을 주지 않는다.
- 캐릭터 패키지는 읽기 전용 애셋으로 취급한다. 인스턴스 위치·대화·사용자 설정은 `%LOCALAPPDATA%`에 별도 저장한다.
- 누락된 필수 이미지/행동은 검증 오류로 표시하고, 안전한 기본 캐릭터로 폴백한다.
- 로딩 시 크기 제한·경로 정규화·압축 해제 경로 검증·스크립트 권한 제한을 적용한다.
- 배포 애셋마다 라이선스/출처를 메타데이터로 기록한다.

### 6.3 다중 캐릭터 데이터 모델

```cpp
struct PetInstance {
    PetId id;
    CharacterAssetId asset;
    Transform2D transform;
    PhysicsState physics;
    BehaviorState behavior;
    AnimationState animation;
    ConversationId conversation;
};
```

`PetInstance`에는 텍스처·오디오·모델 바이너리를 직접 담지 않는다. `ResourceManager`에서 캐릭터 패키지별로 참조 공유한다. Windows 창 감시, D3D11 Device, AI 인증, TTS 재생 큐도 프로세스 수준에서 공유한다.

## 7. AI·SIWC·화면 관찰

### 7.1 AI 실행 흐름

```text
사용자 질문 / 승인된 관찰 트리거
 → Privacy Guard(캡처 범위·사용량·권한)
 → ScreenObserver(필요 시 이미지 1프레임)
 → Context Builder(페르소나·최근 대화·제한된 화면)
 → IAiProvider(SIWC / 일반 API / 로컬 모델)
 → Response Validator(스키마·권한·대상 확인)
 → 말풍선 / 선택적 TTS / 행동 명령
```

- 걷기, 충돌, 클릭 반응 등 짧은 동작에는 AI를 호출하지 않는다.
- 자율 발화는 기본 꺼짐이며 사용자가 캡처 범위와 빈도를 명시적으로 승인한다.
- **초기 제안값:** 관찰 시도 15~30분 간격, 자율 발화 시간당 최대 2회. 실제 발화는 AI 판단 및 방해 최소화 규칙에 따라 생략 가능하다.
- 게임 전체화면·잠금·프라이버시 일시정지 상태에서는 화면 관찰을 중지한다.
- 동일 화면 반복 분석을 피하고, 공통 관찰 결과를 캐릭터들이 공유할 수 있게 하되 각 캐릭터의 발화·대화는 분리한다.
- 스크린샷은 로컬 임시 버퍼에서 크기 축소 후 전송하고 기본적으로 디스크에 저장하지 않는다.
- 시스템 화면의 텍스트는 신뢰할 수 없는 입력으로 취급해 프롬프트 인젝션 방어를 적용한다.

### 7.2 SIWC 기술 제약 및 구현

- 공개 오픈소스 로컬 앱은 OpenAI의 공식 **Sign in with ChatGPT** 흐름에서 해당 사용자의 승인을 받아 적격 AI 요청을 실행한다.
- PKCE·state·nonce·고유 `ext_agent_host_id`를 사용하고, 최초 등록 후 받은 `client_id`는 계정/워크스페이스 맥락에 맞게 보관한다.
- SIWC 플랜 사용은 **지원되는 공개 Responses API** 경로로만 구현한다. HTTP 요청에 `store:false`, `stream:true`를 적용하고 이벤트 스트림을 완료까지 처리한다.
- 모델별 이미지 지원 여부를 확인한다. 현재 문서의 제한에 따라 해당 경로의 이미지 생성·네이티브 Computer Use·호스티드 MCP 등 미지원 기능을 사용하지 않는다.
- OAuth 토큰은 사용자 로컬의 보호된 저장소에 보관하고, 외부 플러그인·스크립트·로그·공개 API에 제공하지 않는다.
- 사용자가 승인한 **데스크탑 펫 기능** 외 다른 프로그램의 범용 API 프록시로 SIWC를 사용하지 않는다.
- 명시적으로 승인된 백그라운드 화면 관찰만 수행하며, 로그인 취소·권한 해제·사용량 소진·토큰 갱신 오류 복구 UI를 제공한다.
- SIWC 기능은 지원 요금제·모델·정책 변화에 영향을 받으므로 실제 연동 전 최신 공식 문서를 다시 확인한다.

### 7.3 AI 행동 응답 표준

```json
{
  "should_speak": true,
  "text": "작업이 꽤 길어졌네.",
  "emotion": "curious",
  "action": {
    "type": "look_at_window",
    "target_window_id": "window_12"
  }
}
```

이 JSON은 앱 내부 표준 명령이며 SIWC 응답 원본 형식을 의미하지 않는다. 실행 전 허용된 행동인지, 대상 창이 존재하는지, 길이 제한을 넘지 않는지 검사한다. 화면에 나타난 임의 텍스트가 권한을 부여하거나 행동을 직접 실행할 수 없도록 한다.

### 7.4 TTS 및 말풍선

- 말풍선은 모든 캐릭터에 기본 제공하고, 사용자 입력창은 별도 포커스 가능 창으로 둔다.
- TTS는 기본 OFF이며 Windows 로컬 TTS를 첫 번째 Provider 후보로 한다. 외부 음성 엔진은 플러그인으로 추가한다.
- 다중 캐릭터 음성은 하나의 출력 큐를 공유한다. 기본은 동시 재생 방지, 말풍선은 각각 표시한다.
- 장기적으로 3D VRM 표정·립싱크를 지원할 수 있도록 음소/타임스탬프 인터페이스를 확보한다.

## 8. 성능·스레드·전력 정책

### 8.1 실행 방식

- UI 메시지 루프, 렌더링, 물리 처리는 필요 최소한의 스레드에서 관리한다. 성능 검증 전에는 스레드를 과도하게 늘리지 않는다.
- 파일 로딩·이미지 전처리·AI 네트워크는 제한된 백그라운드 작업 큐로 실행한다.
- EcoQoS는 비실시간 스레드에 `SetThreadInformation(ThreadPowerThrottling)`으로 적용한다. E코어 배치는 권장 힌트일 뿐 보장되지 않는다.
- 사용하지 않는 모듈(3D/TTS/AI)은 초기화하지 않고, 정적 캐릭터는 렌더링을 중단한다.
- 게임 전체화면 숨김 상태와 화면 잠금 시에는 애니메이션·관찰 작업을 중지하고 복귀 시 정상 재개한다.

### 8.2 측정 기준 및 목표(미검증)

| 측정 항목 | 초기 목표 | 측정 방식 |
|---|---|---|
| 1마리 유휴 CPU | 전체 CPU 기준 평균 0.2% 이하 | 10분 이상 평균; 앱 CPU 시간 기록 |
| 1마리 유휴 RAM | Working Set 30~60MB 지향 | Working Set·Private Bytes 모두 기록 |
| 1마리 일반 애니메이션 CPU | 전체 CPU 기준 평균 1% 이하 | 30FPS 스프라이트 동작 중 기록 |
| 정지 렌더링 | 상태 변화가 없다면 0 FPS | Present 횟수 측정 |
| 커서/클릭 반응 | 사용자 체감 지연 최소화 | 입력 이벤트→시각 반영 지연 측정 |
| 다중 캐릭터 | 1/3/5마리 증가량 측정 | CPU, RAM, VRAM, Present 비교 |
| 전체화면 게임 | 숨김 시 불필요한 GPU 작업 최소화 | 게임 실행 전후 ETW/GPUView 등 비교 |

위 수치는 제품 목표이지 검증된 성능 수치가 아니다. 특히 DirectComposition/D3D11과 다중 캐릭터 사용 시 30~60MB 목표 달성 여부를 실측으로 판단한다. CPU 점유율과 별도로 CPU 누적 시간·프레임 시간·GPU 비용을 함께 추적한다.

## 9. 프로젝트 구조

```text
desktop-pet/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  apps/desktop-pet/
  src/
    core/                     # EventBus, Scheduler, Config, Permissions
    character/                # PetManager, Loader, ResourceManager
    platform/windows/         # Win32, DPI, WindowObserver, Input, CPU QoS
    physics/                  # 2D World / Colliders / Attachment
    behavior/                 # FSM, Lua
    rendering/                # IRenderer, Sprite, D3D11, Composition
    capture/                  # Windows.Graphics.Capture
    ai/                       # Orchestrator, SIWC, provider adapters
    speech/                   # Bubble, TTS, Audio queue
    plugins/                  # 스크립트 호스트, 모듈 등록
    ui/                       # 설정, 트레이, 대화창
  characters/example/
  schemas/
  tests/
  docs/
  .github/workflows/
```

### 9.1 확장 인터페이스

- `IRenderer`: 애셋 로드, 상태 갱신, Draw, Device 재생성, 언로드.
- `IBehavior`: 이벤트 소비, 행동 후보 제시, 우선순위/취소 처리.
- `IAiProvider`: 기능 탐색(텍스트·이미지·도구), 요청·스트림·취소, 오류 매핑.
- `ITtsProvider`: 지원 음성 조회, 합성·취소, 음성 타이밍/립싱크 메타데이터.
- `IScreenCapture`: 동의된 캡처 대상, 1프레임 획득, 세션 종료.
- 공개 바이너리 플러그인에는 C++ STL 타입을 ABI 경계로 노출하지 않는다. 서드파티 네이티브 플러그인은 버전 지정 C ABI 또는 별도 프로세스 IPC를 채택한다.

## 10. 개발 단계와 완료 기준

| 단계 | 주요 구현 | 완료 판정(Exit Criteria) |
|---|---|---|
| P0 · 기술 검증 | 투명 D3D11 오버레이, DComp, 입력 통과, 3D 테스트 폴리곤 | 일반 창 클릭 통과·DPI·투명 합성이 정상 동작 |
| P1 · 핵심 MVP | Win32 창 감시, 2D 물리, 1마리 스프라이트, 드래그·클릭 | 창 이동/최소화/닫힘 시 비정상 상태 없이 동작 |
| P2 · 캐릭터 시스템 | 패키지 로더, FSM, Lua, 3~5마리, 리소스 공유 | 코드 수정 없이 캐릭터 교체 및 다중 실행 가능 |
| P3 · AI 대화 | SIWC 로그인, 대화창, 말풍선, 전역 단축키 | 승인·대화·스트림·로그아웃·오류 복구 검증 |
| P4 · 화면 관찰/TTS | WGC, Privacy Guard, 자율 발화, 로컬 TTS | OFF 기본값·동의·예외·중지·개별 발화 검증 |
| P5 · 3D 확장 검증 | glTF 테스트, 직교 카메라, 공통 행동/충돌 연결 | 기존 물리/행동 코어 변경 없이 테스트 3D 동작 |
| P6 · 공개 배포 | 설치/Portable, 업데이트, 라이선스, CI, 문서 | 설치·삭제·업데이트·보안·성능 회귀 테스트 통과 |

**우선순위:** P0를 최우선으로 수행한다. P0에서 투명 입력 통과, D3D11/DComp와 GPU 자원 비용을 실측해 설계를 고정한다. P5의 전체 구현은 뒤로 두더라도 간단한 3D 표시 검증은 P0에 포함한다. 일정은 P0 측정 이후 추정한다.

## 11. 테스트 전략 및 위험 관리

### 11.1 테스트 매트릭스

| 범주 | 핵심 시나리오 | 허용 기준 |
|---|---|---|
| 입력 | 투명 픽셀 통과, 캐릭터 클릭/드래그, 다른 앱 입력 | 다른 앱 조작 방해 없음 |
| 창 | 겹침, 빠른 이동·리사이즈, 최소화, 관리자 창 | 크래시·무한 낙하·유령 충돌 없음 |
| 모니터 | 100%/150% 배율, 음수 좌표, 연결/해제 | 캐릭터 좌표 정상 복원 |
| 게임 | 독점/테두리 없는 전체화면, F11 브라우저 | 자동 숨김과 예외 설정 동작 |
| 렌더링 | 2D 1/3/5마리, Device Lost, 프레임 제한 | 누수·검은 사각형·잔상 없음 |
| AI | 동의 취소, 통신 오류, 사용량 제한, 장시간 응답 | 렌더/물리 비차단, 안전한 오류 안내 |
| 캡처 | 앱 제외, 잠금, 스크린샷 즉시 폐기 | 무허가 저장·전송 없음 |
| 패키지 | 누락 파일, 경로 이탈, 악성 Lua, 큰 이미지 | 검증 실패·권한 제한·안전한 폴백 |
| 성능 | idle 10분, 동작 10분, 게임 병행 | 측정 로그 작성 및 목표 비교 |

### 11.2 주요 위험 및 대응

| 위험 | 가능 영향 | 대응 |
|---|---|---|
| DComp의 시각적 투명과 입력 통과 불일치 | 클릭 방해 | P0에서 전용 Hit Test 프로토타입 |
| Z-order·노출 표면 정확성 | 가려진 창 위 부자연스러운 착지 | 단계별 노출 구간 계산 및 예외 처리 |
| 전체화면 게임 감지 오탐 | 불필요한 펫 숨김 | 앱별 설정·수동 토글·로그 |
| D3D11 메모리/Present 비용 | 목표 자원 초과 | 공유 리소스·작은 Surface·정지 렌더 중단 |
| SIWC 정책·기능 변경 | 로그인·추론 실패 | 독립 Provider, 기능 협상, 최신 문서 확인 |
| 화면 프롬프트 인젝션 | 원치 않는 행동 | 외부 화면 텍스트 불신, 명령 검증, 권한 분리 |
| 서드파티 캐릭터/SDK 라이선스 | 공개 배포 제약 | 코드/캐릭터/SDK 라이선스 분리 고지 |

## 12. 오픈소스 운영 및 릴리스

- 초기 배포는 GitHub Releases의 Portable ZIP을 우선한다. 설치 프로그램과 자동 업데이트는 안정화 이후 추가한다.
- CMake Presets와 vcpkg manifest로 개발 환경을 재현 가능하게 유지한다.
- Windows CI에서 빌드·단위 테스트·패키지 검증·정적 분석을 수행한다.
- C++에서는 RAII, `std::unique_ptr`, COM 스마트 포인터를 사용하며, AddressSanitizer/UBSan 가능 범위를 테스트한다.
- 사용자 데이터·로그·크래시 덤프에서 OAuth 토큰, 화면 이미지, 대화 원문을 기본 수집하지 않는다.
- 코드 라이선스(MIT/Apache-2.0 중 선택), 기여 규칙, 개인정보 고지, 보안 취약점 제보 절차를 릴리스 전 확정한다.
- 캐릭터 에셋, 음성 모델, Live2D SDK 등은 코어 라이선스와 독립적으로 표기한다.

## 13. 바로 실행할 작업(첫 개발 사이클)

1. C++20·CMake·MSVC·vcpkg 기반 최소 Windows 실행 파일 및 트레이 구성을 만든다.
2. 투명 D3D11/DirectComposition 오버레이 창에 테스트 스프라이트를 표시한다.
3. 실제 일반 창 뒤쪽 클릭이 통과하는지 검증한다. 실패 시 오버레이 윈도우 전략을 재검토한다.
4. 혼합 DPI 및 두 모니터 환경에서 캐릭터 화면 좌표를 검증한다.
5. `SetWinEventHook`으로 창 이동 이벤트를 받아 스프라이트를 창 위에 부착한다.
6. 유휴/30FPS 동작/3마리 확장의 CPU·Working Set·GPU 측정을 기록한다.
7. 단순한 3D 테스트 도형을 동일 합성 경로에 표시해 3D 확장 경로를 확인한다.

**첫 검증 산출물:** 실행 파일, P0 측정 로그, 알려진 OS/그래픽 제약 목록, 렌더링/입력 방식의 최종 ADR.

## 14. 추가 결정이 필요한 사항

| 의사결정 | 추천 방향 | 확정 시점 |
|---|---|---|
| 오버레이 창 전략 | 캐릭터별 작은 창 우선, 다중 캐릭터 측정 후 통합 검토 | P0 |
| 정밀한 창 가림 계산 | MVP는 상단 노출 구간 중심 | P1 |
| 기본 캐릭터팩 라이선스 | 독자 제작 애셋을 기본 배포 | P2 이전 |
| 캐릭터 간 충돌·협동 행동 | 비활성으로 시작 | P2 이후 |
| AI 기억 저장 기간 | 최근 이력 중심, 장기 기억은 Opt-in | P3 |
| 기본 TTS 엔진/목소리 | Windows 로컬 엔진, 엔진 교체 가능 | P4 |
| 외부 네이티브 플러그인 정책 | 초기 내장 모듈 + Lua, 이후 C ABI/IPC | P6 이후 |
| 3D 공식 지원 범위 | glTF 기초 → VRM 1.0 기능별 확대 | P5 |
| 리포지토리·릴리스 라이선스 | MIT 또는 Apache-2.0 및 별도 에셋 라이선스 | P6 이전 |

## 15. 공식 문서 및 참고 자료

아래 자료는 2026-10-13 기준 확인한 공식 문서입니다. API·요금제·허용 기능 등 변동 가능한 항목은 실제 구현 시 재확인합니다.

1. OpenAI, *Sign in with ChatGPT – Open-source plan usage overview*: https://developers.openai.com/siwc/token-sharing-open-source
2. OpenAI, *Registration and sign-in*: https://developers.openai.com/siwc/token-sharing-open-source/sign-in
3. OpenAI, *Models and inference*: https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference
4. OpenAI, *Preview limitations*: https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations
5. OpenAI, *SIWC Terms* (2026-09-29): https://openai.com/policies/sign-in-with-chatgpt-terms/
6. Microsoft, *DirectComposition*: https://learn.microsoft.com/en-us/windows/win32/directcomp/directcomposition-portal
7. Microsoft, *CreateSwapChainForComposition*: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition
8. Microsoft, *SetWinEventHook*: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook
9. Microsoft, *Screen capture*: https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture
10. Microsoft, *SetThreadInformation (EcoQoS)*: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadinformation
11. Microsoft, *RegisterHotKey*: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey
12. Microsoft, *SetWindowDisplayAffinity*: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity

---

**기획 기준선 요약:** C++20 단일 프로세스·공유 자원 + Windows 창 기반 2D 물리 + Direct3D 11/DirectComposition 렌더링 + 캐릭터 독립 패키지 + 승인형 화면 인식 AI(SIWC 우선) + 말풍선/선택형 TTS + 향후 Live2D/3D 지원. 가장 먼저 검증할 사항은 투명 오버레이의 실제 입력 통과와 GPU·CPU 비용이다.
