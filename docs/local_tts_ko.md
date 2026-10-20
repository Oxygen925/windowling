# 로컬 AI TTS 연결 안내

**작성:** 2026-10-19  
Desktop Pet 은 TTS 엔진·음성 모델을 **동봉하지 않는다.** 사용자가 설치한 엔진에 연결한다.
- 연결 방식은 HTTP(OpenAI 호환)·명령 실행 두 가지다. VOICEVOX 호환 방식은 보류했다 (2026-10-19 결정).
- 엔진이 실패하면(서버 꺼짐·오류) 그 발화는 **읽지 않는다.** Windows 음성으로 대체하지 않는다. 사유는 로그에 `[tts] synth failed engine=… code=…` 로 남는다.

## 1. 추천 엔진 (2026-10-19 조사, 한국어·일본어 기준)

| 용도 | 엔진 | 방식 | 라이선스 | 주의 |
|---|---|---|---|---|
| 한국어·일본어 | **sherpa-onnx + supertonic-3** | 명령 실행 | 코드 Apache-2.0, 모델 OpenRAIL-M | Windows 에서 한글·일본어 인자가 ANSI 코드페이지로 깨질 수 있다 `[검증 필요]` |
| 한국어·일본어 | **Supertonic (`supertonic serve`)** | HTTP `127.0.0.1:7788` | 코드 MIT, 모델 OpenRAIL-M | Python 필요. 원 저장소가 2026-09 archived |
| 일본어 | **Kokoro-FastAPI** | HTTP `127.0.0.1:8880` | Apache-2.0 | 한국어 없음. 일본어 품질은 중간 |

- **비추천:** Piper 한국어·일본어 음성(학습 데이터가 비상업 CC BY-NC-SA), XTTS-v2(비상업 CPML).
- **생성 음성 공개 시:** OpenRAIL-M 은 공개할 때 "기계 생성" 고지를 요구하고, 동의 없는 사칭을 금지한다. 다른 사람 목소리를 복제하는 엔진은 법적·윤리적 위험이 있다.
- 출처와 비교표는 조사 기록(이 문서 작성 시 서브에이전트 조사)에 있다. 지연시간·메모리는 대부분 확인하지 못했다.

### 1.1 GPU 사용자용: Qwen3-TTS (+ RVC) — 연결만 지원

**원칙 (2026-10-19 사용자 결정):** 앱은 **연결만 지원한다.** 엔진·래퍼 서버·스크립트를 제공하지 않고, 서버를 켜고 끄는 것도 사용자가 한다. 아래는 2026-10-19 조사 결과이며 직접 측정한 값이 아니다.

| 항목 | 내용 |
|---|---|
| 모델 | `Qwen/Qwen3-TTS-12Hz-0.6B-CustomVoice`(기본 화자 9명: 한국어 `Sohee`, 일본어 `Ono_Anna`) / `-0.6B-Base`(짧은 음성으로 클론). 1.7B 도 있다. 텍스트 LLM `Qwen3-0.6B` 는 음성을 만들지 못한다 |
| 라이선스 | 코드·가중치 Apache-2.0 |
| 언어 품질 (WER) | 0.6B: 한국어 1.7~3.1, **일본어 6.4~6.9 (약함)**. 일본어가 중요하면 1.7B (3.8~4.9) |
| 속도 (보고값, 2초 문장) | GPU(RTX 4060): 기본 PyTorch 약 9초, CUDA graphs 서버(faster-qwen3-tts) 약 0.9초. CPU: 약 6초 이상, RAM 약 3GB |
| 연결 | 커뮤니티 OpenAI 호환 서버(예: groxaxo/Qwen3-TTS-Openai-Fastapi, qwentts.cpp `tts-server`)의 `/v1/audio/speech` → 이 앱의 `engine = "http"` |
| RVC | 음색만 바꾸는 후처리. 앱은 RVC 를 직접 다루지 않는다. 쓰려면 Qwen3-TTS → RVC 를 한 서버 안에서 처리해 `/v1/audio/speech` 로 WAV 를 돌려주는 래퍼를 사용자가 준비해야 한다 (앱 쪽은 아래 HTTP 설정 그대로) |

설정 예 (값은 서버마다 다르다 `[검증]`):

```toml
[tts]
enabled = true
engine = "http"
http_url = 'http://127.0.0.1:8880/v1/audio/speech'   # 서버가 쓰는 포트
http_model = "qwen3-tts"        # 서버가 요구하는 이름 (없으면 비움)
http_voice = "Sohee"            # 기본 화자, 또는 클론 프로필 (예: 'clone:이름')
http_timeout_sec = 120          # 첫 요청은 모델 로드로 오래 걸린다. 서버의 워밍업 설정을 켜 두면 좋다
```

