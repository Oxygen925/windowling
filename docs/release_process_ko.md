# 개발용·배포용 분리와 릴리스 절차

2026-10-20 결정 (사용자): 실행 환경과 릴리스 절차를 개발용·배포용으로 나눈다.

## 1. 저장소

| 원격 | 저장소 | 용도 |
|---|---|---|
| `origin` | `Oxygen925/windowling-dev` (비공개) | 개발. 모든 커밋·문서·로그 |
| `public` | `Oxygen925/windowling` (공개) | 릴리스마다 커밋 1개 ("Desktop Pet <버전>"). 태그·릴리스 |

- 공개 저장소에는 강제 push 하지 않는다 (v0.1.0 공개 뒤부터). 새 릴리스 커밋은 공개 `master` 바로 위에 쌓는다.
- 공개 커밋의 트리는 그때의 비공개 `master` 트리와 같다. 작성자는 GitHub noreply 주소.

## 2. 실행 채널 (CMake `DP_DEV_CHANNEL`)

| | 개발 채널 (`ON`, 기본) | 배포 채널 (`OFF`) |
|---|---|---|
| 프리셋 | `x64-debug`, `x64-release` | `x64-dist` |
| 데이터 폴더 | `%LOCALAPPDATA%\DesktopPet-Dev` | `%LOCALAPPDATA%\DesktopPet` |
| 창 클래스·단일 실행 mutex | `DesktopPetDev.*` | `DesktopPet.*` |
| 시작 프로그램 값 이름 (`HKCU\...\Run`) | `DesktopPet-Dev` | `DesktopPet` |
| 트레이 툴팁 | 끝에 ` (Dev)` | 그대로 |
| 로그 첫 줄 | `channel=dev` | `channel=release` |

- 둘을 동시에 띄워도 설정·로그인 토큰·캐릭터·로그·시작 등록이 섞이지 않는다. 개발 빌드의 "모든 데이터 지우기" 는 `DesktopPet-Dev` 만 지운다.
- 시나리오(`tests/scenario/*.ps1`)·측정 스크립트는 개발 빌드를 실행하므로 `DesktopPet-Dev\logs`·`DesktopPetDev.*` 를 쓴다.
- `tools/release/package.ps1` 은 개발 채널 빌드면 실패한다 (배포 zip 에 개발 빌드가 들어가지 않게).
- 이 옵션이 생기기 전에 구성한 `build\x64-dist` 는 캐시에 값이 없어 `ON` 이 된다 → `tools\build.ps1 -Preset x64-dist -Configure` 로 다시 구성 (패키징 검사가 막아 준다).
- 개발 채널을 처음 실행하면 `DesktopPet-Dev` 는 비어 있다 (설정은 기본값, 로그인 안 됨. 캐릭터는 실행 파일 옆 `characters\` 의 개발용 패키지). 실사용 데이터(`DesktopPet`)는 배포판이 쓴다.

## 3. 릴리스 절차

1. 비공개 `master` 에서 준비
   - `CMakeLists.txt` 의 `project(desktop_pet VERSION x.y.z)` 를 올린다 (이미 공개한 버전이면 3단계에서 멈춤).
   - `CHANGELOG.md` 에 새 버전 항목.
   - 회귀: 단위 테스트, `tests/scenario` P1~P5·UI. 커밋하고 `origin` 에 push → 비공개 CI 통과 확인.
2. `powershell -File tools\release\publish_public.ps1 -Step Prepare`
   - `public/master` 위에 "Desktop Pet x.y.z" 커밋을 만들어 로컬 `public-release` 에 둔다. 바뀐 파일 수를 보여 준다.
3. `... -Step Push` → 공개 CI 시작. 통과를 확인한다 (`gh run list --repo Oxygen925/windowling`).
4. `... -Step Tag` → 공개 CI 통과를 다시 확인하고 `vx.y.z` 태그 push → `release.yml` 이 **초안** 을 만든다.
5. 초안 확인: 자산(본 ZIP·예제 캐릭터 zip·각 `.sha256`·`latest.json`), SHA-256 일치, `latest.json` 의 `page` 가 공개 저장소, 실행 파일의 저장소 이름.
6. 공개 (사람이 결정): `gh release edit vx.y.z --repo Oxygen925/windowling --draft=false --latest`
7. 공개 뒤: `releases/latest/download/latest.json` 받기, 이전 버전 앱의 업데이트 알림, README 링크.

- 각 단계는 원격을 바꾸므로 실행 전에 확인한다. 스크립트는 단계마다 조건(깨끗한 작업 트리, `origin` 과 같음, noreply 작성자, fast-forward, 공개 CI 통과, 태그 없음)을 검사하고 어긋나면 멈춘다.
