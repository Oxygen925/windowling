# ADR-06 세부: AI Provider — Sign in with ChatGPT 연동 — 잠정

- 상태: **잠정** (2026-10-14).
  - 기준선 ADR-06("AI Provider·Capture·TTS 분리")의 P3 세부안이다.
  - P3-R1~R14 는 사용자 확인 전 추천안이다.
  - **실제 계정 연동은 아직 검증하지 않았다** (모의 서버로만 검증).
- 관련: [P3 계획](../plan/desktop_pet_p3_detailed_plan_ko.md) §1.1(문서 조사), [P3 보고서](../p3_report.md)

## 결정

- **계층**
  - `dp_ai` (Windows 비의존): 프로토콜 규칙 전부.
    - 인가 URL, 콜백 검증(state 를 가장 먼저 확인), form 본문, 토큰 응답, 범위, ID 토큰 클레임, 오류 분류
    - SSE 파서, Responses 요청·이벤트, 대화 이력
  - `dp_ai_win`: Windows 에 의존하는 부분.
    - 네트워크(WinHTTP), 루프백 콜백(Winsock, 127.0.0.1 전용)
    - 암호(BCrypt: 난수·SHA-256·RS256 검증), 저장(DPAPI)
    - 작업 스레드
- **스레드**
  - 네트워크는 작업 스레드 1개에서만 처리한다(EcoQoS).
  - 큐가 비면 이벤트를 기다리므로 쉬는 동안 깨어나지 않는다. 갱신 타이머도 두지 않고, 요청 직전에 만료 120초 이내이면 갱신한다.
  - 결과는 `PostMessage` 로 UI 스레드에 전달하고, 받는 쪽이 해제한다.
  - 취소는 UI 스레드에서 WinHTTP 요청 핸들을 닫아 처리한다.
  - 스트림 수신은 `WinHttpQueryDataAvailable` 로 도착한 양만큼만 `WinHttpReadData` 한다. 동기 모드에서 큰 버퍼를 한 번에 요청하면 버퍼가 찰 때까지 기다려 delta 가 몰려 도착한다(P3 에서 확인한 문제).
  - 종료 시 작업 스레드를 최대 2초 기다린다.
- **인증**
  - 문서 계약을 따른다: 최초 등록은 `dynamic_agent_client` + `agent_name_hint="Desktop Pet"`, 이후에는 발급 `client_id` 를 재사용한다.
  - 호스트 ID 는 `urn:uuid` 형식이다.
  - PKCE S256 을 쓰고, 시도마다 state·nonce 를 새로 만든다.
  - ID 토큰은 JWKS(RS256)로 서명을 검증하고 iss·aud·exp·nonce 를 확인한다.
  - 다시 로그인했을 때 `sub` 가 등록된 계정과 다르면 저장하지 않는다.
  - plan usage 범위(`chatgpt.tokens.use.direct`)가 없으면 로그인은 유지하되 추론은 금지한다.
- **토큰 저장**
  - 위치: 설정 폴더 `auth\siwc-<issuer 해시>.bin`.
  - DPAPI(CurrentUser + 앱 entropy)로 암호화하고, 임시 파일 → `MoveFileEx(WRITE_THROUGH)` 로 원자적으로 교체한다.
  - 교체된 refresh 토큰은 새 access 토큰을 쓰기 전에 먼저 저장한다.
  - 일시 장애(5xx·네트워크)에는 삭제하지 않는다. 영구 갱신 오류일 때만 토큰을 지우고 등록은 유지한다.
- **로그아웃**
  - revoke 가 200 이면 로컬 토큰을 삭제한다.
  - 실패하면 토큰을 유지하고, "로컬에서만 로그아웃"을 고르면 "원격 해제 미확인" 안내를 띄운다.
- **추론**
  - `POST /v1/responses`, `store:false`, `stream:true`, `instructions`(앱 고정 안내 + `persona.md`), `input` 에 최근 10턴.
  - 미지원 필드는 보내지 않는다.
  - `response.completed` 를 받아야 성공으로 친다. 그 밖의 결과는 다음과 같이 구분한다.