- 캐릭터마다 다른 목소리: 캐릭터 `manifest.toml` 의 `[voice] ai_voice` 에 화자·클론 이름을 넣는다.
- RVC 의 피치 등 추가 값은 래퍼가 받는 이름으로 `http_extra_json` 에 넣는다 (예: `'{"f0_up_key":0}'`).
- **느림:** CPU 만 있으면 한 문장에 수 초가 걸린다. 발화마다 프로그램을 새로 띄우는 명령 실행 방식은 모델을 매번 읽어 수십 초가 걸리므로 맞지 않는다 (서버를 띄워 HTTP 로 연결).
- **자원:** 서버가 떠 있는 동안 VRAM·RAM 을 GB 단위로 쓴다 (앱과 별도 프로세스).
- **권리 경고:**
  - 본인 목소리, 명시적으로 허락받은 목소리, 합성 화자만 쓴다.
  - 인터넷에 도는 RVC 음성 모델 대부분은 실존 인물·성우·캐릭터 목소리를 허락 없이 쓴 것이다. 출처가 불분명한 모델은 쓰지 말고 결과물을 공개하지 않는다.
  - 한국: 부정경쟁방지법(2022년 시행)이 널리 알려진 타인 음성의 무단 상업적 사용을 규제한다.
  - RVC 기본 가중치(hubert·rmvpe, `lj1995/VoiceConversionWebUI`)에는 "연구용 한정" 조항이 붙어 있다.
  - 이 앱은 음성 모델을 배포하거나 추천하지 않으며, 엔진·모델 사용 책임은 사용자에게 있다.

## 2. 설정 (`%LOCALAPPDATA%\DesktopPet\config.toml`, 다시 시작하면 적용)

경로·JSON 은 TOML **리터럴 문자열**('…')로 쓰면 역슬래시를 두 번 쓰지 않아도 된다.

### HTTP (OpenAI 호환 `/v1/audio/speech`)

```toml
[tts]
enabled = true
engine = "http"
http_url = 'http://127.0.0.1:7788/v1/audio/speech'   # 127.0.0.1·localhost·[::1] 만 허용
http_model = "supertonic-3"     # 비면 보내지 않음
http_voice = "F1"               # 비면 보내지 않음. 캐릭터 [voice] ai_voice 가 우선
http_extra_json = '{"lang":"ko"}'  # 엔진별 추가 필드 (선택)
http_timeout_sec = 60           # 첫 요청은 모델 로드로 느릴 수 있음
```

- 보내는 본문: `{"model", "input", "voice", "response_format":"wav", "speed"}` + 추가 JSON. 설정한 값만 보낸다.
- 응답은 WAV 여야 한다. 샘플레이트는 WAV 헤더 값을 쓴다.

### 명령 실행 (WAV 파일을 쓰는 프로그램)

```toml
[tts]
enabled = true
engine = "command"
command_path = 'C:\tts\sherpa-onnx\bin\sherpa-onnx-offline-tts.exe'
command_args = '--supertonic-duration-predictor=C:\tts\m\duration_predictor.int8.onnx --supertonic-text-encoder=C:\tts\m\text_encoder.int8.onnx --supertonic-vector-estimator=C:\tts\m\vector_estimator.int8.onnx --supertonic-vocoder=C:\tts\m\vocoder.int8.onnx --supertonic-tts-json=C:\tts\m\tts.json --supertonic-unicode-indexer=C:\tts\m\unicode_indexer.bin --supertonic-voice-style=C:\tts\m\voice.bin --sid=0 --lang=ko --speed={speed} --output-filename={out} {text}'
command_stdin = false
command_timeout_sec = 60
```

- 바뀌는 자리: `{text}`(발화), `{text_file}`(발화를 담은 UTF-8 임시 파일), `{out}`(WAV 출력, 필수), `{voice}`, `{speed}`.
- 값은 따옴표 처리해 넘긴다. 임시 파일은 `%TEMP%\DesktopPet\tts` 에 만들고 끝나면 지운다.
- `command_stdin = true` 면 발화를 표준 입력(UTF-8)으로도 넘긴다.
- 엔진이 한글·일본어 인자를 깨뜨리면 `{text_file}` 이나 `command_stdin` 을 지원하는 엔진을 쓴다.
- **실행 파일은 `.exe`·`.bat`·`.cmd` 만** 쓸 수 있다 (보안 점검 2026-10-20).
  - `.bat`·`.cmd` 는 cmd.exe 가 명령줄을 해석하므로 발화(`{text}`)·음성(`{voice}`)을 인수로 넘길 수 없다 — 발화는 `{text_file}` 이나 `command_stdin = true` 로 받는다. 예: `command_args = '{text_file} {out}'` 에 배치 안에서 `python tts.py --in %1 --out %2`.
  - 배치에 넘기는 경로(`{out}`·`{text_file}`)에 `" % ! ^ & | < >` 가 있으면(예: 사용자 이름) 읽지 않는다.
  - 발화가 `-`·`/` 로 시작하면 앞에 공백을 붙여 넘긴다 (옵션으로 읽히지 않게).
  - 시간 제한·취소 때 엔진이 띄운 하위 프로세스까지 끝낸다.

### 캐릭터별 음성 (캐릭터 패키지 `manifest.toml`)

```toml
[voice]
name = "Heami"       # Windows 음성 표시 이름 일부 (engine = "winrt")
ai_voice = "F1"      # 로컬 AI 엔진 voice id (http "voice", command {voice})
rate = 1.1           # 0.5~2.0, 없으면 [tts] rate
```

## 3. 확인 방법

- 트레이 "펫의 말을 음성으로 읽기 (로컬 TTS)" 를 켜거나 `[tts] enabled = true`.
- 로그: 성공 `[tts] spoke engine=http … completed=true`, 실패 `[tts] synth failed engine=http code=http_500` (`network_…`, `not_wav`, `url_not_local`, `timeout`, `exit_N` 등).
- 자동 시험: 모의 서버로 HTTP·명령 실행·실패를 확인했다 (`tests/scenario/p4_scenarios.ps1` T2·T3·T4). **실제 엔진으로는 확인하지 않았다.**
