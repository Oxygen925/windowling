# P4 화면 관찰·자율 발화·TTS 시나리오 (P4 계획 §4). 모의 SIWC 서버(127.0.0.1)만 사용하며 실제 OpenAI 로는 아무것도 보내지 않는다.
# 주의: 실제 화면을 캡처한다 (모의 서버로만 전송). 검증용 캡처 1장(--observe-dump)은 임시 폴더에 쓰고 끝나면 지운다.
#       TTS 확인 때 짧은 음성이 1번 재생된다. 제외 앱 확인용 테스트 창(dpexcl.exe = 복사한 powershell)을 잠깐 띄운다.
#       사용자 프로그램은 제외 목록에 넣거나 종료하지 않는다. PC 를 만지지 않는다.
param([string]$Preset = "x64-release", [switch]$KeepDump)
$Phase = "p4"
. (Join-Path $PSScriptRoot "common.ps1")
$tmp = Join-Path $env:TEMP ("dp_p4_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force $tmp | Out-Null

Add-Type -Namespace P4 -Name N -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowW")] public static extern System.IntPtr FindWindowT(string cls, string title);
[DllImport("user32.dll")] public static extern System.IntPtr GetDlgItem(System.IntPtr h, int id);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr SendMessageW(System.IntPtr h, uint m, System.IntPtr w, string l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr SendMessageW(System.IntPtr h, uint m, System.IntPtr w, System.Text.StringBuilder l);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
'@
$WM_COMMAND = 0x111; $WM_CLOSE = 0x10
function App { [P4.N]::FindWindowW("DesktopPetDev.App", [IntPtr]::Zero) }
function Chat { [P4.N]::FindWindowW("DesktopPetDev.Chat", [IntPtr]::Zero) }
function AppCommand($id) { $null = [P4.N]::PostMessageW((App), $WM_COMMAND, [IntPtr]$id, [IntPtr]::Zero) }
function ChatClick($id) { $c = Chat; $null = [P4.N]::PostMessageW($c, $WM_COMMAND, [IntPtr]$id, [P4.N]::GetDlgItem($c, $id)) }
function ChatText($id) {
    $h = [P4.N]::GetDlgItem((Chat), $id); $n = [int][P4.N]::SendMessageW($h, 0x0E, [IntPtr]::Zero, [string]$null)
    $sb = New-Object System.Text.StringBuilder ($n + 2); $null = [P4.N]::SendMessageW($h, 0x0D, [IntPtr]($n + 1), $sb); $sb.ToString()
}
# 동의 창(UI-06, DesktopPetDev.Consent): 동의 = "이해했습니다" 체크(404) 뒤 "동의하고 켜기"(405), 거부 = 취소(IDCANCEL)
# -TryUnchecked: 체크 없이 동의 명령을 먼저 보내 무시되는지(창이 그대로인지) 확인한 뒤 답한다
function AnswerConsent([bool]$agree, [switch]$TryUnchecked) {
    for ($i = 0; $i -lt 50; $i++) {
        $d = [P4.N]::FindWindowW("DesktopPetDev.Consent", [IntPtr]::Zero)
        if ($d -ne [IntPtr]::Zero) {
            if ($TryUnchecked) {
                $null = [P4.N]::PostMessageW($d, $WM_COMMAND, [IntPtr]405, [P4.N]::GetDlgItem($d, 405))
                Start-Sleep -Milliseconds 500
                if ([P4.N]::FindWindowW("DesktopPetDev.Consent", [IntPtr]::Zero) -eq [IntPtr]::Zero) { return $false }
            }
            if ($agree) {
                $null = [P4.N]::SendMessageW([P4.N]::GetDlgItem($d, 404), 0xF5, [IntPtr]::Zero, [string]$null)  # BM_CLICK
                $null = [P4.N]::PostMessageW($d, $WM_COMMAND, [IntPtr]405, [P4.N]::GetDlgItem($d, 405))
            } else {
                $null = [P4.N]::PostMessageW($d, $WM_COMMAND, [IntPtr]2, [P4.N]::GetDlgItem($d, 2))
            }
            return $true
        }
        Start-Sleep -Milliseconds 100
    }
    return $false
}
$ID_OBSERVE_TOGGLE = 113; $ID_OBSERVE_PAUSE_ON = 116; $ID_OBSERVE_RESUME = 117; $ID_TTS_TOGGLE = 119

# ---- 모의 서버 ----
Start-Mock $tmp

# ---- 설정 (임시): 자리 비움 검사 끔(자동 테스트는 입력이 없음), 테스트 창을 제외 앱으로 ----
$cfgDir = Join-Path $tmp "cfg"; New-Item -ItemType Directory -Force $cfgDir | Out-Null
$cfg = Join-Path $cfgDir "config.toml"
function WriteConfig($perHour, $tts = "volume = 0.3") {
    [IO.File]::WriteAllText($cfg, "[display]`nproximity_reaction = false`n[ai]`nhotkey = `"Ctrl+Alt+F9`"`nbubble_seconds = 2`nfirst_token_timeout_sec = 5`nrequest_timeout_sec = 15`n" +
        "[observe]`nrequire_activity_min = 0`nmax_per_hour = $perHour`nmax_per_day = 200`nexclude_exe = [`"dpexcl.exe`"]`n[tts]`n$tts`n")
}
WriteConfig 50
$stateFile = Join-Path $cfgDir "observe_state.json"
$urlFile = Join-Path $tmp "authorize_url.txt"
$dump = Join-Path $tmp "observe_dump.jpg"

# ---- 로그 ----
# 관찰은 2초마다 계속된다: 모의 서버 모드를 바꾼 "뒤에 시작한" 요청의 결과만 본다 (Mark → start → 그 뒤 done)
function NextObserve($sec = 12) {
    Mark
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        $lines = @(PetLines | Select-Object -Skip $script:mark)
        $si = -1
        for ($k = 0; $k -lt $lines.Count; $k++) { if ($lines[$k] -match "\[observe\] start pet=\d+ request=(\d+)") { $si = $k; $req = $Matches[1]; break } }
        if ($si -ge 0) {
            for ($k = $si + 1; $k -lt $lines.Count; $k++) { if ($lines[$k] -match "\[observe\] done pet=\d+ request=$req ") { return $lines[$k] } }
        }
        Start-Sleep -Milliseconds 100
    }
    return $null
}
function State { if (Test-Path $stateFile) { Get-Content $stateFile -Raw | ConvertFrom-Json } else { $null } }

function StartPet($pets = 1) {
    $before = LogNames
    $script:petLog = $null
    $p = Start-Process $pet -ArgumentList @("--config=$cfg", "--ai", "--ai-endpoint=$base", "--ai-open-url-file=$urlFile",
        "--observe-interval-sec=2", "--observe-dump=$dump", "--no-behavior", "--pets=$pets", "--character=test-blue",
        "--spawn=1500,150", "--duration=600") -PassThru
    $null = Find-NewLog $before
    $script:allLogs += $script:petLog
    $script:mark = 0
    $null = WaitLog "\[ai\] enabled endpoint=loopback-test" 8
    $null = WaitLog "\[pet0\] event=Landed" 6
    return $p
}
# 대조 시험용: 펫 1마리, 위치·추가 인자 지정 (대화창을 열지 않음)
function StartPet1($spawn, $extra = $null) {
    $before = LogNames
    $script:petLog = $null
    $args2 = @("--config=$cfg", "--ai", "--ai-endpoint=$base", "--observe-interval-sec=2", "--observe-dump=$dump", "--no-behavior",
        "--pets=1", "--character=test-blue", $spawn, "--duration=120")
    if ($extra) { $args2 += $extra }
    $p = Start-Process $pet -ArgumentList $args2 -PassThru
    $null = Find-NewLog $before
    $script:allLogs += $script:petLog
    $script:mark = 0
    $null = WaitLog "\[pet0\] event=Landed" 8
    return $p
}
function StopPet($p) {
    AppCommand 106
    $exited = $p.WaitForExit(8000)
    if (-not $exited) { Stop-Process -Id $p.Id -Force }
    return $exited
}
function SignIn {
    AppCommand 110  # 대화창 (로그인 버튼)
    for ($i = 0; $i -lt 30 -and (Chat) -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100 }
    Remove-Item $urlFile -ErrorAction SilentlyContinue
    Mark
    ChatClick 211
    for ($i = 0; $i -lt 50 -and -not (Test-Path $urlFile); $i++) { Start-Sleep -Milliseconds 100 }
    $u = (Get-Content $urlFile -Raw).Trim()
    try { $null = Invoke-WebRequest -UseBasicParsing $u -TimeoutSec 10 } catch { }
    $r = WaitLog "\[ai\] ui signin ok=" 8
    $null = WaitLog "\[ai\] ui models" 5
    $null = [P4.N]::PostMessageW((Chat), $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)  # 대화창이 앞에 있으면 관찰하지 않는다 (G9)
    Start-Sleep -Milliseconds 300
    return $r
}

[void][P4.N]::SetCursorPos(2400, 200)
$notepad = $null
try {
    $p = StartPet
    $r = SignIn
    # ---- 기본값: 꺼짐 ----
    Mark; Start-Sleep -Seconds 5
    $starts = SinceMark "\[observe\] (start|skip)"
    Check "O0" "기본값 꺼짐: 로그인해도 관찰 시도·캡처 0회" ($r -match "ok=true" -and $starts.Count -eq 0 -and (StatN "observe") -eq 0 -and -not (Test-Path $stateFile)) "attempts=$($starts.Count) observe=$(StatN 'observe')"

    # ---- 동의 거절 ----
    Mark; AppCommand $ID_OBSERVE_TOGGLE
    $shown = AnswerConsent $false -TryUnchecked
    $d = WaitLog "\[observe\] consent declined" 3
    Start-Sleep -Seconds 3
    Check "O1" "동의 창: 체크 없이 동의는 무시, 취소 → 꺼진 채로, 캡처 0회" ($shown -and $d -and (SinceMark "\[observe\] start").Count -eq 0 -and -not (Test-Path $stateFile)) "$d"

    # ---- 동의 → 관찰 ----
    Mark; AppCommand $ID_TTS_TOGGLE; $null = WaitLog "\[tts\] enabled" 2
    Mark; AppCommand $ID_OBSERVE_TOGGLE
    $shown = AnswerConsent $true
    $en = WaitLog "\[observe\] enabled consent_version=1" 3
    $st = State
    Check "O2" "동의 화면에서 예 → 동의 버전·시각 저장" ($shown -and $en -and $st.enabled -eq $true -and $st.consent_version -eq 1 -and $st.consented_at) "$en"
    $done = WaitLog "\[observe\] done" 10
    $cap = SinceMark "\[observe\] capture ok" | Select-Object -First 1
    Start-Sleep -Milliseconds 300
    $lr = (Stats).last_responses
    Check "O3" "관찰 1회: 모니터 1프레임 → JPEG(축소)·detail low·구조화 출력 → 검증 후 발화·행동" ($done -match "result=spoke" -and $done -match "behavior=sit" -and $cap -and $lr.has_image -and $lr.image_jpeg -and $lr.image_detail -eq "low" -and $lr.has_text_format -and $lr.store -eq $false -and $lr.stream -eq $true) "$cap | $done"
    $m = [regex]::Match("$cap", "out=(\d+)x(\d+)")
    Check "O3b" "축소: 긴 변 1024px 이하" ($m.Success -and [math]::Max([int]$m.Groups[1].Value, [int]$m.Groups[2].Value) -le 1024) "$($m.Value)"
    $tts = WaitLog "\[tts\] spoke" 15
    Check "T1" "TTS 켜짐: 발화를 음성으로 1회 재생 (출력 큐)" ($tts -match "completed=true") "$tts"

    # ---- 응답 검증·기능 거부 ----
    Mock "observe=invalid"
    $done = NextObserve
    Check "V1" "JSON 이 아닌 응답 → 버림 (말풍선 없음)" ($done -match "result=invalid_reply reason=invalid_json") "$done"
    Mock "observe=reject_format"
    $done = NextObserve
    $lr = (Stats).last_responses
    Check "V2" "구조화 출력 거부(400 text.format) → 안내문 JSON 으로 재시도해 발화" ($done -match "result=spoke" -and (SinceMark "구조화 출력").Count -ge 1 -and -not $lr.has_text_format) "$done"
    Mock "reset=1"

    # ---- 제외 앱: 이름이 고유한 테스트 창 프로세스 (펫과 같은 모니터) ----
    $excl = Join-Path $tmp "dpexcl.exe"
    Copy-Item "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" $excl
    $notepad = Start-Process $excl -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "target_window.ps1"),
        "-EventLog", (Join-Path $tmp "excl_events.log"), "-X", "1200", "-Y", "400", "-W", "300", "-H", "200", "-Title", "P4-excluded") -PassThru
    Start-Sleep -Seconds 2
    Mark
    $sk = WaitLog "\[observe\] skip reason=excluded_app" 8
    Check "G6" "제외 앱 창이 그 모니터에 보이면 건너뜀" ($sk -and (SinceMark "\[observe\] start").Count -eq 0) "$sk"
    Stop-Process -Id $notepad.Id -Force -ErrorAction SilentlyContinue; $notepad = $null
    Start-Sleep -Milliseconds 800

    # ---- 일시정지 ----
    Mark; AppCommand $ID_OBSERVE_PAUSE_ON
    $sk = WaitLog "\[observe\] skip reason=paused" 6
    Check "G3" "일시정지 → 건너뜀" ($sk -and (SinceMark "\[observe\] start").Count -eq 0) "$sk"
    AppCommand $ID_OBSERVE_RESUME

    # ---- 진행 중 중지 ----
    Mock "observe=slow"
    Mark
    $s = WaitLog "\[observe\] start" 8
    Start-Sleep -Milliseconds 1500
    AppCommand $ID_OBSERVE_PAUSE_ON
    $c = WaitLog "\[observe\] cancel request=\d+ reason=paused" 3
    Start-Sleep -Seconds 2
    Check "G3b" "요청 진행 중 일시정지 → 즉시 취소, 발화 없음" ($s -and $c -and (SinceMark "result=spoke").Count -eq 0) "$c"
    Mock "reset=1"
    AppCommand $ID_OBSERVE_RESUME

    # ---- 끄기 ----
    Mark; AppCommand $ID_OBSERVE_TOGGLE
    $off = WaitLog "\[observe\] disabled" 3
    $n0 = StatN "observe"; Start-Sleep -Seconds 5
    Check "O4" "끄기 → 저장(enabled=false), 이후 시도 0회" ($off -and (State).enabled -eq $false -and (StatN "observe") -eq $n0 -and (SinceMark "\[observe\] (start|skip)").Count -eq 0) "$off"
    Check "X0" "정상 종료" (StopPet $p) ""

    $sentRun1 = @((State).sent_unix).Count

    # ---- 재실행: 꺼짐 유지, 전송 기록 유지 → 다시 켜면 시간당 상한 ----
    WriteConfig 2
    $p = StartPet  # 자격 증명이 남아 있어 로그인된 채로 시작한다
    $null = WaitLog "\[observe\] state" 3
    Mark; Start-Sleep -Seconds 4
    Check "O5" "재실행 후에도 꺼짐 유지 (설정 파일로는 켜지지 않음)" ((PetLines | Where-Object { $_ -match "\[observe\] state on=false consent_version=1" }).Count -eq 1 -and (SinceMark "\[observe\] start").Count -eq 0) "sent_run1=$sentRun1"
    Mark; AppCommand $ID_OBSERVE_TOGGLE
    $null = AnswerConsent $true
    $sk = WaitLog "\[observe\] skip reason=hourly_limit" 6
    Check "G8" "시간당 상한(전송 기준)은 재실행해도 유지 → 건너뜀" ($sentRun1 -ge 2 -and $sk -and (SinceMark "\[observe\] start").Count -eq 0) "$sk"
    Check "X1" "정상 종료" (StopPet $p) ""

    # ---- 펫 2마리: 회차마다 1마리만, 같은 펫이 연달아 말하지 않음. 표시 ----
    WriteConfig 50
    $p = StartPet 2
    # 동의(시간당 2회) 뒤 설정 파일에서 상한을 50 으로 올렸다 → 시작할 때 관찰을 끄고 다시 동의를 받는다
    $wid = WaitLog "\[observe\] 동의 뒤 범위가 넓어짐 \(observe\.max_per_hour" 4
    Check "O6" "동의 뒤 설정 파일로 상한을 올리면 → 관찰 꺼짐 (다시 켤 때 동의)" ($wid -and (PetLines | Where-Object { $_ -match "\[observe\] state on=false" }).Count -ge 1) "$wid"
    AppCommand $ID_OBSERVE_TOGGLE
    $null = AnswerConsent $true
    $null = WaitLog "\[observe\] enabled consent_version=1" 3
    AppCommand 110
    for ($i = 0; $i -lt 30 -and (Chat) -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100 }
    Start-Sleep -Milliseconds 300
    $note = ChatText 212
    $null = [P4.N]::PostMessageW((Chat), $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
    Mark
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline -and (SinceMark "\[observe\] done").Count -lt 3) { Start-Sleep -Milliseconds 200 }
    $startPets = @(SinceMark "\[observe\] start pet=(\d+)" | ForEach-Object { if ($_ -match "start pet=(\d+)") { [int]$Matches[1] } })
    $alt = $startPets.Count -ge 3
    for ($k = 1; $k -lt [Math]::Min(3, $startPets.Count); $k++) { if ($startPets[$k] -eq $startPets[$k - 1]) { $alt = $false } }
    $spoke = @(SinceMark "\[observe\] done .*result=spoke").Count
    Check "M1" "펫 2마리: 회차마다 1마리 발화, 연달아 같은 펫 아님" ($alt -and $spoke -ge 3) ("starts=" + ($startPets -join ","))
    Check "U1" "대화창에 '화면 관찰 켜짐' 표시" ($note -match "화면 관찰 켜짐") "$note"
    Check "X2" "정상 종료" (StopPet $p) ""

    # ---- 일시정지 유지: "다시 켤 때까지 일시정지" 는 다시 시작해도 풀리지 않는다 ----
    $p = StartPet1 "--spawn=1500,150"
    AppCommand $ID_OBSERVE_PAUSE_ON
    $null = WaitLog "\[observe\] pause until_resume" 3
    $null = StopPet $p
    $p = StartPet1 "--spawn=1500,150"
    $st = WaitLog "\[observe\] state on=true consent_version=1 paused=true" 4
    Mark; Start-Sleep -Seconds 4
    Check "G3c" "'다시 켤 때까지 일시정지' 는 다시 시작해도 유지" ($st -and (SinceMark "\[observe\] start").Count -eq 0) "$st"
    AppCommand $ID_OBSERVE_RESUME
    $null = WaitLog "\[observe\] resume" 3
    $null = StopPet $p

    # ---- B14 대조 캡처: 제외 켬 2장 vs 제외 끔 1장, 펫 영역이 배경색과 다른 비율 ----
    Copy-Item "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" (Join-Path $tmp "dpbg.exe") -Force
    $tw = Join-Path $PSScriptRoot "target_window.ps1"
    $bgWin = Start-Process (Join-Path $tmp "dpbg.exe") -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $tw,
        "-EventLog", (Join-Path $tmp "bg.log"), "-X", "900", "-Y", "300", "-W", "900", "-H", "700", "-Title", "P4-bg", "-Argb", "-16744448") -PassThru
    Start-Sleep -Milliseconds 800
    $ledge = Start-Process (Join-Path $tmp "dpbg.exe") -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $tw,
        "-EventLog", (Join-Path $tmp "ledge.log"), "-X", "1250", "-Y", "800", "-W", "300", "-H", "40", "-Title", "P4-ledge", "-Argb", "-12566464") -PassThru
    Start-Sleep -Seconds 2
    function PetFrac($img, $scale, $r) {
        Add-Type -AssemblyName System.Drawing
        $b = [Drawing.Bitmap]::FromFile($img)
        try {
            $diff = 0; $n = 0
            for ($y = [int]($r.T * $scale) + 2; $y -lt [int]($r.B * $scale) - 2; $y++) {
                for ($x = [int]($r.L * $scale) + 2; $x -lt [int]($r.R * $scale) - 2; $x++) {
                    $c = $b.GetPixel($x, $y); $n++
                    if ([Math]::Abs($c.R - 0) + [Math]::Abs($c.G - 128) + [Math]::Abs($c.B - 0) -gt 90) { $diff++ }
                }
            }
            if ($n -eq 0) { return -1 } else { return $diff / $n }
        } finally { $b.Dispose() }
    }
    function PetRect { $h = [P4.N]::FindWindowW("DesktopPetDev.Pet", [IntPtr]::Zero); $r = New-Object P4.N+RECT; [void][P4.N]::GetWindowRect($h, [ref]$r); $r }
    function CaptureCopy($name) {
        Mark; $c = WaitLog "\[observe\] capture ok" 8; Start-Sleep -Milliseconds 300
        $dst = Join-Path $tmp $name; Copy-Item $dump $dst -Force
        $m = [regex]::Match("$c", "src=(\d+)x\d+ out=(\d+)x")
        return [pscustomobject]@{ Path = $dst; Scale = $(if ($m.Success) { [double]$m.Groups[2].Value / [double]$m.Groups[1].Value } else { 0 }) }
    }
    $p = StartPet1 "--spawn=1400,400"
    Start-Sleep -Seconds 2
    $r = PetRect
    $a = CaptureCopy "excl_a.jpg"; $b2 = CaptureCopy "excl_b.jpg"
    $null = StopPet $p
    $p = StartPet1 "--spawn=1400,400" "--observe-no-exclude"
    Start-Sleep -Seconds 2
    $r2 = PetRect
    $c3 = CaptureCopy "noexcl.jpg"
    $null = StopPet $p
    $fa = PetFrac $a.Path $a.Scale $r; $fb = PetFrac $b2.Path $b2.Scale $r; $fc = PetFrac $c3.Path $c3.Scale $r2
    Check "B14" "대조 캡처: 제외 켬이면 펫 영역이 배경색, 제외 끔이면 펫이 보임" ($r.T -gt 600 -and $r.L -eq $r2.L -and $fa -ge 0 -and $fa -lt 0.05 -and $fb -lt 0.05 -and $fc -gt 0.15) ("pet=({0},{1},{2},{3}) excl={4:N3}/{5:N3} noexcl={6:N3}" -f $r.L, $r.T, $r.R, $r.B, $fa, $fb, $fc)
    foreach ($w in @($bgWin, $ledge)) { if ($w) { Stop-Process -Id $w.Id -Force -ErrorAction SilentlyContinue } }

    # ---- 로컬 AI TTS (B11·B12): HTTP(OpenAI 호환)·명령 실행, 캐릭터 음성, 실패 시 읽지 않음 ----
    # TOML 리터럴 문자열('...')로 경로·JSON 을 넘긴다 (역슬래시 이스케이프 없음). 무음 WAV 라 소리는 나지 않는다
    WriteConfig 60 ("enabled = true`nengine = `"http`"`nhttp_url = '$base/v1/audio/speech'`nhttp_model = `"mock-tts`"`n" +
        "http_voice = `"global-voice`"`nhttp_extra_json = '{`"lang`":`"ko`"}'`nvolume = 0.0")
    $p = StartPet1 "--spawn=1500,150"
    # 상한 50 → 60: 범위가 넓어져 꺼졌으므로 다시 동의
    AppCommand $ID_OBSERVE_TOGGLE
    $null = AnswerConsent $true
    $null = WaitLog "\[observe\] enabled consent_version=1" 3
    Mark
    $sp = WaitLog "\[tts\] spoke engine=http" 15
    $lt = (Stats).last_tts
    Check "T2" "HTTP 로컬 TTS: 캐릭터 voice 우선·model·wav·speed·추가 JSON" ($sp -match "completed=true" -and $lt.voice -eq "blue-voice" -and $lt.model -eq "mock-tts" -and $lt.response_format -eq "wav" -and $lt.lang -eq "ko" -and [double]$lt.speed -eq 1.1) "$sp | voice=$($lt.voice) speed=$($lt.speed)"
    Mock "tts=fail"
    Mark
    $fl = WaitLog "\[tts\] synth failed engine=http code=http_500" 15
    Start-Sleep -Milliseconds 500
    Check "T4" "로컬 TTS 실패 → 그 발화는 읽지 않음 (다른 엔진으로 대체 안 함)" ($fl -and (SinceMark "\[tts\] spoke").Count -eq 0) "$fl"
    Mock "reset=1"
    $null = StopPet $p

    $rec = Join-Path $tmp "tts_record.txt"
    $jp = '{"should_speak":true,"text":"こんにちは、안녕! \"따옴표\" {text}","emotion":"happy","action":{"type":"none","name":""}}'
    Mock ("observe_text=" + [uri]::EscapeDataString($jp))
    WriteConfig 60 ("enabled = true`nengine = `"command`"`ncommand_path = '$mockExe'`n" +
        "command_args = '--tts-out={out} --tts-record=$rec --tts-text {text}'`nvolume = 0.0")
    $p = StartPet1 "--spawn=1500,150"
    Mark
    $sp = WaitLog "\[tts\] spoke engine=command" 15
    $got = if (Test-Path $rec) { [IO.File]::ReadAllText($rec, [Text.Encoding]::UTF8) } else { "" }
    Check "T3" "명령 실행 TTS: 일본어·한국어·따옴표·중괄호가 든 텍스트를 그대로 전달, WAV 재생" ($sp -match "completed=true" -and $got -eq 'こんにちは、안녕! "따옴표" {text}') "$sp | got=$got"
    Mock "reset=1"
    $null = StopPet $p
    Remove-Item $rec -ErrorAction SilentlyContinue

    # ---- 데이터 유출 없음 ----
    $leaks = @()
    foreach ($l in $script:allLogs) {
        $t = Get-Content $l -Raw -Encoding UTF8
        foreach ($pat in @("모의 관찰", "base64", "/9j/", "dpexcl", "should_speak", "mock_at_", "eyJ", "こんにちは", "blue-voice")) { if ($t.Contains($pat)) { $leaks += $pat } }
    }
    Check "P1" "로그에 이미지·응답 원문·실행 파일 이름·토큰 없음" ($leaks.Count -eq 0) ($leaks -join ",")
    $imgs = @(Get-ChildItem $cfgDir, $logDir -Recurse -Include *.jpg, *.jpeg, *.png, *.bmp -ErrorAction SilentlyContinue)
    Check "P2" "설정·로그 폴더에 이미지 파일 없음 (검증용 덤프는 임시 폴더만)" ($imgs.Count -eq 0) ($imgs.FullName -join ",")
    $dumpOk = (Test-Path $dump) -and ([IO.File]::ReadAllBytes($dump)[0..1] -join ",") -eq "255,216"
    Check "D1" "검증용 덤프(테스트 전용 인자) = JPEG" $dumpOk ""
    if ($KeepDump -and (Test-Path $dump)) { Copy-Item $dump (Join-Path $outDir "observe_dump.jpg") }
}
finally {
    if ($notepad) { Stop-Process -Id $notepad.Id -Force -ErrorAction SilentlyContinue }
    if ($p -and -not $p.HasExited) { $null = StopPet $p }
    if ($mock -and -not $mock.HasExited) { Stop-Process -Id $mock.Id -Force }
    $i = 0; foreach ($l in $script:allLogs) { if ($l) { Copy-Item $l (Join-Path $outDir ("desktop-pet_{0}.log" -f $i++)) } }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue  # 캡처 덤프(실제 화면)를 지운다
    Save-Results
}
