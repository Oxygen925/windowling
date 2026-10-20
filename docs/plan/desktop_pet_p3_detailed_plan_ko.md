# Windows Desktop Pet — P3 상세 계획 및 작업 계획서 (AI 대화)

**버전:** 0.3 (2026-10-14 보완 결정 반영)  
**작성일:** 2026-10-14  
**기준 문서:** 기준선 v1.3 §1.3·§5.2·§7·§8·§10(P3)·§11, [P2 계획](desktop_pet_p2_detailed_plan_ko.md), [P2 보고서](../p2_report.md)  
**문서 상태:** 사용자가 "필요/불필요 조사 → 작업 계획서 → 테스트 포함 구현"을 위임했다(2026-10-14).
- 기준선에 근거가 없는 선택은 **추천 (미확정)** 으로 표시하고, 구현은 추천안 기본값으로 진행한다.
- `[검증]` 은 아직 확인하지 않은 동작이다.

**진행 상태:** P3-01~P3-11 구현·자동 검증(모의 서버)을 마쳤다. 남은 것은 실제 계정 수동 확인과 P3-R1~R14 사용자 확인이다 → [P3 보고서](../p3_report.md)

> **P3 목표 (기준선 §10):** SIWC 로그인, 대화창, 말풍선, 전역 단축키.
> **완료 판정:** 승인·대화·스트림·로그아웃·오류 복구 검증.

> **완료 판정의 분리 (중요):** 실제 ChatGPT 계정으로 하는 로그인·동의는 **사용자만 할 수 있다.**
> - 조건: Plus/Pro 플랜이 필요하다(문서 기재).
> - 자동화가 할 수 없는 일: 사용자 비밀번호 입력, OAuth 권한 승인, OpenAI 서버로 실요청 보내기. 이 셋은 자동화하지 않는다.
> - 따라서 완료 판정은 두 부분으로 나눈다.
>   - **(A) 자동 검증:** 로컬 모의 서버(`127.0.0.1` 전용)로 프로토콜·오류 복구·UI 흐름 전체를 검증한다.
>   - **(B) 실연동:** 사용자가 수동 체크리스트로 확인한다.

---

## 1. 현황 조사

### 1.1 SIWC 공식 문서에서 확인한 사실 (문서 기재, 실연동 미검증)

조사일은 2026-10-14 이다. 출처는 `developers.openai.com/siwc/...` 이다.

모든 항목은 문서에 적힌 내용일 뿐 **실연동은 하지 않았다.** 문서가 바뀔 수 있으므로 실제 연동 전에 다시 확인한다(기준선 §7.2).

