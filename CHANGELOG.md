# Changelog

## 0.1.1

- Sample character Windowling 1.1.0: jumps, climbs walls, and with two or more Windowlings greets, plays tag, high-fives (with its body), bumps, sits together and walks in a line. Re-install it from this release to get the new moves
- Optional actions for characters: jump (onto a higher window top within reach, or a small hop) and climb (up a window side onto its top, or up the outer screen edge and jump off). Enabled per character with `jump`/`climb` in `[activities]` and the `jump`/`climb` animations
- Development builds use a separate data folder (`DesktopPet-Dev`)
- Screen watching limits: 0 means no limit for the per-hour and per-day caps (interval down to 1 minute); consent and status windows show it; going from a limit to no limit asks for consent again
- Settings: default model and reasoning effort for chat and screen watching, and a separate effort for pet chat (sent as `reasoning.effort`; if the model rejects it, the request is sent again without it)
- More pet interactions (per character): high five, bumping into each other, sitting together, walking in a line, pair programming with a review, a wall-climbing race, stacking on each other's heads (up to 3), and chasing a bug that shows up now and then (`bump`, `lean`, `train`, `bug_hunt`, `highfive`/`lean`/`bug` animations)
- Pets can chat with each other using AI when they meet (setting, off by default, at most every 10 minutes by default; yields to your chat and screen watching, not read aloud)
- Optional per character: a short rest between activities (`rest_between`), jumping across to same-level or lower windows and leaping onto floating window sides, pets greeting each other and playing tag (`greet`, `follow`)

---

- 예제 캐릭터 Windowling 1.1.0: 점프, 벽 타기, 2마리 이상이면 인사·잡기 놀이·하이파이브(몸으로)·부딪치기·붙어 앉기·기차놀이. 이 릴리스의 캐릭터 zip 으로 다시 설치하면 적용
- 캐릭터 선택 행동: 점프(닿는 높이의 창 위로, 또는 작게 뛰기), 벽 타기(창 옆면을 올라 창 위로, 또는 화면 바깥 끝을 오르다 뛰어내리기). 캐릭터마다 `[activities]` 의 `jump`·`climb` 과 애니메이션으로 켠다
- 개발 빌드는 데이터 폴더를 따로 쓴다 (`DesktopPet-Dev`)
- 화면 관찰 상한: 시간당·하루 상한 0 = 무제한 (간격은 최소 1분), 동의·상태 창에 표시, 제한에서 무제한으로 바꾸면 다시 동의
- 설정: 대화·화면 관찰의 기본 모델과 생각 깊이(effort), 펫끼리 대화의 effort 따로 (`reasoning.effort` 로 보내고, 모델이 받지 않으면 빼고 다시 보냄)
- 펫끼리 상호작용 추가 (캐릭터 선택): 하이파이브, 부딪치면 튕기기, 붙어 앉기, 기차놀이, 페어 프로그래밍·코드 리뷰, 벽 타기 경주, 머리 위 올라타기(3층까지), 가끔 나타나는 버그 잡기 (`bump`·`lean`·`train`, `bug_hunt`, `highfive`·`lean`·`bug` 애니메이션)
- 펫끼리 만나면 AI 로 한마디씩 주고받기 (설정, 기본 끔, 기본 10분 간격. 사용자 대화·화면 관찰에 양보, 음성으로 읽지 않음)
- 캐릭터 선택: 행동 사이 짧은 쉼(`rest_between`), 같은 높이·낮은 창으로 건너뛰기와 떠 있는 창 옆면에 뛰어 붙기, 펫끼리 인사·잡기 놀이(`greet`, `follow`)

## 0.1.0 — first public release / 첫 공개

- Desktop pets that walk on the desktop and on top of windows (Win32 + Direct3D 11 + DirectComposition), drag, click reactions, multiple pets, full-screen hiding
- Character packages: 2D sprites and 3D glTF, Lua behavior scripts (sandboxed), install from folder or `.zip` with checks, `pack-check` tool
- Optional AI chat with Sign in with ChatGPT (uses the user's ChatGPT plan), speech bubbles, global shortcut
- Optional screen watching with consent window, privacy guard (lock, full screen, away, excluded apps), pause, re-consent when scope widens
- Optional reading aloud: Windows voices, local TTS engines (OpenAI-compatible HTTP on localhost, or a command that writes WAV)
- Settings, characters, welcome and consent windows; Korean and English; light and dark
- Update check (notification only, once a day, can be turned off); "Delete all data"
- Portable zip, static runtime, no installer; not code-signed
- Sample character "Windowling" as a separate download (the app icon with legs)

---

- 바탕 화면과 창 위를 걷는 데스크톱 펫 (Win32 + Direct3D 11 + DirectComposition), 끌기·클릭 반응·여러 마리·전체화면 숨김
- 캐릭터 패키지: 2D 스프라이트·3D glTF, Lua 행동 스크립트(샌드박스), 폴더·`.zip` 검사 후 설치, `pack-check` 도구
- 선택: Sign in with ChatGPT AI 대화 (사용자 ChatGPT 플랜 사용), 말풍선, 전역 단축키
- 선택: 화면 관찰 — 동의 창, 개인정보 보호(잠금·전체화면·자리 비움·제외 앱), 일시정지, 범위가 넓어지면 재동의
- 선택: 음성 읽기 — Windows 음성, 로컬 TTS(이 PC 의 OpenAI 호환 HTTP, WAV 를 쓰는 명령)
- 설정·캐릭터 관리·환영·동의 창, 한국어·영어, 밝게·어둡게
- 새 버전 확인 (알림만, 하루 1번, 끌 수 있음), 모든 데이터 지우기
- Portable zip, 정적 런타임, 설치 프로그램 없음, 코드 서명 없음
- 예제 캐릭터 "Windowling" 별도 내려받기 (앱 아이콘에 다리를 단 모양)
