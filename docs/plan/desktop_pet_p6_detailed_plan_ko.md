# Windows Desktop Pet — P6 상세 계획 (공개 배포)

**버전:** 0.2 (2026-10-20, 사용자 결정 반영: §5 추천안 7개 모두 확정)  
**작성일:** 2026-10-20  
**기준 문서:** 기준선 v1.3 §10(P6)·§11·§376("초기 배포는 GitHub Releases 의 Portable ZIP 우선"), [보안 점검](../security_review_ko.md), [남은 작업](../tasks_ko.md)

**2026-10-20 사용자 결정**
- 배포 형태: **Portable ZIP 먼저** (설치 프로그램은 안정화 뒤).
- 업데이트: **확인·알림만** — 앱이 `latest.json` 을 확인해 새 버전을 알리고 릴리스 페이지를 연다. 앱이 실행 파일을 바꾸지 않는다.
- 코드 서명: **일단 없음** — SmartScreen 경고를 README 에 안내하고 SHA-256 을 제공.
- 기본 캐릭터: **동봉 안 함** (이전 결정 유지, 캐릭터 없으면 트레이만 상주).
- (이전 결정) 라이선스 MIT, 저장소는 P6 공개 때 새 저장소로 (커밋 이메일 노출 최소화).
- §5 추천안 7개 확정: C 런타임 정적 링크, 첫 버전 0.1.0, 데이터는 `%LOCALAPPDATA%` 유지, 예제 캐릭터 zip 을 릴리스에 별도로,
  업데이트 확인 기본 켬·하루 1번, 설정에 "모든 데이터 지우기", 태그 릴리스는 초안만(공개는 사용자).

**표기:** 추천 (미확정) = 사용자 확인 전 기본값, `[검증]` = 확인하지 않은 동작.

**완료 판정 (기준선 §10):** 설치·삭제·업데이트·보안·성능 회귀 테스트 통과.

## 1. 현황 (2026-10-20)

- 빌드: CMake Preset `x64-release`, vcpkg 트리플렛 `x64-windows-static-md` (라이브러리는 정적, **C 런타임은 DLL** → 실행 PC 에 VC++ 재배포 패키지 필요).
- 시험 전용 인자·훅: CMake `DP_TEST_HOOKS` (기본 ON). 개발 캐릭터 복사: `DP_DEV_CHARACTERS` (기본 ON).
- 데이터: `%LOCALAPPDATA%\DesktopPet` (설정·로그·자격 증명·캐릭터·상태 파일).
- CI: GitHub Actions — 텍스트 검사, x64-debug/x64-release 빌드·단위 테스트. 릴리스·패키징 없음.
- 버전: `vcpkg.json` 의 `0.3.0-p3` 만 있고 실행 파일에 버전 정보 없음.
- 문서: 단계별 계획·보고서(한국어). 사용자용 README 없음.
- 보안 점검 뒤 남은 배포 항목: 정적 가져오기 DLL 검색 고정(`/DEPENDENTLOADFLAG`), 인증서 폐기 확인 검토.

## 2. 설계

### 2.1 배포 빌드 (`x64-dist` 프리셋)
- Release + **C 런타임 정적 링크** (트리플렛 `x64-windows-static`, `/MT`) — 추천: 실행 파일 하나, VC++ 재배포 패키지 불필요.
- `DP_TEST_HOOKS=OFF`, `DP_DEV_CHARACTERS=OFF`, 도구 타깃(측정·모의 서버·ui-gallery) 빌드 안 함.
- 링커 `/DEPENDENTLOADFLAG:0x800` (정적 가져오기 DLL 도 System32 에서만), 매니페스트 `asInvoker` 명시.
- 버전: CMake `project(VERSION x.y.z)` 하나에서 실행 파일 `VERSIONINFO`·앱 표시·`latest.json` 을 만든다. 첫 공개 **0.1.0** (추천).