| 항목 | 내용 | 출처 |
|---|---|---|
| 대상 | 오픈소스·로컬 실행 앱은 셀프서비스 흐름으로 "ChatGPT plan usage" 를 쓸 수 있다. 최종 사용자는 Plus·Pro | `/siwc/quickstart`, cookbook |
| OIDC 설정 | issuer `https://auth.openai.com`<br>authorize `…/api/accounts/authorize`<br>token `…/api/accounts/oauth/token`<br>revoke `…/api/accounts/oauth/revoke`<br>jwks `…/.well-known/jwks.json`<br>서명 RS256 | `auth.openai.com/.well-known/openid-configuration` |
| 최초 등록 | `client_id=dynamic_agent_client`, `agent_name_hint`=앱 이름, `ext_agent_host_id`(호스트별 고정·불투명 값), `response_type=code`, `scope=openid profile email offline_access resource.invoke chatgpt.tokens.use.direct`, `resource=https://api.openai.com/v1`, 시도마다 새 `state`·`nonce`, PKCE `S256` | `/token-sharing-open-source/sign-in` |
| 콜백 | `http://127.0.0.1:<port>/auth/callback`<br>`localhost` 가 아니라 `127.0.0.1`, 경로는 정확히 `/auth/callback`<br>예시 포트 1455. "later sign-ins may use another available port"<br>성공 시 `code, state, client_id(신규 등록), scope`, 거부 시 `error=access_denied` | 같음 |
| 다시 로그인 | 저장된 `client_id`(`oaiapp_…`)를 쓰고 `agent_name_hint` 는 생략<br>같은 `ext_agent_host_id`<br>선택: `id_token_hint`, `login_hint` | 같음, `/profiles-and-sessions` |
| 코드 교환 | token 엔드포인트에 form POST, `authorization_code` grant<br>필드: `client_id, code, code_verifier, redirect_uri(같은 값), resource`. 시크릿 없음<br>`invalid_grant` → 새로 인가 | 같음 |
| ID 토큰 검증 | JWKS 서명, `iss`, `aud`=발급 client_id, `exp`, `nonce`, `sub` = 계정 식별자 | 같음 |
| 토큰 | access 1시간, refresh 30일(갱신할 때마다 교체)<br>응답 필드: `access_token, refresh_token, id_token, token_type, expires_in, scope, earliest_refresh_at` | `/token-reference` |
| 갱신 | token 엔드포인트, refresh_token grant, 발급 client_id, `resource`, `scope` 생략<br>세션별로 직렬화 | `/profiles-and-sessions` |
| 로그아웃 | 요청 중지 → revoke 엔드포인트에 `token`(refresh), `token_type_hint=refresh_token`, `client_id`<br>빈 200 = 성공 → 로컬 토큰 삭제<br>확인하지 못하면 "원격 해제 미확인, ChatGPT 설정에서 연결 해제 가능" 안내<br>등록(client_id·host id)은 유지 | 같음 |
| 추론 | `POST https://api.openai.com/v1/responses`, Bearer<br>`store:false`, `stream:true`<br>`input` 에 이력 전체(`previous_response_id` 사용 불가)<br>`instructions` 또는 developer 메시지 사용 가능, system role 항목은 거부 | `/models-and-inference`, `/preview-limitations` |
| 미지원 필드 | `background, conversation, max_output_tokens, max_tool_calls, metadata, moderation, multi_agent, prompt, prompt_cache_retention, safety_identifier, temperature, top_logprobs, top_p, truncation, user` | `/preview-limitations` |
| 모델 | `GET /v1/models` → `models[]` 중 `visibility=="list"` 의 `slug`·`display_name`<br>예시 슬러그는 하드코딩하지 않는다 | `/models-and-inference` |
| 스트림 | `response.output_text.delta`(텍스트)<br>`response.completed` 를 받아야만 성공<br>`response.failed`(`error.code`), `response.incomplete`<br>종료 이벤트 없이 끊기면 실패 | 같음 |
| 오류 | 401·403·503(스트림 전)<br>`subscription_sharing_user_not_eligible`(403, 재시도 금지)<br>`…usage_limit_exceeded`(429, 일시 중지 + Manage usage)<br>`…usage_unavailable`·`…user_unavailable`(503, 제한된 백오프)<br>`…unsupported_capability`(400)<br>`…invalid_user`(401, 재로그인)<br>refresh 의 `invalid_grant` 등 → 토큰 삭제 후 재로그인<br>`invalid_client` → 설정 문제<br>범위 누락 → 로그인은 유지하되 plan usage 끔 | `/errors-and-recovery` |
| 저장 | 토큰은 보호된 로컬 저장소에 둔다. 로그·URL·소스에 넣지 않는다. `id_token_hint` 가 들어간 URL 은 로그에서 가린다 | `/profiles-and-sessions` |
| UI | 로그인 버튼 문구 "Continue with ChatGPT"<br>첫 로그인 안내 "You're using your ChatGPT plan" + "Got it"<br>입력창 근처에 "Using ChatGPT plan" + "Manage usage"<br>한도 초과 시 "Usage limit reached" | `/siwc/ui-ux-guidelines` |

**쓰지 않는 것:** OpenAI DevKit(`openai/sign-in-with-chatgpt-devkit`).
- 비상업 라이선스이고 Node/React 기반이다. 코드를 복사하지 않고 문서 계약대로 C++ 로 새로 구현한다.
- ChatGPT 로고 에셋은 받지 않는다. 버튼은 텍스트만 쓴다. 로고 사용은 P6 배포 때 브랜딩 지침을 검토한 뒤 결정한다.

### 1.2 현재 코드에서 재사용할 것

