# P3 실연동 검증 절차서 (실제 ChatGPT 계정)

**목적:** 모의 서버로만 검증한 SIWC 연동을 실제 계정으로 확인한다. 확인할 단계는 로그인 → 모델 목록 → 짧은 스트리밍 응답 → 대화 이어가기 → 토큰 갱신 → 로그아웃이다.
**원칙:**
- 로그인과 권한 승인은 **사용자가 직접** 한다.
- 결과는 로그의 `[ai]` 줄로 판단한다.
- 로그에는 토큰·인증 코드·대화 원문·이메일이 남지 않는다(P3-R13, 시나리오 P1a 로 확인).
**완료 조건:** 아래 1~6 이 모두 기대 결과와 같으면 P3 를 최종 완료로 처리한다.

## 준비

1. **계정:** ChatGPT Plus 또는 Pro 계정을 준비한다(문서 기재 대상 플랜).
2. **포트:** `127.0.0.1:1455` 를 쓰는 프로그램(Codex CLI 등)을 종료한다. 최초 등록 콜백 포트다.
3. **AI 켜기:** `%LOCALAPPDATA%\DesktopPet\config.toml` 끝에 다음을 추가한다.
   ```toml
   [ai]
   enabled = true
   ```
4. **빌드:** `powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset x64-release`
5. **실행:** `build\x64-release\desktop-pet.exe` (모의 서버 인자 없이 → 실제 엔드포인트)
6. **로그 위치:** `%LOCALAPPDATA%\DesktopPet\logs\` 의 최신 파일 (트레이 → 로그 폴더 열기)

## 절차와 기대 결과

| # | 할 일 | 기대 로그 (`[ai]` 줄) | 화면 |
|---|---|---|---|
| 0 | 앱 시작 | `enabled endpoint=production hotkey=on` | — |
| 1 | 펫 클릭 → 대화창 → **Continue with ChatGPT** → 브라우저에서 앱 이름 "Desktop Pet" 확인 후 승인 | `discovery ok` → `signin start registration=true port=1455` → `signin ok registration=true plan_usage=true` → `ui signin ok=true` | 계정 이메일, "Using ChatGPT plan", 첫 안내(Got it) |
| 2 | (자동) 모델 목록 | `models N개` (N ≥ 1), `ui models count=N` | 모델 선택 상자에 모델 표시 |
| 3 | "안녕, 한 문장으로 자기소개해 줘" 보내기 | `send pet=0 ...` → `stream done ... outcome=completed ... first_delta=…s` | 답이 조금씩 나타남, 말풍선 |
| 4 | "방금 뭐라고 했지?" 보내기 (이어가기) | `send ... turns=3` → `outcome=completed` | 앞 대화를 기억한 답 |
| 5 | 트레이 → **AI 연결 진단 (토큰 갱신·모델 목록)** | `diag refresh ok=true` → `token refresh ok` → `diag models ok=true count=N` → `ui diag ok=true` | 상태 줄 "진단: 토큰 갱신 성공" |
| 6 | 대화창 **로그아웃** | `signout revoked` → `ui signout ok=true code=` | "로그아웃했습니다" |
| 7 | (선택) 다시 **Continue with ChatGPT** | `signin start registration=false port=1455 reconsent=false` → `signin ok registration=false` | 문서 기대 동작: **계정 선택 화면 → 바로 돌아옴**. 워크스페이스 선택·동의 화면은 없어야 한다. 확인 페이지가 나오면 승인까지 진행하고 결과를 기록 |

## 문제가 생기면 확인할 것 (문서에 명시되지 않았거나 예시로만 있던 부분)

| 증상 (로그) | 가능한 원인 | 보내 주실 정보 |
|---|---|---|
| `코드 교환 실패 http=400 error=...` | 토큰 교환 요청 형식(`grant_type` 등) 차이 | 해당 줄 전체 (`request_id` 포함) |
| `콜백 수신 시작 실패: port_in_use` | 1455 를 다른 프로그램이 사용 | — (그 프로그램 종료 후 재시도) |
| `ID 토큰 검증 실패: ...` | issuer·aud·iat 등 클레임 차이 | 해당 줄 |
| `discovery 무시: ...` | OpenID 설정 응답이 예상과 다름 | 해당 줄 (내장 엔드포인트로 계속 진행됨) |
| `models 0개` | `/v1/models` 응답 형식 차이 | `ui models` 줄 |
| `stream done ... outcome=failed code=...` | 계정·플랜·정책 | `stream done` 줄 전체 |
| `outcome=timed_out code=first_token_timeout` | 첫 글자까지 120초 초과 (추론이 긴 모델) | 모델 이름, 줄 전체. 필요하면 `[ai] first_token_timeout_sec` 조정 |

보내 주실 때는 로그에서 `[ai]` 가 들어간 줄만 골라 주시면 된다. 이 줄들에는 토큰·이메일·대화 원문이 없다.

## 끝나면

- 결과는 [p3_report.md](p3_report.md) §6 과 [수동 체크리스트](p3_manual_checklist.md)에 기록한다.
- 실제 응답 형식이 문서와 달라 고칠 점이 생기면 수정한 뒤 1~6 을 다시 진행한다.
