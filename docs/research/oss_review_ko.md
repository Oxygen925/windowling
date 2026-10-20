# 유사 오픈소스 조사 — 가져올 만한 부분 정리

**작성일:** 2026-10-14
**목적:** 그동안 조사한 오픈소스 데스크탑 펫·AI 컴패니언 프로젝트를 우리 프로젝트(P0~P3 구현 상태)와 비교한다. 단계별로 **가져올 만한 아이디어**와 **가져오면 안 되는 것**을 나눠 둔다.
**상태:** 후보 목록이다. 무엇을 실제로 채택할지는 정하지 않았다(사용자 결정).

**확인 수준**
- 각 프로젝트 내용은 저장소 페이지·README 와 제3자 인덱스로 확인했다.
- 코드를 직접 빌드하거나 실행한 것은 없다.
- 별 수 등 수치는 조사 시점 기준이다.

---

## 1. 조사한 프로젝트와 라이선스

"재사용"은 우리 코드(Apache-2.0)에 소스를 가져올 수 있는지를 뜻한다.

| 프로젝트 | 언어/스택 | 라이선스 | 재사용 | 우리와 가까운 축 |
|---|---|---|---|---|
| [NeurolingsCE-Qt](https://github.com/qingchenyouforcc/NeurolingsCE-Qt) | C++17 / Qt 6.8 | GPLv3 + 추가 조건 | **불가** (아이디어만) | 창 상호작용 펫, Shimeji 팩, 배포 |
| [Shijima-Qt](https://github.com/pixelomer/Shijima-Qt) / [libshijima](https://github.com/pixelomer/libshijima) | C++ / Qt | GPL-3.0 | **불가** | Shimeji 엔진. 2026-04-29 보관(읽기 전용) |
| [Shimeji-ee](https://github.com/gil/shimeji-ee) | Java | 확인하지 않음 | 확인 전 불가 | 행동 XML, 캐릭터 생태계 |
| [Shimeji Desktop Mascot](https://sourceforge.net/projects/shimeji-desktop/) | Windows 네이티브 | GPLv3 | **불가** | 창 위 오르기, 다중 모니터 |
| [VPet-Simulator](https://github.com/LorisYounger/VPet) | C# / WPF | Apache-2.0 (기본 애니메이션은 별도 저작권) | 코드 가능 (언어가 달라 직접 쓰기는 어려움) | 모드·워크숍·코드 플러그인 |
| [Seelie](https://github.com/huangcheng/Seelie) | C++17 / Qt 6.5 | MIT | 코드 가능 (출처 표기). Live2D 팩·SDK 는 불가 | 팩 형식, TTS, 로컬 이벤트 |
| [WindowPet](https://github.com/SeakMengs/WindowPet) | Tauri + React | MIT | 코드 가능 (스택 다름) | 클릭 통과 오버레이 |
| [Open-LLM-VTuber](https://github.com/Open-LLM-VTuber/Open-LLM-VTuber) | Python + 웹 | MIT (Live2D 샘플 제외) | 코드 가능 (스택 다름) | 음성·화면 인식·TTS/ASR 모듈 |
| [AIRI](https://www.mintlify.com/moeru-ai/airi) | 웹/데스크탑 | 확인하지 않음 | 확인 전 불가 | 다중 LLM Provider, VRM |
| [Mate-Engine](https://github.com/shinyflvre/Mate-Engine) | Unity | AGPL v3 + 자체 라이선스 혼합 | **불가** | VRM 데스크탑 메이트 |
| [DyberPet](https://gittrend.io/repo/ChaozhongLiu/DyberPet) | Python / PySide6 | 확인하지 않음 | 확인 전 불가 | 성장·작업·모드·LLM |

**에셋(이미지·모델·음성)은 모두 가져오지 않는다.** 이유:
- CC-BY-NC-SA 펫 팩(NeurolingsCE), VPet 기본 애니메이션, Mate-Engine 기본 아바타, 각 프로젝트의 Live2D 샘플은 비상업 조건이거나 권리가 따로 있다.
- 2026-10-14 결정에 따라 우리 배포판에는 기본 캐릭터팩을 넣지 않는다.

---

## 2. 우리가 이미 앞선 부분 (가져올 필요 없음)

| 항목 | 우리 | 조사 대상 |
|---|---|---|
| 창 지형 | 모든 창의 상단 노출 구간(Z-order 빼기), 펫별 위 공간, 가림 유예 | Shijima 계열은 맨 앞 창 추적으로 보인다 (README 기준, 코드 미확인) |
| 클릭 통과 | P0 에서 3가지 방식을 실측하고 `SetWindowRgn` 채택 | 대부분 방식이나 측정 근거를 밝히지 않음 |
| 성능 근거 | 유휴 CPU 0.005% 수준, 1/3/5마리 측정, Present 0 | "가볍다"는 주장뿐. Mate-Engine 은 모델 하나에 약 200MB 라고 밝힘 |
| 스크립트 안전성 | Lua 샌드박스(명령 수·메모리 제한, 자동 비활성) | Shimeji 는 XML 안 스크립트, VPet 은 제한 없는 C# 플러그인 |
| AI 인증 보안 | DPAPI 저장, issuer 별 격리, 로그 마스킹, 모의 서버 시나리오 | 비슷한 수준은 NeurolingsCE 의 OS 자격 증명 저장소 정도 |

---

## 3. 가져올 만한 부분 (후보)

### 3.1 캐릭터 생태계 — P2 보완 / P6

| # | 후보 | 출처 | 우리 현황 | 가져올 형태 | 비용·위험 |
|---|---|---|---|---|---|
| C1 | **압축 팩 1개 파일 + 드래그 설치** | Seelie `.spk`, NeurolingsCE `.mascot` | 폴더만 지원 (P2-R1, zip 은 P6 에서 검토) | 설계 참고. zip 압축 해제 시 경로 검증은 기준선 §6.2 에 이미 요구됨 | 압축 해제 라이브러리(miniz 등, MIT) 추가 필요 |
| C2 | **팩 형식 스키마 문서화** | Seelie `character-pack-v1.schema.json` | ADR-13 에 형식을 글로만 적어 둠 | 우리 TOML 형식의 기계 판독 스키마나 검증 CLI | 낮음. `LoadPackage` 를 재사용하는 검증 도구로 충분 |
| C3 | **Shimeji 팩 가져오기(변환)** | NeurolingsCE "제작" 페이지, Shimeji-ee 호환 | 없음 | 아이디어만 (GPL 코드 금지). 이미지·기본 행동만 우리 형식으로 변환 | Shimeji XML 의 스크립트 조건은 변환 불가 → 일부만 지원. 팩마다 라이선스 확인 필요 |
| C4 | **제작 도구 / 미리보기** | VPet ModMaker, NeurolingsCE 제작 페이지 | 없음 | 패키지 검증 + 애니메이션 미리보기 도구 | 중간 |
| C5 | **콘텐츠 등급 태그로 배포 포함 여부 결정** | Seelie `nsfw` 태그 + 빌드 옵션 | `DP_DEV_CHARACTERS` (개발용 캐릭터만 복사) | manifest 에 `tags` 예약 | 낮음 |

### 3.2 창 상호작용·행동 — P2 이후

| # | 후보 | 출처 | 우리 현황 | 가져올 형태 |
|---|---|---|---|---|
| W1 | **벽·창 측면 오르기, 천장 매달리기** | Shimeji 계열 기본 행동 | `climb` 자리만 예약 (P2-R5 범위 밖) | 동작 아이디어만. 우리 1D 노출 구간 모델을 세로 측면으로 넓혀 새로 설계 |
| W2 | **다중 모니터·사용자 배율 실사용 근거** | NeurolingsCE·Shimeji Desktop Mascot 이 지원을 명시 | 실환경 검증 연기 (D10) | 다중 모니터 환경이 생기면 같은 시나리오로 비교 |
| W3 | **창 모드(샌드박스 창 안에서 실행)** | NeurolingsCE window mode | 없음 | 캐릭터 제작자 미리보기·디버그용 |

### 3.3 AI·음성 — P3 보완 / P4

| # | 후보 | 출처 | 우리 현황 | 가져올 형태 |
|---|---|---|---|---|
| A1 | **TTS 공급자 추상화 + 디스크 캐시 상한(100MB)** | Seelie (MIT, C++) | P4 예정, `ITtsProvider` 인터페이스만 기준선에 있음 | 구조 참고. 캐시 상한·만료 정책 포함. 첫 공급자는 기준선대로 Windows 로컬 TTS |
| A2 | **오프라인 TTS/ASR 후보 목록** | Open-LLM-VTuber (sherpa-onnx, Edge TTS 등) | 없음 | P4 엔진 선정 후보로 검토 (sherpa-onnx 는 C++ API 가 있어 우리 스택에 맞음) `[검증]` |
| A3 | **"승인은 자동으로 하지 않는다 / 허용 필드만 표시" 원칙** | NeurolingsCE Codex 연동 | P3 에서 같은 방향으로 구현 (로그 마스킹, 화면 텍스트 불신) | 원칙을 P4 Privacy Guard 문서에 명시 |
| A4 | **로컬 이벤트 수신 API / CLI** — 예: Claude Code hook → 펫 반응 | Seelie (UDP 127.0.0.1), NeurolingsCE (REST localhost + CLI + 에이전트 스킬) | 없음 (기준선 범위 밖) | 새 기능 후보. 넣는다면 127.0.0.1 전용, 읽기 전용 이벤트, 인증 토큰, 명령 화이트리스트 | 
| A5 | **API 키 / 로컬 모델 Provider** | AIRI, Open-LLM-VTuber (다중 공급자) | `SiwcClient` 만 있음 (ADR-06 은 Provider 분리) | P3 이후. SIWC 실연동이 막힐 때의 대안 경로이기도 함 (errors-and-recovery 문서도 "자기 API 키" 대안을 언급) |

A4 의 위험: 로컬 다른 프로세스가 펫을 조종할 수 있게 된다.

### 3.4 배포·업데이트 — P6

| # | 후보 | 출처 | 가져올 형태 |
|---|---|---|---|
| D1 | **정적 `latest.json` + SHA-256 검증 업데이트** | NeurolingsCE | 업데이트 확인 설계 그대로 참고. 서명 검증 추가를 검토 |
| D2 | **배포 산출물 구성: 포터블 ZIP + MSI(WiX) + `SHA256SUMS.txt`** | NeurolingsCE | 기준선 §12 "Portable ZIP 우선"과 맞음. MSI 는 안정화 이후 |
| D3 | **팩 상점 / Steam 워크숍** | NeurolingsCE 상점, VPet 워크숍 | 장기 후보. 배포 경로와 라이선스 정책이 먼저 필요 |
| D4 | **로그인 토큰은 OS 자격 증명 저장소** | NeurolingsCE (GitHub Device Flow) | 우리는 DPAPI 파일 (크기 제한 때문, P3-R3). 현행 유지 |

### 3.5 테스트·검증

| # | 후보 | 출처 | 가져올 형태 |
|---|---|---|---|
| T1 | **로컬 HTTP 서버로 외부 공급자 테스트** | Seelie (QHttpServer 로 TTS 공급자 테스트) | 우리 모의 SIWC 서버와 같은 방식. P4 TTS 테스트에 그대로 적용 |
| T2 | **같은 PC 비교 측정** | NeurolingsCE (실행 가능한 배포판 있음) | 유휴 CPU·메모리, 창 추종을 같은 조건으로 비교. **외부 프로그램을 내려받아 실행하므로 사용자 허락 필요** |

---

## 4. 가져오면 안 되는 것

- **GPL·AGPL 코드:** NeurolingsCE-Qt, Shijima-Qt, libshijima, Shimeji Desktop Mascot, Mate-Engine.
  - 가져오면 우리 Apache-2.0 코드 전체가 GPL/AGPL 조건을 따라야 한다.
  - 동작·구조 아이디어만 참고하고, 소스를 보면서 옮겨 쓰지 않는다.
- **비상업·권리 보유 에셋:** NeurolingsCE 공식 팩(CC-BY-NC-SA-4.0), VPet 기본 애니메이션, Mate-Engine 기본 아바타, 각 프로젝트의 Live2D 샘플·SDK.
- **제한 없는 네이티브 코드 플러그인 (VPet 방식):** 기준선 ADR-07(초기에는 Lua, 이후 C ABI/IPC)과 맞지 않는다.
- **Qt 의존성:** Seelie·NeurolingsCE 는 Qt 기반이다. 우리 렌더링·입력 경로(D3D11/DComp, P0 실측)를 바꿀 이유가 없다.

---

## 5. 추천 우선순위 (미확정)

| 순위 | 후보 | 이유 |
|---|---|---|
| 1 | **A1 + T1** (P4 착수 때) | P4 다음 단계와 바로 이어진다. MIT 참고 자료가 있고 우리 테스트 방식과 같다 |
| 2 | **C2 팩 검증 CLI** | 비용이 낮다. 기본 캐릭터팩 없이 배포하기로 한 이상, 외부 제작자를 지원하는 첫 도구가 된다 |
| 3 | **C1 압축 팩 + 드래그 설치** (P6) | 배포판에 캐릭터가 없으므로 사용자가 캐릭터를 넣는 경로가 중요해졌다 |
| 4 | **D1·D2 업데이트·배포 구성** (P6) | 검증된 구성을 그대로 계획에 넣을 수 있다 |
| 5 | **W1 등반** | 장르의 핵심 재미 요소다. 다만 물리 모델을 넓혀야 해서 별도 설계가 필요하다 |
| 보류 | A4 로컬 이벤트 API, C3 Shimeji 가져오기, D3 상점 | 기준선 범위 밖이거나 라이선스·보안 결정이 먼저 필요하다 |

---

## 6. 사용자 결정이 필요한 것

1. 위 후보 중 채택할 항목과 시기
2. A4 (Claude Code 등 외부 도구 이벤트에 반응하는 로컬 API)를 범위에 넣을지
3. T2 비교 측정을 위해 NeurolingsCE 배포판을 내려받아 실행해도 되는지
4. 라이선스를 확인하지 않은 프로젝트(Shimeji-ee·AIRI·DyberPet)를 더 확인할지