| 대상 | 현황 | P3 사용 |
|---|---|---|
| `GpuContext` | D2D Device Context·DWrite Factory 를 이미 생성한다 | 말풍선 텍스트 |
| `TextRenderer` (P0-11) | 한 줄 텍스트 + 둥근 배경 | 줄바꿈·크기 측정을 더해 `BubbleRenderer` 로 확장 |
| 방식 A 창 스타일 (P0 E1) | `WS_EX_LAYERED + TRANSPARENT + NOREDIRECTIONBITMAP` 에서 DComp 가 표시되고 투명 영역 클릭이 통과함을 확인 | 말풍선은 **항상 클릭 통과** 이므로 A 의 토글 경쟁(누락) 문제가 없다 |
| `pet_app` 루프 | 단일 UI 스레드, 마감 기반 대기, 공통 30Hz 이동 틱, `DeferWindowPos` 묶음 | 말풍선 위치를 펫과 같은 묶음으로 옮긴다 |
| 트레이·우클릭 메뉴 | 펫 대상 메뉴 | AI 대화 열기, 로그인·로그아웃 |
| 캐릭터 패키지 | `persona.md` 무시 중 (P2 §1.3) | P3 에서 읽는다 (대화 성격) |
| `AnimationPlayer` | 없는 행동은 idle 로 대체 | 발화 중 `talk` 행동 (없으면 idle) |

### 1.3 P3에 필요한 것

| # | 항목 | 근거 |
|---|---|---|
| N1 | `IAiProvider` 경계: 기능 탐색(모델 목록), 스트림 요청·취소, 오류 분류 | 기준선 §9.1, ADR-06 |
| N2 | SIWC 인증: 호스트 ID, PKCE, 브라우저 열기, 루프백 콜백, 코드 교환, ID 토큰 검증(RS256/JWKS), 범위 확인, 다시 로그인 | §7.2, §1.1 |
| N3 | 토큰 수명: 요청 직전 갱신(교체되는 refresh 를 원자적으로 저장), 갱신 직렬화 | §1.1 |
| N4 | 로그아웃: revoke 를 확인한 뒤 로컬 삭제, 확인하지 못하면 안내, 등록은 유지 | §1.1 |
| N5 | 보호 저장소: 토큰은 DPAPI(사용자 범위)로 암호화. 로그·URL 에 넣지 않음 | §7.2, §12 |
| N6 | Responses 스트림: SSE 파서, delta·completed·failed·incomplete, 끊김 감지 | §1.1 |
| N7 | 대화 상태: 펫마다 최근 이력(메모리), 페르소나 → `instructions`, 길이 제한 | §7.1, §14 (최근 이력 중심) |
| N8 | 대화창: 포커스를 받을 수 있는 별도 창, 입력·기록·보내기/중지·모델 선택·로그인/로그아웃·plan usage 표시 | §5.2, §7.4, UI 지침 |
| N9 | 말풍선: 펫마다 하나, D2D 텍스트, 스트리밍 표시, 클릭 통과, 자동 숨김, 전체화면·숨김·GPU 재생성 연동 | §5.1, §7.4 |
| N10 | 전역 단축키: `RegisterHotKey`, 기본 `Ctrl+Alt+P`, 충돌하면 로그·안내, 설정으로 변경 | §1.3, §5.2 |
| N11 | 백그라운드 작업 스레드 1개 (네트워크). 결과는 UI 스레드로 메시지 전달, 렌더·물리 비차단, EcoQoS | §8.1, ADR-08, §11.1 "AI" 행 |
| N12 | 오류 안내 UI: 거부·범위 없음·자격 없음·한도 초과·일시 장애·재로그인·네트워크·장시간 응답 | §7.2, §11.1 |
| N13 | 테스트: 모의 SIWC 서버(`127.0.0.1` 전용) + 시나리오·단위 테스트, P1·P2 회귀, 성능 | §11.1 |

### 1.4 P3에 필요 없는 것 (범위 밖)

