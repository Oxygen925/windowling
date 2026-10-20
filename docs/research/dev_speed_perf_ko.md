# 개발 속도·성능 개선 참고 조사

**작성일:** 2026-10-14
**상태:** 후보 목록. 채택 여부는 정하지 않았다(사용자 결정).
**방법:** 우리 프로젝트에서 실제로 시간이 드는 곳을 먼저 쟀다(빌드·테스트·런타임). 그다음 그 지점에 맞는 참고 자료를 찾았다. 성능 효과는 모두 **추정**이며, 채택하면 기존 측정 스크립트로 전후를 비교한다.

---

## 1. 지금 시간이 드는 곳 (측정값)

### 1.1 빌드 (Release, 이 PC, 2026-10-14)

| 상황 | 시간 | 비고 |
|---|---|---|
| 아무것도 안 바뀐 빌드 | **18.7초** | `tools/build.ps1` 이 매번 `vcvars64`(약 6초)와 `cmake --preset`(configure + vcpkg 확인 약 7초)를 다시 실행 |
| `.cpp` 1개 수정 | 37.9초 | 위 오버헤드 약 19초 + 컴파일·링크 약 19초 |
| `siwc_protocol.h` 수정 (9개 파일이 포함) | 53.6초 | |
| `config.h` 수정 | 33.4초 | |
| 오래 걸린 TU (병렬 빌드 기록) | 50~56초 | `pet_app.cpp`·`pet_app_ai.cpp`·`main.cpp`·`mock-siwc`·`ai_tests.cpp` |

오래 걸린 TU 의 공통점:
- `pet_app.h` 가 거의 모든 헤더를 끌어온다(windows.h·D3D11·DComp·D2D·WIL·toml++·`<format>` 등).
- `siwc_protocol.h` 가 공개 헤더에서 `nlohmann/json.hpp` 를 포함한다. 그래서 AI 와 관계없는 앱 코드도 JSON 헤더 비용을 낸다.

### 1.2 테스트·측정

| 항목 | 시간 | 비고 |
|---|---|---|
| 단위 테스트 47개 | 1초 미만 | |
| 시나리오 P1 / P2 / P3 | 약 20초 / 약 80초 / 약 90초 | 실제 데스크탑과 마우스가 필요해 직렬·무인 실행만 가능 |
| 성능 측정 (60초 × 3회) | 12~22분 | 조건 수 × 76초 |

이번 세션에서 반복된 시간 손실도 있었다:
- 셸 이스케이프(`\n`·`\a` 가 깨짐), PowerShell BOM, CRLF 혼재
- 시나리오 스크립트끼리 도우미 함수(Start-Target·WaitLog·Session)를 복사해 씀

### 1.3 런타임 비용 (P1~P3 측정)

| 조건 | CPU (GetProcessTimes / 사이클) | 기상/초 | Present/초 |
|---|---|---|---|
| 1마리 정지 | 0.005 / 0.004 % | 5.9 | 0 |
| 1마리 걷기 | 0.14 / 0.18 % | 약 78 | 8 |
| 5마리 걷기 | 0.90 / 0.41 % | 약 82 | 40 |
| AI 응답 스트리밍 | 0.27 / 0.23 % | 약 24 | 12 |

- 정지 상태 기상 5.9회/초 = 커서 근접 확인 5회(200ms) + 측정 샘플 1회.
- 걷기 비용은 다음을 합친 값이다. 항목별 비중은 **측정하지 않았다**.
  - 60Hz 물리 기상
  - 30Hz 창 이동(`SetWindowPos`)
  - 8fps 그리기와 Present
  - 프레임이 바뀔 때마다 하는 hit-test Region 재계산(스프라이트 픽셀 스캔 → `SetWindowRgn`)

---

## 2. 개발 속도 후보

