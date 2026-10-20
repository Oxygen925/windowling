# P4 실연동 검증 절차 (화면 관찰 → 실제 SIWC)

**목적:** 실제 화면 이미지를 실제 ChatGPT 계정(SIWC)으로 보내 관찰·발화가 동작하는지 확인한다.  
**원칙**
- 로그인·동의·화면 관찰 켜기는 **사용자가 직접** 한다. 자동화하지 않는다.
- 로그에는 이미지·응답 원문·창 제목·실행 파일 이름이 남지 않는다. 결과는 `[observe]`·`[tts]` 줄의 코드·크기·시간만 본다.
- 확인하는 동안 화면에 개인정보(메일·메신저·은행 등)가 보이지 않게 한다. 관찰 대상은 **펫이 있는 모니터 전체**다.

## 0. 준비

1. 최신 빌드: `tools\build.ps1 -Preset x64-release`
2. P3 실연동이 된 상태 (ChatGPT 로그인 가능).
3. 설정 파일(`%LOCALAPPDATA%\DesktopPet\config.toml`)에서 확인 시간을 줄인다. 확인이 끝나면 되돌린다.
   ```toml
   [observe]
   interval_min = 1
   interval_max = 2
   max_per_hour = 3
   ```
4. 로그 폴더: `%LOCALAPPDATA%\DesktopPet\logs\desktop-pet_*.log`

## 1. 단계

| 단계 | 할 일 | 기대 로그 | 확인 |
|---|---|---|---|
| 1 | 앱 실행 → 트레이 "AI 대화 사용" → 로그인 | `[ai] ui signin ok=true` | |
| 2 | 트레이 "화면 관찰 (꺼짐)" → "화면 관찰 켜기…" → 동의 화면 내용 확인 → **예** | `[observe] enabled consent_version=1 signed_in=true` | 동의 문구가 실제 동작과 맞는지 |
| 3 | 대화창을 닫고 1~2분 기다린다 | `[observe] start …` → `[observe] capture ok src=…x… out=…x…` → `[observe] done … result=spoke` 또는 `result=silent` | 관찰 중 트레이 툴팁 "화면을 보는 중", 펫 말풍선이 뜨는지 (WGC 테두리는 2026-10-19 확인에서 보이지 않았다) |
| 4 | (구조화 출력) 3의 `done` 앞에 다음 경고가 있는지 본다: `구조화 출력(text.format)을 거부` | 없으면 구조화 출력 지원, 있으면 안내문 JSON 으로 동작 | 결과 기록 |
| 5 | (이미지 지원) 다음 경고가 있는지 본다: `이미지 입력을 거부` | 있으면 그 모델은 관찰 중지 → 대화창에서 다른 모델 선택 후 다시 | 결과 기록 |
| 6 | 트레이 "펫의 말을 음성으로 읽기 (로컬 TTS)" 를 켜고 다음 발화를 기다린다 | `[tts] spoke … completed=true` | 소리·음성(Heami 등) |
| 7 | 트레이 "1시간 일시정지" | `[observe] pause 1h` → 이후 `skip reason=paused` | |
| 8 | 트레이 "화면 관찰 끄기" | `[observe] disabled`, 이후 `[observe] start` 없음 | |

## 2. 기록할 것

- 모델 이름(대화창 선택값), 구조화 출력 지원 여부, 이미지 지원 여부.
- 발화가 상황에 맞았는지, 화면의 개인정보를 읽어 말하지 않았는지 (주관 평가).
- `done` 줄의 `http`·`code`·`request_id` (오류일 때).
- 결과는 [P4 보고서](p4_report.md) §3 에 추가한다. 이 확인 전까지 P4 는 최종 완료가 아니다.

## 3. 끝나면

- 설정의 `[observe]` 값을 되돌린다 (또는 줄을 지운다 → 기본 15~30분, 시간당 2회).
- 계속 쓰지 않으려면 트레이에서 "화면 관찰 끄기".
