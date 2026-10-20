# P1 수동 확인 체크리스트

자동 시나리오(`tests/scenario/p1_scenarios.ps1`, 12/12 통과)로 확인할 수 없는 항목이다. 결과는 [p1_report.md](p1_report.md) §5에 옮긴다.

- 실행 파일: `build\x64-release\desktop-pet.exe` (빌드: `powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset x64-release`)
- 로그: `%LOCALAPPDATA%\DesktopPet\logs\` (트레이 메뉴 → 로그 폴더 열기)
- 설정: `%LOCALAPPDATA%\DesktopPet\config.toml` (트레이 메뉴 → 설정 파일 열기, 수정 후 재시작)
- 종료: 트레이 아이콘 → 종료

```bash
build/x64-release/desktop-pet.exe
```

## 1. 기본 동작

| # | 확인 | 기록 |
|---|---|---|
| 1-1 | 펫이 위에서 떨어져 창 상단 또는 작업 표시줄 위에 착지 | |
| 1-2 | 가끔 걷고·앉고·멈춤, 노출된 창 상단 끝에서 방향을 바꿈 | |
| 1-3 | 커서를 가까이 대면 표정(look) 변화 | |
| 1-4 | 클릭 → 잠깐 반응, 드래그 → 들려 있다가 놓으면 떨어짐 | |
| 1-5 | 펫 투명 부분(귀 사이·몸 옆) 클릭이 뒤 창에 전달됨, 펫 클릭 시 다른 창 포커스 유지 | |
| 1-6 | 왼쪽으로 걸을 때(반전 표시)도 1-5가 같게 동작 | |
| 1-7 | 우클릭 메뉴: 숨기기/보이기, 행동 일시정지, 설정·로그 열기, 창 목록 다시 읽기 | |

## 2. 창 상호작용 (일상 사용)

| # | 확인 | 기록 |
|---|---|---|
| 2-1 | 펫이 서 있는 창을 손으로 끌어 이동 → 따라옴 | |
| 2-2 | 그 창 위로 다른 창을 클릭해 올림 → 잠시 뒤 펫이 떨어짐 (유예 250ms 체감 적정성) | |
| 2-3 | 창 최대화 → 펫이 화면 밖으로 나가지 않음 | |
| 2-4 | 작업 관리자 등 관리자 권한 창 위 착지·투명 영역 클릭 | |
| 2-5 | 가상 데스크탑 전환(Win+Ctrl+→) → 다른 데스크탑 창에 서 있던 펫이 떨어짐 | |

## 3. 세션·디스플레이 상태 (P1-08)

| # | 확인 | 로그에서 찾을 줄 | 기록 |
|---|---|---|---|
| 3-1 | `Win+L` 잠금 → 해제 | `일시정지 ... (session lock)`, `재개 (session unlock)` | |
| 3-2 | 절전 → 복귀 | `(suspend)`, `(resume)` | |
| 3-3 | 작업 관리자에서 Windows 탐색기 다시 시작 → 트레이 아이콘 복구, 펫이 작업 표시줄 위 유지 | `TaskbarCreated` | |
| 3-4 | 배율 100→125→150→100% → 펫 크기 변화, 발 위치 정상 | `DPI 96→120` 등 | |
| 3-5 | 작업 표시줄 자동 숨김 켜기/끄기 → 바닥 높이 변경 | `SPI_SETWORKAREA` | |

## 4. 전체화면 (P1-07)

| # | 확인 | 기록 |
|---|---|---|
| 4-1 | 브라우저 F11 / 동영상 전체화면 → 펫 숨김, 해제 → 다시 떨어지며 표시 | |
| 4-2 | 설정 `always_show_exe = ["vlc.exe"]` 등 예외 → 숨기지 않음 | |

### 4.1 직접 출력 복귀 시간 측정 (P1 완료 조건, P1-D5) — 관리자 PowerShell

1. Mindustry를 전체화면으로 실행하고 메인 메뉴에 둔다.
2. **관리자 PowerShell**에서:

```powershell
powershell -ExecutionPolicy Bypass -File C:\Claude_code\desktop_pet\tools\measure\p1_fullscreen_recovery.ps1
```

3. 약 1분 동안 스크립트가 게임 ↔ 일반 창 전환을 3회씩, 펫 없음(nopet)·펫 실행(pet) 두 조건으로 반복한다. 마우스·키보드를 만지지 않는다.
4. 끝나면 `logs\p1\<시각>_fsrecovery\summary.txt`에 조건별 복귀 시간(`RecoverMs`, 미복귀 포함), 합성 비율, 프레임 시간 p95/p99가 나온다.
5. 측정이 끝나면 Mindustry 설정 → Graphics → Fullscreen 을 원래대로 돌린다.

참고: PresentMon 시각과 스크립트 시각 사이에 수백 ms 정렬 오차가 있을 수 있다 `[검증]`. 고정 임계값은 없고 기록·비교만 한다.