| 결과 | 조건 |
|---|---|
| truncated | 앱 상한 1200자에 도달 |
| cancelled | 사용자가 중지 |
| incomplete | `response.incomplete` |
| interrupted | 종료 이벤트 없이 스트림이 끊김 |
| failed(code) | 오류 응답 |

- **재시도**
  - 스트림 시작 전 503 계열만 1·2·4초 백오프로 3회 재시도한다.
  - 401 은 토큰을 한 번 갱신하고 재시도한다.
  - 429·403 은 재시도하지 않는다.
- **테스트 격리**
  - 엔드포인트 재정의는 명령행 `--ai-endpoint=http://127.0.0.1:<port>` 로만 할 수 있다.
  - 자격 증명은 issuer 별 파일이고 테스트는 `--config` 로 폴더도 분리하므로, 실제 토큰이 모의 서버로 갈 수 없다.
  - `--ai-open-url-file` 은 재정의가 있을 때만 쓸 수 있다.
- **로그**
  - 토큰·인가 URL(`id_token_hint` 포함 가능)·대화 원문은 기록하지 않는다.
  - 길이·결과·오류 코드만 남긴다.

## 근거 (확인한 것)

- **단위 테스트**
  - URL·콜백·form 본문·토큰 응답·클레임·오류 분류표·SSE 청크 경계·요청 형식(미지원 필드 없음)·이력·UTF-8·설정 `[ai]`
  - PKCE(RFC 7636 부록 B 벡터), RS256 서명·검증, DPAPI 왕복(평문 없음·변조 거부), 루프백 콜백·취소·포트 충돌
- **시나리오** (모의 서버, 36/36, `logs/p3/20261014_013254_scenarios`)
  - 승인·거부·state 불일치·범위 없음, 다시 로그인·재실행 후 다시 로그인
  - 스트림·중지·긴 응답 중 비차단·상한·끊김
  - 401·403·429(`response.failed`)·503 백오프, 요청 전 갱신·갱신 영구 오류
  - 로그아웃·revoke 실패
  - 스트리밍 실시간 표시(C6)·말풍선 갱신(C7)·말풍선 클릭 통과(C1e)
  - 단축키, 스트림 중 종료(45ms), 로그(토큰·URL·대화·이메일)·자격 증명 파일의 비밀 유출 없음

## 미확정·미검증

- **실제 OpenAI 서버와의 연동 전체 `[검증]`** — 사용자 수동 체크리스트로 확인한다.
  - 특히 확인하지 않은 것:
    - 토큰 교환의 `grant_type` 필드명: 문서는 "authorization_code grant" 라고만 쓴다.
    - 최초 등록 포트 1455 가 필수인지: 문서는 예시로 쓴다.
    - 실제 오류 응답 본문 형식
    - `/v1/models` 응답 형식
    - 스트림 이벤트 세부
- WinHTTP 동기 호출 중 다른 스레드에서 핸들을 닫는 취소: 이 PC(모의 서버, HTTP)에서는 확인했다. TLS·프록시 환경은 확인하지 않았다.
- 읽기 1회당 수신 제한 시간 60초: 추론이 긴 모델이 첫 토큰 전에 60초 넘게 조용하면 `interrupted`(stream_ended) 가 될 수 있다. `[검증]` 실제 모델에서 확인한 적 없다.
- 대화창 전경 전환: 다른 앱이 전경일 때 `SetForegroundWindow` 가 실패할 수 있다(Windows 전경 잠금 규칙). 시나리오에서는 성공을 기록했다.
- 여러 계정, 계정 선택기, API 키 Provider, 장기 기억: P3 범위 밖

## 추가 (2026-10-14, v0.3)

