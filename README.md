# Desktop Pet

Windows 바탕 화면과 창 위를 돌아다니는 데스크톱 펫입니다. 펫은 창 가장자리에 서고, 창을 옮기거나 닫으면 떨어져 다른 곳에 내려앉습니다.

- 2D 스프라이트·3D(glTF) 캐릭터, 여러 마리 동시에
- 선택 기능 (모두 기본 꺼짐): AI 대화(Sign in with ChatGPT), 화면 관찰(펫이 가끔 화면을 보고 한마디), 음성 읽기(Windows 음성·로컬 TTS)
- 한국어·영어, 밝게·어둡게

[English](README.en.md)

## 설치

1. 릴리스 페이지에서 `DesktopPet-<버전>-win-x64.zip` 을 내려받습니다.
2. (권장) 내려받은 파일이 릴리스의 SHA-256 과 같은지 확인합니다. PowerShell:
   ```powershell
   Get-FileHash .\DesktopPet-0.1.0-win-x64.zip -Algorithm SHA256
   ```
   결과를 같은 이름의 `.sha256` 파일·릴리스 설명의 값과 비교합니다.
3. 원하는 폴더에 압축을 풀고 `DesktopPet\desktop-pet.exe` 를 실행합니다. 설치 프로그램은 없습니다 (Portable).
4. 실행 파일에 코드 서명이 없어 처음 실행할 때 **Windows SmartScreen** 이 "Windows의 PC 보호" 를 띄울 수 있습니다. 출처와 SHA-256 을 확인했다면 **추가 정보 → 실행** 을 누릅니다.

**요구 사항:** Windows 11 x64, Direct3D 11 GPU. (Windows 10 2004 이상에서도 동작할 수 있지만 확인하지 않았습니다.) 별도의 런타임 설치는 필요 없습니다.

## 처음 실행

- 배포판에는 캐릭터가 들어 있지 않습니다. 릴리스의 예제 캐릭터 **Windowling**(`DesktopPet-character-windowling-*.zip`)을 내려받아, 트레이 메뉴 **캐릭터 관리…** 창에 끌어다 놓으면 검사한 뒤 설치하고 바로 나타납니다. 띄울 캐릭터와 마릿수도 그 창에서 정합니다.
- 앱은 알림 영역(트레이)에 아이콘으로 상주합니다. 처음에는 환영 창이 안내합니다 (트레이 **도움말** 로 다시 볼 수 있음).

## 조작

| 동작 | 결과 |
|---|---|
| 끌기 | 펫을 집어 다른 곳에 놓기 |
| 클릭 | 펫이 반응 |
| 펫 오른쪽 클릭 | 펫 메뉴 (대화·캐릭터 바꾸기·제거·숨기기) |
| 트레이 아이콘 | 전체 메뉴 (펫·AI 대화·화면 관찰·음성 읽기·캐릭터 관리·설정·도움말·종료) |

설정은 트레이 **설정…** 창에서 바꿉니다 (언어·테마·펫 크기·AI·화면 관찰·음성·전체화면 예외). 고급 값은 설정 창의 **설정 파일 열기** 로 `config.toml` 을 직접 고칠 수 있습니다.

## AI 대화 (선택)

- 트레이 **AI 대화 → AI 대화 사용 (Sign in with ChatGPT)** 으로 켜고, 대화창에서 **Continue with ChatGPT** 로 로그인합니다. 브라우저에서 OpenAI 로그인·동의를 하면 앱으로 돌아옵니다.
- 대화는 **사용자의 ChatGPT 플랜 사용량** 으로 처리됩니다 (앱 자체 요금 없음). 사용량은 대화창의 **Manage usage** 에서 확인합니다.
- 로그인 정보(토큰)는 이 PC 에 Windows DPAPI 로 암호화해 저장합니다. 대화 원문은 디스크·로그에 남기지 않고 메모리에만 둡니다.

## 화면 관찰 (선택)