| # | 후보 | 근거·참고 | 예상 효과 | 비용 |
|---|---|---|---|---|
| B1 | **빌드 스크립트 빠른 경로:** `build/<preset>/CMakeCache.txt` 가 있고 `vcpkg.json`·`CMakeLists.txt`·`CMakePresets.json` 이 그 뒤로 바뀌지 않았으면 configure 를 건너뛴다. 개발 셸 환경(vcvars)을 한 번만 잡는 `-Shell` 모드도 둔다 | §1.1 측정 | 빌드마다 약 7~13초 절약 (전체의 1/3~2/3) | 낮음 |
| B2 | **미리 컴파일된 헤더(PCH):** `target_precompile_headers` 로 windows.h·d3d11·dcomp·d2d1·wil·`<format>`·`<string>` 등을 묶는다. `dp_core`·`dp_ai`(Windows 금지 계층)는 별도 PCH 로 windows.h 를 넣지 않는다 | [CMake target_precompile_headers](https://cmake.org/cmake/help/v4.1/command/target_precompile_headers.html) | 무거운 TU 컴파일 시간 단축 (크기는 측정으로 확인) | 낮음~중간. `/FI forbid_windows.h` 와 순서 충돌 주의 |
| B3 | **헤더 정리:** `siwc_protocol.h` 의 `nlohmann/json` 을 cpp 로 옮긴다(`Jwt` 는 문자열·파싱 결과만 보관). `pet_app.h` 가 하위 헤더를 덜 끌어오게 전방 선언으로 바꾼다 | §1.1 (JSON 헤더가 앱 TU 로 번짐) | 헤더 하나를 고쳤을 때 다시 빌드되는 범위 축소 | 중간 |
| B4 | **빌드 분석 도구로 확인:** vcperf / C++ Build Insights 로 TU·헤더별 시간을 본 뒤 B2·B3 효과를 숫자로 비교 | [Finding build bottlenecks with C++ Build Insights](https://devblogs.microsoft.com/cppblog/finding-build-bottlenecks-with-cpp-build-insights/), [vcperf](https://source-hub.etos-pro.ru/microsoft/vcperf) | 추측 대신 측정으로 결정 | 낮음 (WPA 설치 필요) |
| B5 | **시나리오 공통 모듈:** `tests/scenario/common.psm1` 에 창 조작·로그 대기·세션 시작·모의 서버 도우미를 모은다. UTF-8 BOM 도 강제한다 | §1.2 (복사·이스케이프 실수 반복) | 새 시나리오 작성이 빨라지고 실수가 준다 | 낮음 |
| B6 | **줄 끝·인코딩 고정:** `.gitattributes`(eol), `.editorconfig`(UTF-8, PowerShell 은 BOM) | 커밋 때마다 LF/CRLF 경고, config.cpp 리터럴 깨짐 사고 | 사고 예방 | 낮음 (기존 파일 줄 끝 정리를 커밋 1개로) |
| B7 | **CI (GitHub Actions, Windows):** 3개 Preset 빌드 + 단위 테스트 + vcpkg 바이너리 캐시. 시나리오·성능은 데스크탑이 필요해 로컬 유지 | [vcpkg binary caching](https://devblogs.microsoft.com/cppblog/vcpkg-accelerate-your-team-development-environment-with-binary-caching-and-manifests/), 기준선 §12 | 회귀를 자동으로 잡는다. P6 준비 | 중간. 원격 저장소 결정 필요 |
| B8 | **컴파일 캐시 sccache** (선택) | [sccache](https://docs.rs/crate/sccache) — MSVC 는 `/Z7`(Embedded) + `CMP0141` 필요 | clean 빌드·Preset 전환 단축 | 중간. 디버그 정보 형식이 바뀐다 |
| B9 | **측정 묶음 실행:** 성능 측정을 한 러너로 묶어 무인 실행(밤·자리 비울 때)하고 요약 표를 자동 생성 | §1.2 (12~22분, 그동안 PC 사용 불가) | 사람이 기다리는 시간 제거 | 낮음 |

---

## 3. 성능 후보

| # | 후보 | 근거·참고 | 예상 효과 | 비용·위험 |
|---|---|---|---|---|
| R1 | **hit-test Region 캐시:** (캐릭터, 프레임, 반전, 크기)별 HRGN 을 만들어 두고 `CombineRgn(RGN_COPY)` 로 복사해 `SetWindowRgn` | 지금은 프레임마다 픽셀을 스캔한다(`AppendOpaqueRects`). 같은 캐릭터 펫끼리 공유할 수 있다 | 걷기·talk 중 CPU 감소. 펫 수가 많을수록 효과 큼 `[측정 필요]` | **낮음.** 캐시 무효화는 크기(DPI) 변경 때만 |
| R2 | **커서 근접을 이벤트 기반으로:** 200ms 폴링 대신, 이미 받고 있는 커서 위치 변경 WinEvent(R5 측정에서 커서가 움직이면 초당 약 62회 기상)로 판정한다. 커서가 멈춰 있으면 기상하지 않는다 | §1.3 정지 기상 5.9회/초 중 5회가 폴링 | 정지 기상 5.9 → 약 1회/초(측정 샘플만, 측정을 끄면 0). 전력에 유리 | 낮음~중간. 이벤트가 오지 않는 경우(보안 데스크탑 등)를 대비해 느린 폴링 1초를 남긴다 |
| R3 | **정확하지 않아도 되는 마감에 타이머 병합 허용:** 동기화 30초, 측정 1초, 행동 결정, 말풍선 숨김 등은 `SetWaitableTimerEx` 의 `TolerableDelay`(예: 32~100ms)로 다른 타이머와 묶이게 한다 | [SetWaitableTimerEx](https://learn.microsoft.com/pl-pl/windows/win32/api/synchapi/nf-synchapi-setwaitabletimerex), [no-wake / 병합 지침](https://learn.microsoft.com/en-sg/WINDOWS-HARDWARE/DRIVERS/kernel/no-wake-timers) (32ms 이상 권장) | 시스템 전체 기상 감소(전력) | 중간. 지금 `MsgWaitForMultipleObjectsEx` timeout 구조를 바꿔야 한다. 물리·애니메이션 마감에는 쓰지 않는다 |
| R4 | **DComp 공유 아틀라스로 프레임 전환:** 캐릭터마다 아틀라스를 `IDCompositionSurface` 1개에 한 번만 그린다. 펫 visual 은 offset·clip·transform(반전·확대, 최근접 보간)만 바꾸고 Commit 한다 → 프레임이 바뀔 때 D3D 그리기·Present 가 없다 | [Composition surface](https://learn.microsoft.com/en-us/windows/win32/directcomp/composition-surface) ("하나 이상의 visual 에 연결 가능"), P0 에서 surface 모드 동작 확인 | 걷기·talk CPU·GPU 크게 감소 가능. 펫마다 swapchain 이 없어져 VRAM 도 줄어듦 `[검증]` | **높음.** 렌더 경로 변경(ADR-02 재검토). 최근접 보간, 창 사이 surface 공유, Device Lost 복구를 P0 수준으로 다시 검증해야 함 |
| R5 | **걷기 중 물리 30Hz:** 화면 반영이 30Hz 이므로 걷기만 할 때는 물리도 30Hz 로 돌린다. 낙하·드래그는 60Hz 유지 | §1.3 걷기 기상 약 78회/초 | 걷기 기상 약 절반 | 낮음. P1-D6(물리 60Hz) 결정 변경이라 사용자 확인 필요 |
| R6 | **말풍선 갱신 묶기:** delta 마다 그리지 않고 최대 10fps | P3 보고서 §4 | 스트리밍 CPU 감소 (현재 0.23~0.27%) | 낮음 |
| R7 | **AI 꺼짐·대화창 미사용 때 메모리:** 대화창을 처음 열 때 만들고, 닫으면 일정 시간 뒤 파괴 | P3 측정 (AI 켬·로그인 +7.8MB) | Working Set 몇 MB 감소 `[측정 필요]` | 낮음 |

---

## 4. 측정·분석 도구 (개선 효과를 판단하는 쪽)

| # | 후보 | 근거 | 용도 |
|---|---|---|---|
| M1 | **단계별 비용 계측:** 물리·행동·렌더·Region·창 이동·Present 각각의 사이클/QPC 누적을 측정 CSV 열로 추가 (개발 빌드만) | §1.3: 걷기 비용 구성을 모름 | R1·R4·R5 를 할지 정할 근거 |
| M2 | **Tracy 프로파일러** (BSD-3-Clause, 개발 빌드 옵션) | [Tracy](https://gitblind.noratr.app/wolfpld/tracy) — 수동 계측형 프레임 프로파일러, MSVC 지원 | 프레임 단위 타임라인. 배포 빌드에서는 빼서 오버헤드 없음 |
| M3 | **CPU 판정 보완 (P2-M1 관련):** 사이클 기준 값을 함께 쓰거나 ETW(WPR/WPA)로 교차 확인 | [QueryProcessCycleTime](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryprocesscycletime), [Raymond Chen — 사이클 시간의 의미](https://devblogs.microsoft.com/oldnewthing/20161021-00/?p=94565) | 틱 단위 회계 편차(P2 에서 확인한 15.625ms 양자) 때문에 작은 개선이 안 보이는 문제 해결 |
| M4 | **고해상도 타이머 근거 재확인** | [Chromium scheduler-dev: high resolution waitable timers](https://groups.google.com/a/chromium.org/g/scheduler-dev/c/0GlSPYreJeY?pli=1), [timeBeginPeriod](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod) | 우리는 이미 `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` 을 쓴다(D14). 전역 타이머 해상도를 올리지 않는 지금 방식이 맞다는 근거 |

---

## 5. 추천 순서 (미확정)

| 순서 | 묶음 | 이유 |
|---|---|---|
| 1 | **B1 + B5 + B6** | 비용이 낮고 매번 체감되는 시간·사고를 바로 줄인다 |
| 2 | **M1 → R1 + R2 + R6** | 먼저 걷기 비용 구성을 재고, 위험이 낮은 개선부터 전후 측정으로 확인한다 |
| 3 | **B2 + B3 (B4 로 측정)** | 컴파일 시간 단축. 효과를 숫자로 확인한다 |
| 4 | **B7 CI** | P6 준비. 원격 저장소 결정이 먼저 필요하다 |
| 5 | **R4 DComp 공유 아틀라스** | 효과가 가장 클 수 있지만 렌더 경로를 바꾸므로 M1 결과를 보고 별도 기술 검증으로 판단 |
| 보류 | R3, R5, B8, M2 | R5 는 P1-D6 결정 변경, R3 은 루프 구조 변경, B8·M2 는 필요할 때 |

## 6. 사용자 결정이 필요한 것

1. 위 후보 중 진행할 묶음과 순서
2. R5: 걷기 중 물리 30Hz (P1-D6 "물리 60Hz" 변경)
3. R4: 렌더 경로 변경을 기술 검증할지 (ADR-02 재검토)
4. B7: CI 를 둘 원격 저장소 (GitHub 등)
5. B6: 저장소 줄 끝 기준 (LF 또는 CRLF)

---

## 7. 진행 결과와 사용자 결정 (2026-10-14)

| 항목 | 결과 / 결정 |
|---|---|
| B6 줄 끝·인코딩 | **확정.** LF + UTF-8, PowerShell 은 LF + UTF-8 BOM, BAT/CMD 는 CRLF. `.gitattributes`·`.editorconfig` 를 추가하고 기존 파일 정규화를 단일 커밋으로 처리 |
| M1 단계별 계측 | 구현. 걷기 비용의 대부분은 창 이동과 `SetWindowRgn` 호출 자체였고, 그리기·Present 는 작았다 |
| R1 Region 캐시 | 구현·**유지**. 픽셀 스캔 비용이 작아 효과는 미미하다 |
| `hit_region = "clip"` | 선택 옵션으로 제공한다(5마리 0.42 → 0.27%). **기본값은 프레임별 정밀 Region 유지** — 다른 프로그램에 대한 클릭 통과 정확성을 CPU 절감보다 우선한다. 다양한 크기·애니메이션에서 클릭 오차를 측정한 뒤 기본값 변경을 다시 검토한다 |
| 프레임 레이트 | `[display] frame_rate` (기본 30, 5~60) 로 조정 가능 |
| R4 DComp 공유 아틀라스 | 기술 검증 결과 명확한 이득이 없어 ADR-02 를 유지하고 **당분간 중단**한다. `exp/dcomp-atlas` 브랜치를 보존하고 master 에 병합하지 않는다. 보고서: [dcomp_atlas_report_ko.md](../exp/dcomp_atlas_report_ko.md). 다중 캐릭터 최적화나 P5 3D 단계에서 필요하면 다시 검토한다 |
| GPU 재생성 후 펫이 보이지 않는 버그 | 검증 중 발견. master 에 수정 반영 |
| B7 CI | GitHub Actions 채택(Windows x64·MSVC·vcpkg 고정, Debug·Release 빌드 + 단위 테스트). 원격은 GitHub Private 로 시작하고, 공개 여부는 따로 정한다. 데스크탑·GPU·SIWC 실계정 검증은 로컬에서 한다 |

### 7.1 CI 첫 실행 확인 (2026-10-14)

- 원격: GitHub Private `Oxygen925/windowling`. master 와 `exp/dcomp-atlas` 를 올렸다.

| 항목 | 결과 |
|---|---|
| 빌드·테스트 | Debug·Release 모두 성공, 단위 테스트 48/48, 컴파일 경고 0 |
| 의존성 | 포트 버전이 로컬과 같다: catch2 3.15.3, lua 5.5.0#1, nlohmann-json 3.12.0#2, tomlplusplus 3.4.0#1, wil 1.0.260126.7 |
| vcpkg 고정 | 첫 실행에서 `ilammy/msvc-dev-cmd` 가 `VCPKG_ROOT` 를 VS 내장 vcpkg 로 덮어써, 고정 커밋으로 받은 vcpkg 가 쓰이지 않았다. vcvars 를 직접 처리하도록 바꾸고 `VCPKG_ROOT` 검사 단계를 추가했다. 이후 고정 도구(2026-07-27 판)가 쓰이는 것을 확인했다 |
| 바이너리 캐시 | 키에 버전을 넣은 뒤 다시 실행했을 때 9개 패키지를 복원했다. 작업 시간이 Release 6분 53초 → 3분 58초, Debug 5분 27초 → 2분 39초로 줄었다 |
| 로그 보안 | 토큰·비밀 문자열 없음. vcvars 환경 전체를 다시 내보내던 방식 때문에 러너 기본 변수(`PGPASSWORD`, 러너 이미지 공개 기본값)가 단계마다 찍혔다. 바뀐 변수만 넘기도록 고쳐 사라졌다. 권한은 `contents: read` 만 쓴다 |
| 액션 | checkout@v7, cache@v6, upload-artifact@v7 (Node 24). Node 20 사용 중단 경고가 사라졌다 |
