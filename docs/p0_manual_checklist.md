# P0 수동 검증 체크리스트

자동으로 확인할 수 없는 항목(실제 클릭·포커스·체감 품질·게임 병행·장시간 측정)을 사용자가 직접 수행하는 절차다.
결과는 각 항목의 **기록** 칸 기준으로 `docs/p0_report.md`에 옮긴다.

- 계획서: [desktop_pet_p0_detailed_plan_ko.md](plan/desktop_pet_p0_detailed_plan_ko.md)
- 실행 파일: `build\x64-release\p0-spike.exe` (빌드: `powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset x64-release`)
- 실행 로그: `build\x64-release\logs\p0-spike_*.log` (E1 클릭, DPI 변경, WinEvent, GPU 재생성이 기록됨)
- 모든 실행은 **트레이 아이콘 → 종료**로 끝낼 수 있다. 펫 우클릭으로도 같은 메뉴가 열린다.

## 0. p0-spike 주요 옵션

| 옵션 | 값 | 설명 |
|---|---|---|
| `--hittest=` | `A` / `B` / `C` / `none` | 클릭 통과 방식 (계획서 §3.3). 기본 A |
| `--overlay=` | `small` / `single` | 펫별 소형 창 / 화면 전체 단일 오버레이 |
| `--content=` | `swapchain` / `surface` | DComp 콘텐츠 방식 (E3) |
| `--timer=` | `hires` / `msgwait` / `settimer` | 30fps 타이머 방식 (E4) |
| `--pets=N` `--anim=FPS` `--move` | | 마리 수, 애니메이션 fps(0=정지), 좌우 이동 |
| `--proximity=on/off` | | 커서 근접 반응 (기본 on) |
| `--cube` `--text` | | 3D 큐브(E8), D2D 텍스트(P0-11) |
| `--follow-delay=N` | | N초 후 전경 창을 추종 (트레이 메뉴로도 가능) |
| `--recreate-at=N` | | N초 후 GPU 재생성 시뮬레이션 (트레이 메뉴로도 가능) |
| `--gpu=off` | | GPU 장치 미생성 (S0-A, `--pets=0` 전용) |
| `--debug-layer=on/off` | | D3D11 debug layer (Debug 빌드 기본 on, Release 기본 off) |
| `--duration=N` `--warmup=N` `--log=csv` | | 측정 길이·안정화·CSV |

제약: `hittest=C`는 `small`·swapchain 전용(큐브/텍스트 없음), `content=surface`는 큐브 불가, `overlay=single + hittest=none`은 10초 이하에서만 실행된다(데스크탑 입력을 모두 가로채므로).

## 1. E1 — 클릭 통과 (P0-05)

준비: 메모장 1개, 작업 관리자(관리자 권한 창) 1개를 펫 뒤에 겹치게 둔다.

방식별로 아래 명령을 실행하고 1-1~1-6을 확인한다.

```bash
build/x64-release/p0-spike.exe --hittest=A --pets=1 --text
```
```bash
build/x64-release/p0-spike.exe --hittest=B --pets=1 --text
```
```bash
build/x64-release/p0-spike.exe --hittest=C --pets=1
```
```bash
build/x64-release/p0-spike.exe --hittest=none --pets=1 --duration=60
```

| # | 확인 | 기록 (A / B / C / none) |
|---|---|---|
| 1-1 | 펫 **투명 영역**(귀 사이, 몸 옆) 클릭이 뒤의 메모장에 전달되는가 | 예/아니오 |
| 1-2 | 투명 영역 위 휠 스크롤·드래그(텍스트 선택)가 메모장에 전달되는가 | |
| 1-3 | 관리자 권한 창(작업 관리자) 위에서도 1-1이 되는가 | |
| 1-4 | 펫 **불투명 영역** 클릭 시 펫이 점프하고, 메모장 포커스가 유지되는가(타이핑이 계속 메모장에 입력) | |
| 1-5 | 펫 드래그가 끊김 없이 따라오는가, 놓은 위치에 남는가 | |
| 1-6 | 가장자리(반투명 테두리)·텍스트 라벨 부분 클릭 결과, 깜빡임 유무 | |
| 1-7 | (A 전용) 멀리서 커서를 **빠르게** 펫으로 옮겨 즉시 클릭 — 첫 클릭이 뒤 창으로 새는가. 새면 `--poll-far=50` 등으로 바꿔 재확인하고 값을 기록 (메커니즘 자체 문제인지 확인 주기 문제인지 구분) | |