| 항목 | 이유 / 시기 |
|---|---|
| 화면 관찰(WGC)·Privacy Guard·자율 발화·TTS | P4 |
| §7.3 행동 응답 JSON(감정·행동 명령) | 화면 관찰·자율 발화와 함께 의미가 생긴다 → P4. P3 응답은 일반 텍스트만 |
| 이미지 입력, 도구 호출, WebSocket, Codex app-server | 대화에 필요 없다. 일부는 미지원 기능이다(§1.1) |
| API 키·로컬 모델 Provider | 인터페이스만 둔다. 구현은 이후 |
| 계정 선택기(여러 계정) | 계정 1개만 지원. 다른 계정은 로그아웃 후 다시 로그인 |
| 장기 기억·대화 디스크 저장 | 기준선 §14: 장기 기억은 Opt-in. P3 는 메모리 안의 최근 이력만 |
| Lua 에서 AI 호출 | 권한 분리(ADR-07). 스크립트가 AI·토큰에 접근하지 않는다 |
| 펫끼리의 대화, 대화 공유 | 펫마다 대화를 분리한다(§7.1) |
| 클릭 시 말풍선 즉답(AI 없는 짧은 대사) | 기준선은 짧은 동작에 AI 를 호출하지 않는다. 고정 대사는 P4 의 발화 규칙과 함께 다룬다 |

### 1.5 판단이 필요한 항목 — 추천안으로 진행 (모두 **미확정**)

| # | 항목 | 추천안 (구현 기본값) | 대안 |
|---|---|---|---|
| P3-R1 | JSON 라이브러리 | vcpkg `nlohmann-json` (기준선 고정 버전) | 직접 작성한 파서, glaze 등 |
| P3-R2 | HTTP | **WinHTTP**(OS 내장, TLS·프록시 설정 OS 따름) | libcurl / cpr |
| P3-R3 | 토큰 저장 | 설정 폴더의 `auth\siwc.<issuer해시>.bin`, **DPAPI(CurrentUser)** 로 암호화, 원자적 교체 | Windows 자격 증명 관리자(blob 2560B 제한) |
| P3-R4 | AI 기본값과 클릭 | `[ai] enabled = false` 기본. 끄면 P2 동작과 같다. 켜면 펫 클릭 → 반응 + 그 펫의 대화창 열기. 단축키도 켤 때만 등록 | 켜지 않아도 클릭으로 대화창 열기 |
| P3-R5 | 대화창 | 대화창 1개를 공유하고 대상 펫을 바꾼다. 펫마다 최근 **10턴** 을 메모리에 두고 저장하지 않는다 | 펫마다 창 1개 |
| P3-R6 | 페르소나 | 패키지의 선택 파일 `persona.md`(≤ 16KB) 를 앱 고정 안내문 뒤에 붙여 `instructions` 로 보낸다 | developer 메시지 |
| P3-R7 | 응답 길이 | 클라이언트 상한 **1200자**. 넘으면 스트림을 취소하고 "잘림" 으로 표시(`max_output_tokens` 미지원) | 상한 없음 |
| P3-R8 | 말풍선 | 펫 위쪽에 따로 창. 최대 너비 320px(DPI 배율 적용), 최근 160자, 끝나고 8초 뒤 숨김, 클릭 통과 | 펫 창 Region 안에 포함 |
| P3-R9 | 호스트 ID | `urn:uuid:<v4>` (문서상 지원 형식). 사용자 데이터 폴더에 한 번 생성 | JWK thumbprint |
| P3-R10 | 앱 이름 `agent_name_hint` | `Desktop Pet` | — |
| P3-R11 | 콜백 포트 | 최초 등록 1455, 사용 중이면 "1455 사용 중(Codex CLI 등)" 안내. 다시 로그인할 때는 빈 포트 자동 선택. `[검증]` 등록 포트가 고정인지는 확인하지 않았다 | 항상 1455 |
| P3-R12 | 재시도 | 스트림 시작 전 503 만 1·2·4초 백오프로 3회. 429·403·401 은 재시도 없음 | 재시도 없음 |
| P3-R13 | 로그 | 토큰·대화 원문·인가 URL 은 기록하지 않는다. 길이·오류 코드·상태만 기록 | 디버그 옵션으로 원문 기록 |
| P3-R14 | 단축키 | 기본 `Ctrl+Alt+P`, 설정 `[ai] hotkey`. 대상은 마지막에 상호작용한 펫, 없으면 첫 펫 | — |

---

## 2. 설계

### 2.1 모듈 배치

