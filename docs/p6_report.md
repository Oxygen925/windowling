# P6 진행 기록 (공개 배포)

**기준:** [P6 상세 계획 v0.2](plan/desktop_pet_p6_detailed_plan_ko.md), [보안 점검](security_review_ko.md)  
**측정 PC:** P0~P5 와 같다 (Windows 11 26200, 모니터 2560×1440).

**상태 (2026-10-20):** P6-01~P6-08 구현·자동 검증 완료. **P6-09 (공개 저장소·첫 릴리스)는 사용자 확인 전** — 완료 판정(기준선 §10: 설치·삭제·업데이트·보안·성능 회귀 통과) 중 "실제 릴리스 워크플로로 만든 zip 설치" 만 남았다.

## 1. 작업 상태

| ID | 작업 | 상태 | 확인 | 커밋 |
|---|---|---|---|---|
| P6-01 | 배포 빌드 `x64-dist`: 정적 CRT(`/MT`, `x64-windows-static`), 시험 훅·개발 캐릭터·개발 도구 끔, `/DEPENDENTLOADFLAG:0x800`, asInvoker, 버전 0.1.0 (VERSIONINFO·로그·트레이) | 완료 | `dumpbin` 가져오기에 vcruntime·msvcp 없음, 로드 플래그 0x800, 매니페스트, dist 단위 시험, 시험 인자 무시 | `7622862` |
| P6-02 | `tools/release/package.ps1`: zip·SHA-256·라이선스 원문·예제 캐릭터 zip·`latest.json` | 완료 | 항목 대조, 새 폴더에 풀어 실행, 예제 캐릭터 zip 이 앱 설치 검사 통과 | `0f18aef` |
| P6-03 | 업데이트 확인 (하루 1번, 알림만, 기본 켬) | 완료 | 단위 3건, UI 시나리오 UP1~UP7 | `0cf6256` |
| P6-04 | 모든 데이터 지우기 | 완료 | UI 시나리오 WP1~WP3 | `e36180b` |
| P6-05 | 캐릭터 없을 때 환영 창 안내 | 완료 | 배포 빌드 화면 확인 | `e36180b` |
| P6-06 | CI: x64-dist 빌드·패키징 확인, 태그 릴리스 초안 워크플로, 액션 SHA 고정 | 완료 | CI 통과 (`be57a0f`, 7분), 릴리스 워크플로 시험 §3.3 | `be57a0f` |
| P6-07 | README(ko·en)·SECURITY·CHANGELOG·라이선스 고지 | 완료 | 텍스트 검사, 메뉴 문구와 대조 | `1d8cc31` |
| P6-08 | 회귀·성능 | 완료 | §3 | — |
| P6-09 | 공개 저장소·첫 릴리스 | **사용자 확인 전** | — | — |

P6 앞에 한 보안 점검(High 2·Medium 7·Low 9, [보안 점검](security_review_ko.md))과 빌드 의존성 문제 수정(`c201bd6`)도 이 단계의 "보안 회귀" 기준에 들어간다.

## 2. 설계 요약