### 2.2 Portable ZIP
- `DesktopPet-<버전>-win-x64.zip`:
  - `DesktopPet\desktop-pet.exe`, `lang\`, `LICENSE`, `THIRD_PARTY_NOTICES.md` + 라이선스 원문(`licenses\`), `README.md`(ko)·`README.en.md`.
- 같은 이름 `.sha256` 파일. 릴리스 노트에 해시.
- 데이터는 지금처럼 `%LOCALAPPDATA%\DesktopPet` (추천: zip 을 어디에 풀어도 쓰기 권한 문제 없음). "포터블 데이터(zip 폴더 안)" 모드는 하지 않음.
- 캐릭터: 동봉하지 않음 (결정). 대신 **릴리스에 별도 예제 캐릭터 zip**(`test-orange`·`test-blue`, CC0) 을 올려 캐릭터 관리 창으로 설치하게 한다 (추천). 환영 창에 "캐릭터가 없으면 캐릭터 관리에서 설치" 안내.
  - 2026-10-20 변경 (사용자 결정): 예제 캐릭터를 아이콘 기반 마스코트 `windowling`(MIT, 128px) 하나로 바꾸고, 기본 설정의 펫도 `windowling` 으로 (설치하면 바로 뜨게). `test-*` 는 저장소에 개발·테스트용으로 남긴다.

### 2.3 업데이트 확인 (알림만)
- `latest.json` (릴리스 자산, 고정 주소 `https://github.com/<소유자>/<저장소>/releases/latest/download/latest.json`):
  `{"schema":1,"version":"0.1.1","published":"2026-..","page":"https://github.com/.../releases/tag/v0.1.1","zip":"DesktopPet-0.1.1-win-x64.zip","sha256":"…"}`
- 앱: 시작 후 1분 뒤, 하루 최대 1번 HTTPS GET (WinHTTP, 리디렉션은 github.com·objects.githubusercontent.com 만) → 버전 비교 → 새 버전이면 트레이 알림 + 메뉴 "새 버전 x.y.z…". 클릭하면 `page` 를 연다 (**`https://github.com/<소유자>/<저장소>/releases/` 로 시작할 때만**).
- 설정 창 "일반·표시" 에 "업데이트 확인" (기본 **켬**, 추천). 끄면 요청 없음. 보내는 것은 HTTP 요청뿐(IP·User-Agent 에 버전) — README 개인정보 절에 명시.
- 앱은 내려받지 않는다 (결정). SHA-256 은 사용자가 내려받은 zip 을 확인할 때 쓴다 (README 에 `Get-FileHash` 안내).

### 2.4 삭제
- 포터블이라 폴더를 지우면 끝. 남는 것: `%LOCALAPPDATA%\DesktopPet`(자격 증명 포함), 시작 프로그램 Run 값.
- 설정 창에 **"모든 데이터 지우기…"** (추천): 확인 → 로그아웃(토큰 폐기) → Run 값 삭제 → 데이터 폴더를 지우고 종료. README 에 수동 삭제 방법도.

### 2.5 CI 확장
- PR·push: 지금 검사 + `x64-dist` 빌드·단위 테스트·패키징 시험(zip 내용 목록 검사).
- 태그 `v*`: `x64-dist` 빌드 → 단위 테스트 → zip·sha256·`latest.json`·예제 캐릭터 zip 생성 → GitHub Release 초안 생성(자동 공개 안 함, 사용자가 확인 후 공개). 액션은 커밋 SHA 로 고정, 권한 최소(`contents: write` 는 릴리스 작업만).

### 2.6 라이선스·문서
- `THIRD_PARTY_NOTICES.md` 갱신 + 배포 zip 에 각 라이브러리 라이선스 원문 (vcpkg `share/<패키지>/copyright` 에서 자동 수집, 빠진 것이 있으면 패키징 실패).
- 사용자 문서 (README ko·en): 내려받기·SHA-256 확인·SmartScreen 안내, 기본 조작, AI 대화(Sign in with ChatGPT)·화면 관찰의 동작과 보내는 데이터, 캐릭터 설치·만들기(`pack-check`), 로컬 TTS 연결, 삭제, 문제 신고.
- `SECURITY.md`(취약점 신고), `CHANGELOG.md`.