```
src/ai/                      dp_ai (Windows 비의존, /FI forbid_windows)
  json.h                     nlohmann 래퍼
  siwc_protocol.*            인가 URL·콜백 파싱·form 본문·토큰 응답·범위·ID 토큰 클레임 검증·오류 분류
  sse.*                      증분 SSE 파서 (청크 경계 무관)
  responses.*                요청 JSON 생성(store false·stream true·instructions·input 이력), 스트림 이벤트 해석
  conversation.*             펫별 최근 이력, 길이 상한, 컨텍스트 구성
  provider.h                 IAiProvider / IHttp / ICrypto 인터페이스
src/platform/windows/ai/     dp_ai_win
  winhttp_client.*           동기 요청 + 스트림 읽기 + 다른 스레드에서 취소(핸들 닫기)
  loopback_server.*          127.0.0.1 TCP, /auth/callback 1회 수신
  win_crypto.*               BCrypt: 난수·SHA-256·RSA(PKCS1 v1.5, SHA-256) 검증(JWK n/e)
  credential_store.*         DPAPI 파일 저장(임시 파일 → MoveFileEx WRITE_THROUGH)
  siwc_provider.*            IAiProvider 구현: 로그인·갱신·모델·스트림·로그아웃
  ai_service.*               작업 스레드 1개 + 큐, 결과를 PostMessage 로 UI 에 전달, EcoQoS
apps/desktop-pet/
  chat_window.*              Win32 대화창
  bubble.*                   말풍선 창 (DComp + D2D)
tools/mock-siwc/             모의 SIWC 서버 (C++, Winsock + BCrypt, 127.0.0.1 전용)
```

### 2.2 스레드와 소유권

- UI 스레드는 지금처럼 하나다. 네트워크는 `AiService` 작업 스레드 **1개** 에서 처리한다.
  - 큐가 비면 이벤트를 기다리고 깨어나지 않는다. 그래서 로그인한 채로 쉬는 동안 비용이 0 이어야 한다.
  - 작업 스레드에는 EcoQoS 를 건다.
- 결과 전달: `PostMessage(appWnd, WM_AI_EVENT, 0, (LPARAM)new AiEvent)`. 받는 쪽(UI)이 `unique_ptr` 로 해제한다.
  - 창이 이미 없으면 보내는 쪽이 해제한다. `PostMessage` 실패를 확인한다.
- 취소: UI 스레드가 요청별 취소 플래그를 세우고 WinHTTP 요청 핸들을 닫는다. 그러면 읽기가 즉시 실패한다.
- 종료: `ID_EXIT` → 진행 중인 요청 취소 → 스레드 join (최대 2초). 넘으면 detach 하지 않고 로그를 남긴 뒤 진행한다(WinHTTP 핸들은 이미 닫혀 있음). 모든 시나리오의 종료 5초 조건을 지킨다.
- 갱신: 타이머가 없다. 요청 직전에 만료 120초 이내이면 갱신한다. 작업 스레드가 하나라서 갱신은 자연히 직렬화된다.

### 2.3 인증 흐름

```
[로그인] UI → AiService.SignIn
  host_id 로드/생성 → 저장된 registration(client_id, sub, email, id_token) 확인
  → state·nonce·PKCE verifier 생성 → 루프백 수신 시작(127.0.0.1:1455 또는 빈 포트)
  → 인가 URL 생성(최초: dynamic_agent_client + agent_name_hint / 다시: oaiapp_… + id_token_hint)
  → 브라우저 열기 (ShellExecute)  ※ URL 은 로그에 남기지 않음
  → 콜백 대기(최대 5분, 취소 가능) → state 검증 → error=access_denied 면 중단
  → 코드 교환 → ID 토큰 검증(JWKS RS256, iss·aud·exp·nonce) → 범위 확인
  → 다시 로그인이면 sub 가 같은지 확인 → 저장(원자적) → UI: 로그인됨(plan usage on/off)
[요청] 만료가 가까우면 갱신 → POST /v1/responses (stream) → delta → UI
[로그아웃] 진행 중인 요청 취소 → revoke(refresh) → 200 이면 토큰 삭제
  실패하면 토큰을 유지하고 "원격 해제 미확인" 안내 → 사용자가 "그래도 로컬 삭제" 선택 가능
```

- **엔드포인트 재정의(테스트 전용)**:
  - 명령행 `--ai-endpoint=http://127.0.0.1:<port>` 로만 바꿀 수 있다. `config.toml` 로는 바꿀 수 없다.
  - 재정의하면 issuer·authorize·token·revoke·jwks·api 가 모두 그 주소가 된다.
  - 자격 증명 파일 이름에 **issuer 해시** 를 넣는다. 그래서 테스트가 실제 계정 토큰을 읽거나 보낼 수 없다.
  - 테스트는 `--config` 로 설정 폴더도 분리한다.
