# Third-Party Notices

이 프로젝트의 코드는 [MIT License](LICENSE)를 따른다. 빌드에 쓰는 외부 구성 요소의 라이선스는 아래와 같다.
버전은 `vcpkg.json` 의 `overrides` 로 고정한다. 배포 zip 의 `licenses\` 에 정적 링크한 라이브러리의 라이선스 원문(vcpkg `share/<패키지>/copyright`)을 넣는다 (`tools/release/package.ps1`, 없으면 패키징 실패).

| 구성 요소 | 버전 | 라이선스 | 배포판 포함 | 용도 |
|---|---|---|---|---|
| [toml++](https://github.com/marzer/tomlplusplus) | 3.4.0 | MIT | 예 (정적 링크) | 설정·캐릭터 패키지 TOML |
| [WIL](https://github.com/microsoft/wil) | 1.0.260126.7 | MIT | 예 (헤더) | Win32 RAII |
| [Lua](https://www.lua.org/) | 5.5.0 | MIT | 예 (정적 링크) | 캐릭터 행동 스크립트 |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | 예 (헤더) | JSON |
| [cgltf](https://github.com/jkuhlmann/cgltf) | 1.15 | MIT | 예 (헤더) | glTF(.glb) 로드 (P5) |
| [Catch2](https://github.com/catchorg/Catch2) | 3.15.3 | BSL-1.0 | 아니요 (테스트 전용) | 단위 테스트 |
| C++/WinRT, Windows SDK 헤더·라이브러리 | SDK 10.0.26100 | Windows SDK 라이선스 | 아니요 (빌드 도구) | WGC·TTS·Win32 API |
| Microsoft C/C++ 런타임 (MSVC) | Visual Studio 2022 | Visual Studio 라이선스 (재배포 허용 구성 요소) | 예 (정적 링크, 배포 빌드 `/MT`) | C·C++ 표준 라이브러리 |

## 캐릭터 에셋

- 캐릭터 패키지는 코드와 별도로 라이선스를 표기한다 (`manifest.toml` 의 `[license]`, 기준선 §6.2).
- 배포 zip 에는 캐릭터를 넣지 않는다 (2026-10-14·2026-10-20 결정). 예제 캐릭터 `windowling` 은 릴리스에 zip 으로 따로 올린다.
  - `windowling`: MIT, `tools/assets/gen_windowling_sprite.ps1` 로 생성. 앱 아이콘(아래)의 창·눈·꺾쇠에 다리를 단 그림.
- 저장소의 `characters/test-*` 는 개발·테스트용 에셋이다.
  - `test-orange`·`test-blue`: CC0-1.0, 스크립트 생성.
  - `test-fox`: Khronos glTF-Sample-Assets "Fox". 모델 PixelMannen (CC0-1.0), 리깅·애니메이션 tomkranis (CC-BY-4.0), glTF 변환 AsoboStudio·scurest (CC-BY-4.0). `characters/test-fox/LICENSE.md`.

## 앱 아이콘

- `apps/desktop-pet/app.ico`·`tools/assets/app_icon.svg`: AI(ChatGPT)로 만든 SVG 를 바탕으로 이 프로젝트에서 고친 것 (눈 추가·선 굵기·정렬·배경 대비·작은 크기 전용 픽셀 그림). 프로젝트 라이선스(MIT)를 따른다.

## 외부 서비스·엔진 (동봉하지 않음)

- **Sign in with ChatGPT (OpenAI):** 사용자가 직접 로그인해 쓴다. [Sign in with ChatGPT Terms](https://openai.com/policies/sign-in-with-chatgpt-terms/) 적용.
- **로컬 AI TTS 엔진·음성 모델:** 동봉하지 않는다. 사용자가 설치하며 각 엔진·모델의 라이선스(비상업 조건 등)는 사용자가 확인한다.