- **엔드포인트:** discovery(`/.well-known/openid-configuration`)로 받는다. issuer 와 모든 엔드포인트의 origin 이 일치할 때만 반영하고, 실패하면 내장값(설정 문서 기재값)을 쓴다.
- **콜백 포트:** `[ai] callback_port`(기본 1455)를 등록·다시 로그인에 쓴다. 다시 로그인에서만 포트가 사용 중이면 빈 포트를 쓴다.
- **취소:** UI 스레드에서 플래그와 이벤트로 함께 전달한다. `Listen` 전에 들어온 취소도 사라지지 않고, 읽는 중인 콜백 연결은 `shutdown` 으로 깨운다.
- **타임아웃:** 첫 응답(시도마다, 백오프 제외)과 요청 전체를 스레드풀 타이머로 감시해 `timed_out` 으로 끝낸다.
- **ID 토큰:** `iat` 필수, `azp` 일치, 허용 오차 60초. 갱신 응답에 새 ID 토큰이 있으면 서명과 `sub` 일치를 확인한다.
- **재동의:** plan usage 범위 없이 동의한 등록은 다시 로그인할 때 `prompt=consent` 를 보낸다(DevKit 동작, 문서 미기재 `[검증]`).
- **revoke:** 5xx·네트워크 오류일 때 300ms 뒤 1회 재시도한다.
- **진단:** 오류 로그에 `x-request-id` 를 남긴다. 트레이 "AI 연결 진단" 은 토큰을 강제로 갱신하고 모델 목록을 다시 받는다.
- **스트림 수신:** `WinHttpQueryDataAvailable` 로 도착한 만큼만 읽는다(실시간 표시).
- **시나리오:** 모의 서버 47/47.
- **실연동 절차:** [p3_live_verification_ko.md](../p3_live_verification_ko.md)

## 추가 (2026-10-14, P4: 화면 관찰·TTS)

- **공급자 경계:** `IAiProvider`(`platform/windows/ai/ai_provider.h`). 요청은 공급자 중립 `ai::AiRequest`(모델·안내문·이력·이미지·JSON 스키마)이고 SIWC 구현이 Responses 본문으로 바꾼다. 로그인·로그아웃은 SIWC 전용으로 남긴다.
  - 로컬 LLM(OpenAI 호환 로컬 HTTP 등)은 이 경계로 추가한다. P4 는 인터페이스만 (사용자 결정).
  - `ProviderCaps.sendsOffDevice`: 동의 문구에 반영할 자리 (로컬 공급자면 "PC 밖으로 나가지 않음").
- **관찰 요청:** 같은 단일 요청 슬롯을 쓴다. 대화가 시작되면 관찰은 취소된다.
- **기능 거부 처리:** 400 + `error.param` 이 `text…` 면 구조화 출력 거부 → 안내문 JSON. `…image…` 면 그 모델에서 관찰 중지. (문서상 지원으로 보이나 `[검증]`)
- **캡처:** WGC, 모니터 1프레임, 캡처마다 별도 D3D11 Device, 캡처 순간에만 `WDA_EXCLUDEFROMCAPTURE`. 상세는 [P4 계획](../plan/desktop_pet_p4_detailed_plan_ko.md) §2.3.
- **TTS:** `ITtsProvider` + WinRT `SpeechSynthesizer` → XAudio2. 출력 큐 1개. 로컬 AI TTS 는 이 경계로 추가한다.
- **동의:** SIWC Terms "Obtain express consent before background use" → 동의 버전을 기록하고, 버전이 바뀌면 다시 묻는다.

## 추가 (2026-10-19, 로컬 AI TTS)

- `ITtsProvider` 구현 3개: `winrt`(Windows 음성), `http`(OpenAI 호환 `/v1/audio/speech`, 로컬 주소만), `command`(WAV 를 쓰는 실행 파일, 인자 템플릿).
- 엔진·모델은 동봉하지 않는다 → 엔진 라이선스(GPL·OpenRAIL-M 등)가 앱에 옮겨 오지 않는다.
- 실패한 발화는 읽지 않는다 (대체 엔진 없음, 사용자 결정). VOICEVOX 호환 방식은 보류.
- 캐릭터 manifest `[voice]` 가 전역 `[tts]` 보다 우선한다.
- 안내: [local_tts_ko.md](../local_tts_ko.md)