- `--ai-open-url-file=path`(테스트 전용): 브라우저를 여는 대신 URL 을 파일에 쓴다.
  - 엔드포인트를 재정의하지 않았으면 이 옵션을 거부한다.
  - 시나리오가 이 URL 을 열어 모의 서버의 "동의" 를 흉내 낸다.

### 2.4 대화·요청

- `input`: `[{role:"user"|"assistant", content}]`, 최근 10턴을 넘으면 앞에서부터 버린다.
- `instructions` 구성:
  - 앱 고정 안내문: 데스크탑 펫 캐릭터라는 것, 짧게 답할 것, 화면·시스템 권한이 없다는 것.
  - 그 뒤에 `persona.md` 를 붙인다.
- 모델: `[ai] model`(slug). 비어 있거나 목록에 없으면 목록의 첫 모델을 쓴다. 목록은 로그인 후 한 번, 그리고 대화창을 열 때 다시 읽는다.
- 응답 처리:
  - 1200자를 넘으면 스트림을 취소하고 `truncated` 로 끝낸다.
  - `response.incomplete` 는 "불완전" 으로 표시한다.
  - 종료 이벤트 없이 끝나면 `interrupted` 로 처리한다.
- 결과 종류(`AiOutcome`):

| 결과 | 의미 |
|---|---|
| `completed` | 정상 완료 |
| `truncated` | 앱 상한(1200자) 초과로 취소 |
| `cancelled` | 사용자 중지 |
| `incomplete` | `response.incomplete` 수신 |
| `interrupted` | 종료 이벤트 없이 스트림 끊김 |
| `failed(code)` | 오류 응답(오류 코드 포함) |

### 2.5 UI

- **대화창** (Win32 표준 컨트롤, 포커스를 받는 창):
  - 상단: 대상 펫 이름, 계정 이메일, `Using ChatGPT plan` · `Manage usage` (링크 → `https://chatgpt.com/settings/usage`)
  - 기록(읽기 전용), 입력, `보내기`/`중지`, 모델 선택, `Continue with ChatGPT` / `로그아웃`
  - 상태 줄에 오류 안내를 표시한다. 한도 초과는 "Usage limit reached — Review your plan or this app's limit in ChatGPT settings" + Manage usage.
  - 첫 로그인(plan usage on) 때 한 번만 "You're using your ChatGPT plan" 안내 + `Got it`. 표시 여부는 설정 폴더에 저장한다.
- **말풍선:**
  - 펫이 발화하는 동안(스트리밍 중) 펫 머리 위에 그 펫의 응답 끝부분을 보여 준다.
  - 펫과 함께 이동한다(같은 30Hz 묶음). 전체화면 숨김·사용자 숨김·GPU 재생성 때 펫과 같이 처리한다.
  - 텍스트가 바뀔 때만 Present 한다. 그래서 정지 상태에서는 Present 0 이다.
- **talk 행동:** 스트리밍 중에는 펫 애니메이션 행동을 `talk` 로 한다. 패키지에 없으면 idle 을 쓰고, 걷기는 멈춘다.

### 2.6 설정 (`[ai]`)

```toml
[ai]
enabled = false          # P3-R4
hotkey = "Ctrl+Alt+P"
model = ""               # 비면 목록의 첫 모델
history_turns = 10
max_reply_chars = 1200
bubble_seconds = 8
```

---

## 3. 작업 분해 (WBS)