- 로그의 `E1 클릭 ... 불투명=false` 줄은 **투명 픽셀 클릭을 펫이 받았다**는 뜻(통과 실패)이다. 실행 종료 시 `요약: clicks_transparent=` 값도 기록한다.
- 예상(가설): `none`은 1-1 실패(창 사각형 전체가 입력을 받음). `B`는 Region 밖 콘텐츠(텍스트 라벨, 알파 32 미만 가장자리)가 **화면에도 그려지지 않는다**(자동 캡처로 확인됨).

## 2. E5 — DPI (P0-06, 단일 모니터 범위 D10)

```bash
build/x64-release/p0-spike.exe --pets=2 --text
```

1. 실행 중 **설정 → 디스플레이 → 배율**을 100% → 125% → 150% → 100%로 바꾼다.
2. 단계마다 확인: 펫 크기가 배율에 맞게 커지는가, 발이 작업 표시줄 위에 붙어 있는가, 텍스트가 흐리지 않은가, 잔상·검은 사각형이 없는가.
3. 해상도를 한 번 바꿨다 되돌린다. 작업 표시줄 자동 숨김을 켰다 끈다.
4. 로그의 `WM_DPICHANGED`, `DPI 변경`, `WM_DISPLAYCHANGE`, `SPI_SETWORKAREA` 줄을 보고서에 첨부한다.

| # | 확인 | 기록 |
|---|---|---|
| 2-1 | 배율 단계별 크기·위치 정상 | |
| 2-2 | 해상도 변경 후 복원 | |
| 2-3 | 작업 표시줄 변경 후 발 위치 | |

## 3. P0-07 / S4 — 창 추종

```bash
build/x64-release/p0-spike.exe --pets=1 --log=build/x64-release/s4_follow.csv
```

1. 트레이 메뉴 → **창 추종 시작 (3초 후 전경 창)** → 3초 안에 메모장(최대화하지 않은 상태)을 클릭해 전경으로 둔다.
2. 펫이 메모장 상단에 붙으면: 메모장을 빠르게 드래그(2분), 최소화 → 복원, 크기 변경, 닫기 순서로 진행한다.

| # | 확인 | 기록 |
|---|---|---|
| 3-1 | 빠른 드래그 중 펫이 창을 따라오는 지연·떨림 체감 | |
| 3-2 | 최소화 시 작업 영역 하단으로 이동, 복원 시 재부착 | |
| 3-3 | 창 닫기 후 크래시·유령 위치 없음 | |
| 3-4 | 로그 `요약: follow_lat_p50/p95`, CSV `notes`의 `we_raw/we_acc` | |

알려진 한계(P1 범위): 최대화된 창은 상단이 화면 맨 위라 펫이 화면 밖으로 나간다. 드롭 후 중력 없음.

## 4. P0-02 — 트레이

| # | 확인 | 기록 |
|---|---|---|
| 4-1 | 트레이 아이콘 좌클릭/우클릭 시 메뉴, 바깥 클릭 시 메뉴 닫힘 | |
| 4-2 | 작업 관리자에서 Windows 탐색기 다시 시작 → 아이콘 복구 (로그 `TaskbarCreated 수신`) | |
| 4-3 | 트레이 종료 후 프로세스·아이콘 잔류 없음 | |

## 4A. 숨김·화면 잠금 시 타이머 해제 (D14)

```bash
build/x64-release/p0-spike.exe --pets=1 --anim=30 --log=build/x64-release/suspend.csv
```

1. 30초 실행 → 트레이 **펫 숨기기** → 30초 → **펫 보이기**.
2. `Win+L`로 잠금 → 30초 후 잠금 해제.
3. 트레이 종료 후 CSV에서 `suspended=1` 구간의 `wakeups_delta`(측정 샘플 1회/초만 남아야 함)와 `present_count_delta`(0이어야 함)를 확인한다. 로그의 `일시정지 (tray)`, `일시정지 (session lock)`, `재개` 줄도 기록한다.

| # | 확인 | 기록 |
|---|---|---|
| 4A-1 | 숨김 중 Present 0, wake-up 1/초 이하 | |
| 4A-2 | 잠금 중 Present 0, wake-up 1/초 이하 | |
| 4A-3 | 복귀 후 애니메이션이 튀지 않고 재개 | |

## 5. E6 / S5 — Mindustry 병행

PresentMon 2.6.0 CLI는 `tools\external\presentmon\PresentMon-2.6.0-x64.exe`에 받아 두었다(Intel 서명 확인). PresentMon은 ETW 세션을 쓰므로 **관리자 권한 터미널**에서 실행한다(또는 계정을 Performance Log Users 그룹에 추가).