### 2.7 공개 저장소
- 새 저장소에 깨끗한 기록으로 올린다 (커밋 이메일은 noreply). 지금 저장소는 비공개 유지.
- **공개(저장소 만들기·push·릴리스 공개)는 외부에 내보내는 작업이라 그때마다 사용자 확인 후** 진행.

## 3. 작업 목록

| ID | 작업 | 테스트 |
|---|---|---|
| P6-01 | `x64-dist` 프리셋: 정적 CRT·시험 훅 끔·DLL 검색 고정·asInvoker·버전 정보 | 빌드, `dumpbin` 으로 가져오기·플래그 확인, 시험 인자 무시 확인 |
| P6-02 | 패키징 스크립트 (`tools/release/package.ps1`): zip·sha256·라이선스 원문·예제 캐릭터 zip | zip 내용 목록 검사, 깨끗한 폴더에 풀어 실행 |
| P6-03 | 업데이트 확인: `latest.json` 파서(core)·버전 비교·WinHTTP·알림·메뉴·설정 항목 | 단위(파서·버전·주소 검사), 모의 서버 시나리오 |
| P6-04 | "모든 데이터 지우기" | 시나리오 (시험 데이터 폴더·시험 Run 키) |
| P6-05 | 환영 창·캐릭터 없음 안내 | UI 시나리오 |
| P6-06 | CI: dist 빌드·패키징 검사, 태그 릴리스 초안 워크플로 | CI 실행 (태그는 시험 저장소/초안만) |
| P6-07 | 라이선스·README(ko·en)·SECURITY·CHANGELOG | 텍스트 검사, 링크 확인 |
| P6-08 | 회귀: P1~P5·UI 시나리오를 dist 실행 파일로 (시험 훅이 필요한 것은 개발 빌드), 성능 측정(P1 기준) | 결과 기록 |
| P6-09 | 공개 저장소·첫 릴리스 | **사용자 확인 후** |

## 4. 테스트 (완료 판정과 연결)

| 기준 | 시험 |
|---|---|
| 설치 | 깨끗한 폴더에 zip 을 풀고 실행 (VC++ 재배포 패키지 없는 상태 가정: 정적 CRT 확인), 첫 실행 환영 창 |
| 삭제 | "모든 데이터 지우기" 뒤 데이터 폴더·Run 값 없음, 폴더 삭제 |
| 업데이트 | 모의 `latest.json`: 새 버전 → 알림·메뉴, 같은/낮은 버전 → 없음, 잘못된 JSON·다른 주소 → 무시, 끔 → 요청 없음 |
| 보안 | 보안 점검 회귀(단위·P3·P4·UI), dist 빌드에 시험 인자 없음, DLL 플래그 |
| 성능 | P1 측정 스크립트로 기존 기준과 비교 (dist) |

## 5. 추천안 (2026-10-20 모두 확정)

1. C 런타임 정적 링크 (실행 파일 하나, 재배포 패키지 불필요).
2. 첫 공개 버전 0.1.0.
3. 데이터는 `%LOCALAPPDATA%\DesktopPet` 유지 (포터블 데이터 모드 없음).
4. 릴리스에 예제 캐릭터 zip 을 별도로 올림 (배포판 동봉은 안 함).
5. 업데이트 확인 기본 켬 (설정에서 끔), 하루 1번.
6. 설정에 "모든 데이터 지우기…".
7. 태그 릴리스는 초안으로만 만들고 공개는 사용자가.

## 6. 위험

| 위험 | 대응 |
|---|---|
| 서명 없는 실행 파일 → SmartScreen 경고·백신 오탐 | README 안내, SHA-256, 나중에 서명 검토 |
| 정적 CRT 로 바꾸며 빌드 차이 (vcpkg 트리플렛) | dist 전용 프리셋, 개발 빌드는 그대로, CI 에서 둘 다 |
| `latest.json` 위조 (서명 없음) | 앱은 내려받지 않고 GitHub 릴리스 페이지만 연다 (주소 접두어 검사) |
| 라이선스 원문 누락 | 패키징 스크립트가 vcpkg copyright 를 모아 없으면 실패 |
| 공개 저장소에 비공개 정보 | 공개 전 기록·파일 검사 (토큰·이메일·로그·개인 경로) |