| ID | 작업 | 산출물 | 완료 확인 |
|---|---|---|---|
| P3-01 | `dp_ai` 프로토콜·SSE·Responses·대화 (Windows 비의존) | `src/ai/*` | 단위 테스트: URL·콜백(state 불일치·거부·client_id 누락)·form 본문·토큰 응답·범위·클레임 검증·오류 분류표·SSE 청크 경계·이벤트 해석·이력 자르기 |
| P3-02 | 플랫폼: WinHTTP·루프백·BCrypt·DPAPI | `src/platform/windows/ai/*` | 단위 테스트(`dp_platform_tests`): PKCE RFC 7636 벡터, RSA 서명 검증, DPAPI 왕복, 루프백 1회 수신 |
| P3-03 | SIWC Provider + AiService(스레드·취소·종료) | 같음 | 모의 서버 시나리오 |
| P3-04 | 모의 SIWC 서버 | `tools/mock-siwc` | 시나리오가 실패 모드를 전환 |
| P3-05 | 설정 `[ai]`, 페르소나 `persona.md`, `talk` 행동 | config·package | 단위 테스트 |
| P3-06 | 대화창 | `apps/desktop-pet/chat_window.*` | 시나리오(WM_SETTEXT·BM_CLICK 으로 실제 컨트롤 조작) |
| P3-07 | 말풍선 | `apps/desktop-pet/bubble.*` | 시나리오: 표시·숨김·클릭 통과, 정지 Present 0 |
| P3-08 | 전역 단축키 | 앱 | 시나리오: 키 입력 → 대화창 |
| P3-09 | 시나리오 테스트 | `tests/scenario/p3_scenarios.ps1` | §4.2 전 항목 + P1 12개·P2 23개 회귀 |
| P3-10 | 성능 | `tools/measure/p3_ai.ps1` | AI 켬·로그인 상태 정지 = P2 M1 수준, 스트리밍 중 비용 기록 |
| P3-11 | 결과 정리·ADR | `docs/p3_report.md`, ADR, 수동 체크리스트(실계정) | — |

---

## 4. 검증 계획

### 4.1 단위 테스트
P3-01·02·05 항목과 기존 28개 회귀를 확인한다.

### 4.2 시나리오 (모의 서버, 자동)

| ID | 항목 | 판정 |
|---|---|---|
| A1 | 최초 등록 승인 | `dynamic_agent_client`·`agent_name_hint` 전송, 발급 client_id 저장, 로그인됨 plan usage on |
| A2 | 거부 (`access_denied`) | 코드 교환 없음, 로그인 안 됨, 안내 |
| A3 | plan usage 범위 없이 동의 | 로그인은 유지, plan usage off, 요청 차단 안내 |
| A4 | state 불일치 콜백 | 거부, 교환 없음 |
| C1 | 대화 스트림 | delta 순서대로 기록·말풍선 표시, completed |
| C2 | 중지 | cancelled, 앱 정상 |
| C3 | 긴 응답(20초) 중 앱 비차단 | 앱 창 ping 응답, 걷기·물리 로그 계속 |
| C4 | 응답 길이 상한 | truncated |
| C5 | 스트림 중 끊김 | interrupted 안내, 다음 요청 정상 |
| E1 | 401 `invalid_user` | 재로그인 안내 |
| E2 | 403 `user_not_eligible` | 재시도 없음 안내 |
| E3 | `response.failed` 429 한도 | "Usage limit reached" + Manage usage, 재시도 없음 |
| E4 | 503 | 백오프 3회 후 안내 (요청 횟수를 모의 서버 기록으로 확인) |
| T1 | 만료 임박 → 요청 전 갱신 | refresh 요청 1회, 교체된 refresh 저장 |
| T2 | 갱신 영구 오류 (`invalid_grant`) | 토큰 삭제, 재로그인 안내, client_id 유지 |
| S1 | 로그아웃 | revoke 1회(모의 서버 기록), 로컬 토큰 삭제, 등록 유지 |
| S2 | 재실행 후 다시 로그인 | 저장된 `oaiapp_…`·같은 host id·`id_token_hint` 전송 |
| S3 | revoke 실패 | 토큰 유지 + "원격 해제 미확인" 안내 |
| K1 | 전역 단축키 | 대화창 열림 |
| C1e·C6·C7 | (구현 중 추가) 말풍선 클릭 통과 / 스트리밍 중 기록 증가 / 말풍선 갱신 횟수 | WindowFromPoint·기록 길이·`bubble_draws` |
| P1 | 비밀 유출 없음 | 로그에 모의 토큰 문자열·대화 원문 없음, 자격 증명 파일에 평문 토큰 없음 |
| X1 | 종료 | 스트림 중 종료 명령 → 5초 안에 종료 |
| R | 회귀 | P1 12개, P2 23개 (AI 꺼짐 기본) |