1. Mindustry를 실행하고 설정에서 **전체화면**(또는 테두리 없는 창)으로 둔다. 게임 내 FPS 표시가 있으면 켠다.
2. 작업 관리자에서 실제 렌더링 프로세스 이름을 확인한다(`Mindustry.exe` 또는 `java(w).exe`). 다르면 `-GameProcess`로 지정한다.
3. 게임 화면이 뜬 상태에서 **관리자 터미널**로 실행:

```bash
powershell -ExecutionPolicy Bypass -File tools/measure/run_scenarios.ps1 -Only S5 -GameMode fullscreen -PresentMonPath tools/external/presentmon/PresentMon-2.6.0-x64.exe
```

4. 측정 중에는 게임을 같은 화면(예: 메뉴 또는 고정 맵)에 둔다. 모드를 바꿔 `-GameMode borderless`로 반복한다.

| # | 확인 | 기록 |
|---|---|---|
| 5-1 | 펫 표시 시 게임 위에 펫이 보이는가 (전체화면 모드에서 가려질 수 있음) | |
| 5-2 | 펫 숨김/정지/애니메이션 조건별 게임 FPS 체감·표시값 | |
| 5-3 | 게임 프로세스 이름 | |
| 5-4 | `*_presentmon.csv`가 생성되었는가 (권한 문제 확인) | |

## 6. E7 — Device Lost (보류, D17·D18)

자동 점검은 앱 내부 **GPU 재생성 시뮬레이션**까지만 했다. **실제 Device Lost 실험은 보류**이며 완료로 처리하지 않는다. `dxcap -forcetdr`는 아직 승인되지 않았다.

| # | 확인 | 기록 |
|---|---|---|
| 6-1 | 트레이 → GPU 재생성 시뮬레이션 반복 10회 후 로그의 handles/gdi/private 증가 여부 | |
| 6-2 | (별도 승인 후) `dxcap -forcetdr` 후 자동 복구 | 보류 |

## 6A. Graphics Tools 설치 (D18 승인, 관리자 권한 필요)

D3D11 debug layer와 `dxcap`이 포함된 Windows 선택적 기능이다. **장시간 측정 시작 전에** 관리자 PowerShell에서 실행한다(설치 중 CPU 부하가 측정을 오염시키므로).

```powershell
Add-WindowsCapability -Online -Name Tools.Graphics.DirectX~~~~0.0.1.0
```

설치 후 Debug 빌드 로그에 `debug_layer=true`가 찍히면 성공이다. **debug layer가 켜진 실행값은 정식 성능 판정에 쓰지 않는다**(D19, `summarize.ps1`이 자동 제외). Release 빌드와 측정 스크립트는 기본으로 debug layer를 끈다.

## 7A. 핵심 시나리오 단축 측정 (예비 판정용, D20)

10개 시나리오 × 3회 × 60초, 약 38분. 펫은 화면 **좌상단**(작업 영역 왼쪽 위, 5마리 기준 가로 약 750px·세로 약 180px 구역)에 뜬다. 측정 중 커서는 화면 오른쪽 아래에 두고 PC를 쓰지 않는다.

```bash
powershell -ExecutionPolicy Bypass -File tools/measure/run_scenarios.ps1 -Core
```

결과 폴더 이름 끝에 `_core`가 붙는다. 이 결과는 설계 방향 비교(E1·E2·E4)용이며 정식 판정은 §7로 한다.

## 7. 장시간 측정 (S0-A/B, S1~S3, S6)

```bash
powershell -ExecutionPolicy Bypass -File tools/measure/run_scenarios.ps1
```

- 기본: 시나리오별 10분 + 안정화 2분, 3회 반복. 예상 시간이 시작 시 출력된다(20개 시나리오, 약 12.2시간).
- 측정 중에는 PC를 사용하지 않는다(커서가 펫 근처에 가면 근접 반응으로 수치가 바뀜). 전원 모드·백그라운드 앱을 고정한다.
- **세션을 잠그지 않는다**(P0는 잠금/절전 대응이 없음, P1-08). 스크립트는 실행 중 디스플레이 꺼짐·절전만 막는다(`SetThreadExecutionState`, 스크립트 종료 시 해제).
- 일부만: `-Only S1,S2`. 회차 축소: `-Runs 3 -DurationSec 300` 등(계획서 기준은 10분).
- 완료 후 요약:

```bash
powershell -ExecutionPolicy Bypass -File tools/measure/summarize.ps1 -Dir logs/p0/<시각>
```