- 트레이 **화면 관찰 → 화면 관찰 켜기…** 로 켤 때 동의 창이 무엇을 보내는지 보여 주고, 체크해야 켜집니다.
- 켜면 가끔(기본 15~30분, 시간당 최대 2회) 펫이 있는 모니터 화면 1장(축소)을 로그인한 ChatGPT 계정으로 OpenAI 에 보내 짧은 한마디를 받습니다. 이미지와 응답 원문은 저장하지 않습니다.
- 잠금·전체화면·자리 비움·대화 중·**제외 앱**(비밀번호 관리자·메신저 등, 실행 파일 이름 기준)이 그 모니터에 보일 때는 보지 않습니다. 브라우저 안의 웹 메신저·비밀번호 확장은 실행 파일 이름으로 거를 수 없다는 점에 주의하세요.
- 언제든 끄거나 일시정지할 수 있습니다 (일시정지는 다시 시작해도 유지). 동의 뒤 설정으로 범위(빈도·이미지 크기·제외 앱 등)를 넓히면 관찰이 꺼지고 다시 켤 때 동의를 다시 묻습니다.

## 음성 읽기 (선택)

트레이 **펫의 말을 음성으로 읽기** 로 켜면 Windows 음성으로 펫의 말을 읽습니다. 직접 설치한 로컬 AI TTS 엔진에도 연결할 수 있습니다 — [로컬 TTS 연결](docs/local_tts_ko.md).

## 캐릭터 만들기

캐릭터는 `manifest.toml` 이 있는 폴더(패키지)입니다. 규격은 [ADR-13 캐릭터 패키지](docs/adr/ADR-13-character-package.md), 예는 저장소의 `characters/windowling`(2D, 예제 캐릭터)·`characters/test-fox`(3D) 입니다. 배포 zip 에 든 `pack-check.exe` 로 앱과 같은 규칙으로 검사할 수 있습니다:

```powershell
.\pack-check.exe C:\my-characters\my-cat
```

설치 때는 zip 경로·링크·크기·스크립트(Lua 샌드박스)를 검사합니다. 출처를 알 수 없는 캐릭터 패키지는 설치하지 않는 것이 좋습니다.

## 데이터와 개인정보

- 데이터 위치: `%LOCALAPPDATA%\DesktopPet` (설정·캐릭터·로그·로그인 정보·상태 파일). 로그는 14일 지나면 지웁니다.
- 앱이 네트워크로 보내는 것:
  - AI 대화를 쓸 때: 대화 내용 → OpenAI (Sign in with ChatGPT).
  - 화면 관찰을 켰을 때: 위 "화면 관찰" 의 내용 → OpenAI.
  - 새 버전 확인 (기본 켬, 하루 1번): GitHub 릴리스의 `latest.json` 요청 (IP 주소와 앱 버전이 GitHub 에 전달됨). 설정 **새 버전 확인** 을 끄면 보내지 않습니다. 새 버전이 있으면 알리기만 하고, 내려받기는 사용자가 릴리스 페이지에서 합니다.
  - 그 밖의 사용 통계·원격 수집은 없습니다.
- 로컬 TTS 는 이 PC(127.0.0.1) 의 엔진에만 연결합니다.

## 삭제

1. 트레이 **설정… → 일반·표시 → 모든 데이터 지우기…** — 로그아웃(토큰 폐기), 시작 프로그램 등록 해제, 데이터 폴더 삭제 뒤 앱이 끝납니다.
2. 압축을 푼 `DesktopPet` 폴더를 지웁니다.

앱을 실행할 수 없을 때는 `%LOCALAPPDATA%\DesktopPet` 폴더를 지우고, 시작 프로그램 등록을 켰었다면 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 의 `DesktopPet` 값을 지웁니다 (설정 앱 **앱 → 시작 프로그램** 에서도 끌 수 있음).

## 문제 신고·보안

- 버그·제안: 저장소의 Issues.
- 보안 문제: 공개 이슈 대신 [SECURITY.md](SECURITY.md) 의 방법으로 알려 주세요.

## 라이선스

코드는 [MIT License](LICENSE). 함께 쓰는 라이브러리의 라이선스는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) (배포 zip 의 `licenses\` 에 원문). 캐릭터 패키지는 각자의 라이선스를 따릅니다 (`manifest.toml` 의 `[license]`).

Sign in with ChatGPT 사용에는 OpenAI 의 [Sign in with ChatGPT Terms](https://openai.com/policies/sign-in-with-chatgpt-terms/) 가 적용됩니다. 이 프로젝트는 OpenAI 와 관련이 없습니다.