### 4.3 성능
- 방식: P2 와 같은 60초 × 3회.
- 조건:
  - AI 켬 + 로그인(모의) 상태로 1마리 정지: P2 M1 과 비교
  - 스트리밍 중: 모의 서버가 20초 동안 초당 10 delta 를 보낸다
- GetProcessTimes 와 사이클 값을 모두 적는다 (P2-M1 미결정).

### 4.4 수동 (실계정, 사용자)
- 실제 "Continue with ChatGPT" 로 로그인한다: 브라우저 승인, 이메일 표시.
- 모델 목록, 대화 1회, 로그아웃.
- ChatGPT 설정에서 앱 연결을 해제한 뒤 재요청해 재로그인 안내가 나오는지 본다.
- 체감 확인: 대화창 포커스, 단축키, 말풍선 가독성.

---

## 5. 구현 중 바뀐 점 (v0.2)

- **WinHTTP 스트림 읽기:** 처음에는 8KB 버퍼를 채울 때까지 기다리는 방식이었다. 이를 `WinHttpQueryDataAvailable` 로 도착한 만큼만 읽는 방식으로 바꿨다.
  - 이전 방식에서는 delta 가 몰려 도착해 말풍선이 응답당 1~2회만 그려졌다.
  - 성능 측정에서 발견했고, 시나리오 C6·C7 을 추가했다.
- **취소 순서:** 요청 직전에 `BeginRequest()` 로 취소 상태를 초기화한 뒤 활성 요청을 확인한다. 갱신 직후 스트림 전송 전에도 취소를 다시 확인한다.
- **대화창 레이아웃:** Manage usage 버튼과 모델 선택이 겹치지 않게 남은 폭을 계산해 배치한다. PrintWindow 캡처로 발견했다.
- **시나리오 추가:** C1e(말풍선 클릭 통과), C6, C7. P1a 에 이메일 유출 검사도 추가했다.

## 6. 보완 (v0.3, 2026-10-14 사용자 결정)

- **로그인 취소 경합·콜백 리스너 잔류 수정**
  - 취소는 플래그와 이벤트로 함께 전달한다. 작업 시작 전·`Listen` 직전에 들어온 취소도 반영된다.
  - 읽는 중인 연결도 `shutdown` 으로 깨운다.
  - 시나리오 L1~L3 (반복 로그인·취소·재로그인, 대기 중 AI 끄기)를 추가했다.
- **타임아웃 분리·설정화**
  - `[ai] first_token_timeout_sec` (기본 120): 시도마다 요청 전송부터 잰다. 503 백오프 대기는 세지 않는다.
  - `[ai] request_timeout_sec` (기본 300): 요청 전체.
  - 결과는 `timed_out`. 시나리오 TO1·TO2 를 추가했다.
- **이력:** 펫별 **20턴 RAM** (`[ai] history_turns` 기본 20). 디스크 저장은 없다.
  - "캐릭터별" 을 펫마다로 해석했다. 같은 캐릭터 펫끼리 공유할지는 확인 대기 (작업 목록 B4).
- **최신 문서·DevKit 대조 반영** (DevKit 은 사실 확인용으로만 열람)
  - 콜백 포트는 `[ai] callback_port`(기본 1455)를 등록·다시 로그인 모두에 쓴다. 다시 로그인 때 사용 중이면 빈 포트를 쓴다(문서 허용).
  - discovery 로 엔드포인트를 받는다. issuer·origin 이 일치할 때만 쓰고, 실패하면 내장값을 쓴다.
  - ID 토큰: `iat` 필수, `azp` 확인, 허용 오차 60초. 갱신 응답의 ID 토큰도 검증한다.
  - plan usage 를 거부했던 등록은 다시 로그인할 때 `prompt=consent` 를 보낸다 `[검증]`.
  - revoke 는 1회 재시도하고, 오류 진단을 위해 `x-request-id` 를 기록한다.
- **실연동 준비**
  - 트레이 "AI 연결 진단 (토큰 갱신·모델 목록)" 을 추가했다.
  - 절차서: [p3_live_verification_ko.md](../p3_live_verification_ko.md)
  - **실제 연동을 확인하기 전까지 P3 를 최종 완료로 처리하지 않는다.**
- **말풍선 갱신 묶기:** 스트리밍 중 최대 10fps(작업 목록 A5).