- **배포물:** `DesktopPet-0.1.0-win-x64.zip` (1.5MB, 13개: `desktop-pet.exe`·`pack-check.exe`·`lang\`·`licenses\`(toml++·WIL·Lua·nlohmann/json·cgltf)·LICENSE·THIRD_PARTY_NOTICES·README ko/en) + `.sha256`. 예제 캐릭터는 `DesktopPet-character-test-orange/test-blue-0.1.0.zip` 으로 따로. 데이터는 `%LOCALAPPDATA%\DesktopPet`.
- **업데이트 확인:** `https://github.com/<저장소>/releases/latest/download/latest.json` → 리디렉션은 `github.com`·`*.githubusercontent.com`(https)만 최대 3번 → schema·버전·sha256 형식·**페이지 주소가 그 저장소의 릴리스 페이지인지** 검사 → 새 버전이면 풍선 알림(버전마다 1번)과 트레이 메뉴. 앱은 내려받지 않는다. 저장소는 빌드 옵션 `DP_RELEASE_REPO` (릴리스 CI 가 `github.repository` 로 지정, 비면 확인 안 함).
- **모든 데이터 지우기:** 확인 → 로그인돼 있으면 로그아웃(토큰 폐기, 최대 10초 대기) → Run 값 삭제 → 앱이 만든 파일·폴더만 삭제(`--config` 가 다른 폴더여도 통째로 지우지 않음) → 종료.
- **릴리스 워크플로:** 태그 `v*` → x64-dist → 단위 시험 → 태그와 버전 일치 확인 → 패키지 → `gh release create --draft`. 공개는 사용자가 초안을 보고 한다.

## 3. 검증

### 3.1 시나리오 (2026-10-20, 개발 릴리스 빌드)

| 시나리오 | 결과 |
|---|---|
| P1 | 12/12 (`logs/p1/20261020_183613_scenarios`) |
| P2 | 25/25 (`logs/p2/20261020_183632_scenarios`) |
| P3 | 47/47 (`logs/p3/20261020_183655_scenarios`) |
| P4 | 28/28 (`logs/p4/20261020_183746_scenarios`) |
| P5 | 9/9 (`logs/p5/20261020_183914_scenarios`) |
| UI | 40/40 (`logs/ui/20261020_183924_scenarios`, 업데이트 UP1~7·지우기 WP1~3 포함) |

- 단위 테스트: dp_core_tests 72건, dp_platform_tests 6건 (x64-release·x64-dist 모두).
- 시나리오는 시험 인자가 필요해 개발 빌드로 돌린다. 배포 빌드는 시작·종료·캐릭터 없음·시험 인자 무시·환영/설정 창 화면을 따로 확인했다.

### 3.2 성능 (P1 측정 스크립트, 60초 × 3회 평균, `logs/p1/20261020_183952_regression`)

| ID | 시나리오 | CPU% (GetProcessTimes / PDH / 사이클) | Working Set | P1 기준 (2026-10) | P1 허용 |
|---|---|---|---|---|---|
| R1 | 1마리 정지 | 0.005 / 0.006 / 0.005 | 46.8MB | 0.006% · 41.2MB | ≤ 0.05% |
| R2 | 1마리 계속 걷기 | 0.152 / 0.160 / 0.166 | 42.7MB | 0.165% · 41.0MB | ≤ 0.3% |
| R3 | 창 이벤트 폭주 | 0.300 / 0.272 / 0.256 | 46.6MB | 0.252% · 40.9MB | ≤ 1% |
| R4 | 창 50개 정지 | 0.006 / 0.007 / 0.005 | 46.9MB | 0.003% · 41.2MB | ≤ 0.05% |
| R5 | 커서 이동 중 정지 | 0.028 / 0.029 / 0.029 | 46.8MB | 0.036% · 40.9MB | ≤ 0.05% |

- 모두 P1 허용 안. 메모리는 P1 대비 +5~6MB (P2~P6 에서 들어온 UI 창·AI·3D 렌더러·업데이트 확인).
- 측정은 개발 릴리스 빌드(`/MD`) — 배포 빌드에는 측정 인자(`--metrics`)가 없다. 코드는 같고 런타임 링크 방식만 다르다 `[검증: 배포 빌드 수치는 재지 않음]`.

### 3.3 릴리스 워크플로 시험 (2026-10-20, 비공개 저장소)
- 시험 태그 `v0.1.0` → 워크플로 전 단계 성공 → 초안 릴리스 "Desktop Pet 0.1.0" 에 자산 7개 (zip·예제 캐릭터 zip 2개·각 `.sha256`·`latest.json`).
- 내려받은 zip 의 SHA-256 = `.sha256` = `latest.json` 의 값, `latest.json` 의 페이지 = 이 저장소 릴리스 주소, 풀면 13개 항목, 실행 파일에 저장소 이름(`DP_RELEASE_REPO`)·버전 0.1.0.
- 확인 뒤 초안 릴리스와 태그를 지웠다 (사용자 결정대로 공개된 것 없음).

### 3.4 확인하지 않은 것 `[검증]`
- GitHub 의 `releases/latest/download` 리디렉션 실제 경로 (초안은 latest 가 아니라 확인 불가 — 공개 뒤 확인).
- VC++ 재배포 패키지가 없는 깨끗한 PC 에서 실행 (가져오기 목록으로만 확인).
- SmartScreen 화면, 실제 휴지통·실제 Run 키 (시험은 대체 경로).

## 4. 남은 것 (P6-09)

**2026-10-20 사용자 결정:** 공개 저장소 이름 `Oxygen925/windowling`, 기록은 **커밋 1개로 압축**, 공개(저장소 만들기·push)는 아직 하지 않음. README 는 지금 것으로 충분.

**준비된 것**
- 공개 전 점검: 추적 파일에 비밀·자격 증명·개인 경로·이메일 없음, 커밋 작성자는 모두 noreply, 최대 파일 160KB(Fox, CC-BY 표기 있음). 문서의 "DevKit" 은 공개 저장소를 참고만 했다는 기록(코드 복사 없음).
- 로컬 브랜치 `public-release`: 지금 master 와 같은 트리를 커밋 1개("Desktop Pet 0.1.0")로. 원격에는 올리지 않았다.
- 릴리스 워크플로는 비공개 저장소에서 시험 완료 (§3.3).

**이름 겹침:** 지금 비공개 개발 저장소가 `Oxygen925/windowling` 이다. 같은 이름의 새 저장소는 만들 수 없으므로 공개할 때 다음 중 하나를 고른다.
- (추천) 비공개 저장소 이름을 `windowling-dev` 로 바꾸고(개발 기록 보존), 새 공개 `windowling` 에 `public-release` 를 `master` 로 올린다.
- 지금 저장소를 공개로 바꾸고 기록을 `public-release` 로 강제로 바꾼다 — 원격의 개발 기록이 사라져 추천하지 않는다.

**공개 절차 (단계마다 사용자 확인)**
1. 비공개 저장소 이름 바꾸기 `gh repo rename windowling-dev` (로컬 `origin` 주소도 갱신).
2. `gh repo create Oxygen925/windowling --public --description "..."` → `git push <공개> public-release:master`.
3. 저장소 설정: 비공개 취약점 신고(Security) 켜기, Actions 권한 확인, 기본 브랜치 master.
4. CI 통과 확인 → `v0.1.0` 태그 → 릴리스 초안 확인 (자산 7개·`latest.json` 주소가 공개 저장소) → 공개.
5. 공개 뒤: `releases/latest/download/latest.json` 리디렉션·앱의 업데이트 확인(같은 버전이라 알림 없음)·README 의 링크 확인.
6. 이후 개발: 비공개 `windowling-dev` 에서 하고, 공개 저장소에는 릴리스마다 정리한 커밋을 올린다 (방식은 그때 결정).

**진행 (2026-10-20)**
- 1~3 완료: 비공개 저장소는 `Oxygen925/windowling-dev` (로컬 `origin`), 공개 저장소 `Oxygen925/windowling` (로컬 `public`) 에 `public-release`(커밋 `018fafd`) 를 `master` 로 올림. 기본 브랜치 master, 비공개 취약점 신고 켬.
- 4 진행: 공개 저장소 CI(run 39409063479) 4개 작업 통과 → 주석 태그 `v0.1.0` push → Release 워크플로(run 39409872182) 성공, **초안** 생성.
  - 확인: 자산 7개, ZIP 3개 SHA-256 이 `.sha256` 와 일치, `latest.json` page = `https://github.com/Oxygen925/windowling/releases/tag/v0.1.0`, 실행 파일에 저장소 이름 `Oxygen925/windowling` 이 들어감, ZIP 13개 항목.
  - **초안 공개는 사용자 결정으로 보류.** 공개하면 5 를 진행한다.
- 예제 캐릭터 교체 (사용자 결정): `test-orange`·`test-blue` 대신 아이콘 기반 마스코트 `windowling`(MIT, 128px 11프레임, 눈 깜빡임·talk 입 모양) 하나를 릴리스에 올리고, 기본 설정의 펫도 `windowling` 으로 바꿈. core 단위 테스트 73개·P1 시나리오 12/12(펫 `windowling`) 통과. 공개 저장소는 커밋 1개로 다시 만들고 초안·태그를 지운 뒤 다시 태그한다.
- 주의: 공개 저장소에 다시 올릴 때는 `public-release` 를 그때의 master 로 새로 만든다 (지금 master 의 이 문서 변경은 공개 커밋에 없음).
