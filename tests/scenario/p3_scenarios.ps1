# P3-09 AI 대화 시나리오 (P3 계획 §4.2). 모의 SIWC 서버(127.0.0.1)만 사용하며 실제 OpenAI 로는 아무것도 보내지 않는다.
# 사용자 config.toml·자격 증명은 건드리지 않는다: 임시 폴더의 --config 로 실행 (자격 증명도 그 폴더의 auth\).
# 실행 중 대화창이 열리고 키 입력(단축키 1회)이 발생한다. PC를 만지지 않는다.
param([string]$Preset = "x64-release")
$ErrorActionPreference = "Stop"
$env:DP_TEST_LANGUAGE = "ko"; $env:DP_TEST_NO_WELCOME = "1"  # 시나리오: 화면 문구 한국어 고정, 환영 창 끔
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bin = Join-Path $root "build\$Preset"
$pet = Join-Path $bin "desktop-pet.exe"
$mockExe = Join-Path $bin "mock-siwc.exe"
$outDir = Join-Path $root ("logs\p3\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_scenarios")
$tmp = Join-Path $env:TEMP ("dp_p3_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force $outDir, $tmp | Out-Null
Add-Type -AssemblyName System.Security

Add-Type -Namespace P3 -Name N -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern System.IntPtr GetDlgItem(System.IntPtr h, int id);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr SendMessageW(System.IntPtr h, uint m, System.IntPtr w, string l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr SendMessageW(System.IntPtr h, uint m, System.IntPtr w, System.Text.StringBuilder l);
[DllImport("user32.dll")] public static extern System.IntPtr SendMessageTimeoutW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l, uint f, uint t, out System.IntPtr r);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr h);
[DllImport("user32.dll")] public static extern int GetWindowTextLengthW(System.IntPtr h);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern System.IntPtr WindowFromPoint(POINT p);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct POINT { public int X, Y; }
'@
$WM_COMMAND = 0x111; $WM_SETTEXT = 0x0C; $WM_GETTEXT = 0x0D; $WM_CLOSE = 0x10
function App { [P3.N]::FindWindowW("DesktopPet.App", [IntPtr]::Zero) }
function Chat { [P3.N]::FindWindowW("DesktopPet.Chat", [IntPtr]::Zero) }
function AppCommand($id) { $null = [P3.N]::PostMessageW((App), $WM_COMMAND, [IntPtr]$id, [IntPtr]::Zero) }
function ChatClick($id) { $c = Chat; $null = [P3.N]::PostMessageW($c, $WM_COMMAND, [IntPtr]$id, [P3.N]::GetDlgItem($c, $id)) }
function ChatText($id) {
    # 다른 프로세스 컨트롤의 텍스트는 GetWindowText 가 아니라 WM_GETTEXTLENGTH/WM_GETTEXT 로 읽는다
    $h = [P3.N]::GetDlgItem((Chat), $id); $n = [int][P3.N]::SendMessageW($h, 0x0E, [IntPtr]::Zero, [string]$null)
    $sb = New-Object System.Text.StringBuilder ($n + 2); $null = [P3.N]::SendMessageW($h, $WM_GETTEXT, [IntPtr]($n + 1), $sb); $sb.ToString()
}
function Say($text) {
    $null = [P3.N]::SendMessageW([P3.N]::GetDlgItem((Chat), 216), $WM_SETTEXT, [IntPtr]::Zero, $text)
    ChatClick 217
}

# ---- 모의 서버 ----
$portFile = Join-Path $tmp "port.txt"
$mock = Start-Process $mockExe -ArgumentList @("--port-file=$portFile") -PassThru -WindowStyle Hidden
for ($i = 0; $i -lt 100 -and -not (Test-Path $portFile); $i++) { Start-Sleep -Milliseconds 100 }
$base = "http://127.0.0.1:" + (Get-Content $portFile).Trim()
function Mock($q) { $null = Invoke-RestMethod -UseBasicParsing "$base/control?$q" }
function Stats { Invoke-RestMethod -UseBasicParsing "$base/stats" }
function StatN($k) { $s = Stats; if ($s.PSObject.Properties[$k]) { [int]$s.$k } else { 0 } }

# ---- 설정·자격 증명 (임시) ----
$cfgDir = Join-Path $tmp "cfg"; New-Item -ItemType Directory -Force $cfgDir | Out-Null
$cfg = Join-Path $cfgDir "config.toml"
[IO.File]::WriteAllText($cfg, "[display]`nproximity_reaction = false`n[ai]`nhotkey = `"Ctrl+Alt+F9`"`nbubble_seconds = 3`nfirst_token_timeout_sec = 3`nrequest_timeout_sec = 12`n")
$urlFile = Join-Path $tmp "authorize_url.txt"
function CredPlain {
    $f = Get-ChildItem (Join-Path $cfgDir "auth") -Filter "siwc-*.bin" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $f) { return $null }
    $ent = [Text.Encoding]::ASCII.GetBytes("DesktopPet.SIWC.v1")
    [Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($f.FullName), $ent, 'CurrentUser'))
}
function CredRaw { $f = Get-ChildItem (Join-Path $cfgDir "auth") -Filter "siwc-*.bin" | Select-Object -First 1; [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($f.FullName)) }

# ---- 로그 ----
$logDir = Join-Path $env:LOCALAPPDATA "DesktopPet\logs"
$script:petLog = $null; $script:mark = 0; $script:allLogs = @()
function PetLines { @(Get-Content $script:petLog -Encoding UTF8) }
function Mark { $script:mark = (PetLines).Count }
function SinceMark($pattern) { @(PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match $pattern }) }
function WaitLog($pattern, $sec = 6) {
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        $hit = PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match $pattern } | Select-Object -First 1
        if ($hit) { return $hit }
        Start-Sleep -Milliseconds 100
    }
    return $null
}
$results = New-Object System.Collections.ArrayList
function Check($id, $name, $ok, $detail) {
    [void]$results.Add([pscustomobject]@{ Id = $id; Scenario = $name; Pass = [bool]$ok; Detail = "$detail" })
    Write-Host ("[{0}] {1} {2} — {3}" -f $id, $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail)
}

function StartPet {
    $before = @(Get-ChildItem $logDir -Filter *.log | Select-Object -ExpandProperty Name)
    $script:petLog = $null
    $p = Start-Process $pet -ArgumentList @("--config=$cfg", "--ai", "--ai-endpoint=$base", "--ai-open-url-file=$urlFile",
        "--no-behavior", "--pets=1", "--character=test-blue", "--spawn=1500,150", "--duration=600") -PassThru
    for ($i = 0; $i -lt 50 -and -not $script:petLog; $i++) {
        Start-Sleep -Milliseconds 200
        $new = Get-ChildItem $logDir -Filter *.log | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($new) { $script:petLog = $new.FullName }
    }
    $script:allLogs += $script:petLog
    $script:mark = 0
    $null = WaitLog "\[ai\] enabled endpoint=loopback-test" 8
    $null = WaitLog "\[pet0\] event=Landed" 6
    AppCommand 110  # 대화창 열기 (마지막 펫)
    for ($i = 0; $i -lt 30 -and (Chat) -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100 }
    return $p
}
function StopPet($p) {
    AppCommand 106
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $exited = $p.WaitForExit(8000)
    if (-not $exited) { Stop-Process -Id $p.Id -Force }
    return [pscustomobject]@{ Exited = $exited; Ms = $sw.ElapsedMilliseconds }
}
# 로그인 1회: 버튼 → URL 파일 → 모의 서버 인가(리디렉션을 따라가면 앱 루프백 콜백) → 결과 로그
function PortFree($port) {
    try { $l = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $port); $l.Start(); $l.Stop(); $true } catch { $false }
}
function WaitPortFree($port, $sec = 3) {
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) { if (PortFree $port) { return $true }; Start-Sleep -Milliseconds 100 }
    return $false
}
function SignIn {
    Remove-Item $urlFile -ErrorAction SilentlyContinue
    Mark
    ChatClick 211
    for ($i = 0; $i -lt 50 -and -not (Test-Path $urlFile); $i++) { Start-Sleep -Milliseconds 100 }
    $u = (Get-Content $urlFile -Raw).Trim()
    try { $null = Invoke-WebRequest -UseBasicParsing $u -TimeoutSec 10 } catch { }
    WaitLog "\[ai\] ui signin ok=" 8
}
function SignOut { Mark; ChatClick 211; WaitLog "\[ai\] ui signout ok=" 8 }
function Turn($text, $sec = 10) { Mark; Say $text; WaitLog "\[ai\] stream done" $sec }

$secret = "비밀대화문장123"
[void][P3.N]::SetCursorPos(2400, 200)
try {
    $p = StartPet
    $s0 = Stats
    Check "U0" "AI 켬: 대화창 열림, 로그인 전 상태" ((Chat) -ne [IntPtr]::Zero -and (ChatText 211) -eq "Continue with ChatGPT") (ChatText 210)

    # ---- 승인 흐름 ----
    # ---- 반복 로그인·취소 (취소 경합·리스너 잔류 수정 검증) ----
    $okCancel = 0
    for ($k = 0; $k -lt 3; $k++) {
        Remove-Item $urlFile -ErrorAction SilentlyContinue
        Mark; ChatClick 211
        for ($i = 0; $i -lt 50 -and -not (Test-Path $urlFile); $i++) { Start-Sleep -Milliseconds 100 }
        $busy = -not (PortFree 1455)  # 대기 중에는 1455 를 앱이 잡고 있어야 한다
        ChatClick 211                  # 로그인 취소
        $r = WaitLog "\[ai\] ui signin ok=false code=cancelled" 4
        if ($busy -and $r -and (WaitPortFree 1455)) { $okCancel++ }
    }
    Check "L1" "로그인 대기 중 취소 ×3 → 매번 취소, 1455 포트 즉시 해제" ($okCancel -eq 3) "ok=$okCancel/3"
    $okRace = 0
    for ($k = 0; $k -lt 5; $k++) {
        Mark; ChatClick 211; ChatClick 211  # 시작 직후 곧바로 취소 (작업 시작 전 취소 경합)
        $r = WaitLog "\[ai\] ui signin ok=false code=cancelled" 4
        if ($r -and (WaitPortFree 1455)) { $okRace++ }
    }
    Check "L2" "시작 직후 취소 ×5 → 취소가 사라지지 않음, 포트 해제" ($okRace -eq 5) "ok=$okRace/5"
    Remove-Item $urlFile -ErrorAction SilentlyContinue
    Mark; ChatClick 211
    for ($i = 0; $i -lt 50 -and -not (Test-Path $urlFile); $i++) { Start-Sleep -Milliseconds 100 }
    AppCommand 111  # 로그인 대기 중 AI 끄기
    $off = WaitLog "\[ai\] disabled" 4
    $free = WaitPortFree 1455 3
    $detached = (PetLines | Where-Object { $_ -match "작업 스레드가 .* 안에 끝나지 않음" }).Count
    Check "L3" "로그인 대기 중 AI 끄기 → 스레드 정상 종료, 포트 해제" ($off -and $free -and $detached -eq 0) "free=$free detached=$detached"
    AppCommand 111  # 다시 켜기
    $null = WaitLog "\[ai\] enabled endpoint=loopback-test" 4
    AppCommand 110
    Start-Sleep -Milliseconds 300

    Mock "auth=deny"
    $r = SignIn
    $a = (Stats).last_authorize
    Check "A2" "거부(access_denied) → 코드 교환 없음, 로그인 안 됨" ($r -match "ok=false code=access_denied" -and (StatN "token_code") -eq 0) "$r"
    Check "A1a" "최초 등록 요청: dynamic_agent_client + agent_name_hint + host id" ($a.client_id -eq "dynamic_agent_client" -and $a.agent_name_hint -eq "Desktop Pet" -and $a.ext_agent_host_id -like "urn:uuid:*" -and $a.redirect_uri -like "http://127.0.0.1:*/auth/callback") "redirect=$($a.redirect_uri)"
    $hostId = $a.ext_agent_host_id

    Mock "auth=badstate"
    $r = SignIn
    $rej = @(SinceMark "\[ai\] callback rejected reason=state")
    ChatClick 211  # 로그인 대기 중 → 취소
    $c = WaitLog "\[ai\] ui signin ok=false code=cancelled" 5
    Check "A4" "state 불일치 콜백 → 400 으로 거부하고 계속 기다림(교환 없음), 취소하면 끝" (-not $r -and $rej.Count -ge 1 -and $c -and (StatN "token_code") -eq 0) "$c"

    Mock "auth=noplan"
    $r = SignIn
    Check "A3a" "plan usage 범위 없이 동의 → 로그인 유지, plan usage off" ($r -match "ok=true .*plan_usage=false") "$r"
    $resp0 = StatN "responses"
    Mark; Say "plan 없음 테스트"; Start-Sleep -Milliseconds 800
    Check "A3b" "plan usage off → 추론 요청 안 보냄, 안내" ((StatN "responses") -eq $resp0 -and (ChatText 218) -match "plan usage") (ChatText 218)
    $r = SignOut
    Check "S1a" "로그아웃 → revoke 1회, 토큰 삭제, 등록 유지" ($r -match "ok=true" -and (StatN "revoke") -eq 1 -and (CredPlain) -notmatch "access_token" -and (CredPlain) -match "oaiapp_mock") "$r"

    Mock "reset=1"
    $r = SignIn
    $a = (Stats).last_authorize
    Check "A1b" "로그아웃 뒤 다시 로그인: 발급 client_id·같은 host id·login_hint, 해제된 세션의 id_token_hint 없음, agent_name_hint 생략" ($r -match "ok=true .*plan_usage=true" -and $a.client_id -like "oaiapp_mock*" -and $a.agent_name_hint -eq "" -and $a.id_token_hint -eq $false -and $a.login_hint -eq $true -and $a.ext_agent_host_id -eq $hostId) "client=$($a.client_id)"
    Check "A3c" "plan usage 를 거부했던 등록의 다시 로그인 → prompt=consent" ($a.prompt -eq "consent") "prompt=$($a.prompt)"
    Check "A5" "OpenID discovery 로 엔드포인트 확인" ((StatN "discovery") -ge 1 -and (PetLines | Where-Object { $_ -match "\[ai\] discovery ok" }).Count -ge 1) "discovery=$(StatN 'discovery')"
    $null = WaitLog "\[ai\] ui models count=1 selected=0" 5
    Check "M1" "모델 목록: visibility=list 만" ((Stats).models -ge 1) "models=$((Stats).models)"
    $rfD = StatN "token_refresh"; $mD = StatN "models"
    Mark; AppCommand 112  # AI 연결 진단 (실연동 점검용)
    $dg = WaitLog "\[ai\] ui diag ok=" 5
    Check "D1" "연결 진단: 토큰 강제 갱신 + 모델 목록" ($dg -match "ok=true .*models=1" -and (StatN "token_refresh") -eq $rfD + 1 -and (StatN "models") -eq $mD + 1) "$dg"
    Check "U1" "plan usage 표시·첫 로그인 안내" ([P3.N]::IsWindowVisible([P3.N]::GetDlgItem((Chat), 212)) -and [P3.N]::IsWindowVisible([P3.N]::GetDlgItem((Chat), 219))) ""
    ChatClick 220  # Got it

    # ---- 대화·스트림 ----
    $r = Turn "안녕 $secret"
    $lr = (Stats).last_responses
    $hist = ChatText 215
    $bubble = [P3.N]::FindWindowW("DesktopPet.Bubble", [IntPtr]::Zero)
    $bubbleVisible = $bubble -ne [IntPtr]::Zero -and [P3.N]::IsWindowVisible($bubble)
    Check "C1a" "대화 스트림 완료 → 기록·말풍선" ($r -match "outcome=completed" -and $hist -match "모의 응답" -and $bubbleVisible) "bubble=$bubbleVisible hist=$($hist.Length) $r"
    # 말풍선은 항상 클릭 통과: 가운데 지점의 창이 말풍선이 아니어야 한다 (WS_EX_TRANSPARENT + LAYERED)
    $br = New-Object P3.N+RECT; $null = [P3.N]::GetWindowRect($bubble, [ref]$br)
    $pt = New-Object P3.N+POINT; $pt.X = [int](($br.L + $br.R) / 2); $pt.Y = [int](($br.T + $br.B) / 2)
    $under = [P3.N]::WindowFromPoint($pt)
    Check "C1e" "말풍선 클릭 통과 (WindowFromPoint 가 말풍선을 건너뜀)" ($bubbleVisible -and $under -ne $bubble) ("bubble=0x{0:X} under=0x{1:X}" -f [long]$bubble, [long]$under)
    Check "C1b" "요청 형식: store=false·stream=true·instructions·system role 없음·모델" ($lr.store -eq $false -and $lr.stream -eq $true -and $lr.has_instructions -and -not $lr.has_system_role -and $lr.model -eq "mock-chat" -and $lr.input_count -eq 1) ($lr | ConvertTo-Json -Compress)
    Start-Sleep -Seconds 4
    Check "C1c" "말풍선 자동 숨김 (bubble_seconds=3)" (-not [P3.N]::IsWindowVisible($bubble)) ""
    $r = Turn "두 번째"
    Check "C1d" "이력 포함 요청 (input 3개)" ($r -match "outcome=completed" -and (Stats).last_responses.input_count -eq 3) "input=$((Stats).last_responses.input_count)"

    Mock "responses=slow"
    Mark; Say "길게 말해줘"; $null = WaitLog "\[ai\] send" 3
    Start-Sleep -Milliseconds 1500
    # C3 스트리밍 중 UI 스레드 비차단: 앱 창 ping 응답 시간
    $worst = 0
    for ($k = 0; $k -lt 10; $k++) {
        $sw = [Diagnostics.Stopwatch]::StartNew(); $res = [IntPtr]::Zero
        $ok = [P3.N]::SendMessageTimeoutW((App), 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 1000, [ref]$res)
        $worst = [math]::Max($worst, $sw.ElapsedMilliseconds); Start-Sleep -Milliseconds 100
    }
    $talk = (PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match "stream done" }).Count -eq 0
    Check "C3" "긴 응답 중 UI 스레드 비차단 (ping 최악 < 200ms)" ($worst -lt 200 -and $talk) "worst=${worst}ms"
    # 스트리밍 실시간성: 끝나기 전에 응답 일부가 대화창에 이미 보여야 한다 (초당 10 delta)
    $h1 = (ChatText 215).Length; Start-Sleep -Milliseconds 600; $h2 = (ChatText 215).Length
    Check "C6" "스트리밍 중 응답이 조금씩 표시됨 (끝나기 전 증가)" ($talk -and $h2 -gt $h1) "len $h1 → $h2"
    ChatClick 217  # 중지
    $r = WaitLog "\[ai\] stream done" 5
    Check "C2" "중지 → cancelled" ($r -match "outcome=cancelled") "$r"
    $draws = if ($r -match "bubble_draws=(\d+)") { [int]$Matches[1] } else { 0 }
    Check "C7" "말풍선이 스트리밍 중 여러 번 갱신됨" ($draws -ge 10) "bubble_draws(누적)=$draws"

    Mock "responses=long"
    $r = Turn "아주 길게"
    Check "C4" "앱 응답 상한(1200자) → truncated" ($r -match "outcome=truncated chars=1200") "$r"
    Mock "responses=drop"
    $r = Turn "끊김 테스트"
    Check "C5a" "스트림 중 끊김 → interrupted" ($r -match "outcome=interrupted") "$r"
    Mock "responses=silent&silent_ms=6000"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $r = Turn "첫 응답 지연" 10
    Check "TO1" "첫 응답 대기 시간 초과(3초) → timed_out first_token_timeout" ($r -match "outcome=timed_out .*code=first_token_timeout" -and $sw.Elapsed.TotalSeconds -lt 6) ("{0:N1}s {1}" -f $sw.Elapsed.TotalSeconds, $r)
    Mock "responses=slow"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $r = Turn "전체 시간 초과" 20
    Check "TO2" "응답은 오지만 전체 시간 초과(12초) → timed_out request_timeout" ($r -match "outcome=timed_out .*code=request_timeout" -and $r -notmatch "chars=0 " -and $sw.Elapsed.TotalSeconds -lt 15) ("{0:N1}s {1}" -f $sw.Elapsed.TotalSeconds, $r)
    Mock "responses=ok"
    $r = Turn "다시"
    Check "C5b" "끊김 뒤 다음 요청 정상" ($r -match "outcome=completed") "$r"

    # ---- 오류 복구 ----
    $rf0 = StatN "token_refresh"
    Mock "responses=invalid_user"
    $r = Turn "e1"
    Check "E1" "401 invalid_user → 갱신 1회·재시도 후 재로그인 안내" ($r -match "outcome=failed .*code=subscription_sharing_invalid_user recovery=re_sign_in" -and (StatN "token_refresh") -eq $rf0 + 1 -and $r -match "attempts=2") "$r"
    Check "E1b" "오류에 x-request-id 기록 (진단용)" ($r -match "request_id=req_mock_\d+") ""
    Mock "responses=not_eligible"
    $r = Turn "e2"
    Check "E2" "403 not_eligible → 재시도 없음" ($r -match "attempts=1 .*recovery=not_eligible") "$r"
    Mock "responses=usage_limit"
    $r = Turn "e3"
    Check "E3" "response.failed 한도 초과 → Usage limit 안내, 재시도 없음" ($r -match "attempts=1 .*code=subscription_sharing_usage_limit_exceeded recovery=usage_limit" -and (ChatText 218) -match "Usage limit reached") (ChatText 218)
    $n0 = StatN "responses"
    Mock "responses=unavailable"
    $r = Turn "e4" 15
    Check "E4" "503 → 1·2·4초 백오프 3회 후 안내 (요청 4회)" ($r -match "attempts=4 .*recovery=retry_backoff" -and (StatN "responses") -eq $n0 + 4) "$r"
    Mock "responses=ok"

    # ---- 토큰 갱신 ----
    $r = SignOut
    Mock "access_ttl=60"  # 만료 120초 이내 → 요청 전에 갱신
    $null = SignIn
    $rf0 = StatN "token_refresh"
    $r = Turn "t1"
    $cred = CredPlain
    Check "T1" "만료 임박 → 요청 전 갱신 1회, 완료" ($r -match "outcome=completed" -and (StatN "token_refresh") -eq $rf0 + 1 -and (PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match "token refresh ok" }).Count -ge 1) "$r"
    Mock "refresh_id=ok"
    $r = Turn "t3"
    Check "T3a" "갱신 응답의 새 ID 토큰 검증 통과 → 완료" ($r -match "outcome=completed") "$r"
    Mock "refresh_id=bad_sub"
    $r = Turn "t3b"
    Check "T3b" "갱신 응답의 ID 토큰이 다른 계정 → 거부·재로그인 안내" ($r -match "code=refresh_id_token_invalid recovery=re_sign_in") "$r"
    Mock "refresh_id=none"
    $null = SignIn
    Mock "refresh=invalid_grant"
    $r = Turn "t2"
    $cred = CredPlain
    Check "T2" "갱신 영구 오류 → 토큰 삭제·재로그인 안내, client_id 유지" ($r -match "recovery=re_sign_in" -and $cred -notmatch "refresh_token" -and $cred -match "oaiapp_mock" -and (ChatText 211) -eq "Continue with ChatGPT") "$r"
    Mock "reset=1"

    # ---- 로그아웃 실패 ----
    $null = SignIn
    Mock "revoke=fail"
    $r = SignOut
    Check "S3a" "revoke 실패 → 토큰 유지, 로컬 로그아웃 버튼" ($r -match "ok=false code=revoke_failed" -and (CredPlain) -match "refresh_token" -and [P3.N]::IsWindowVisible([P3.N]::GetDlgItem((Chat), 221))) "$r"
    Mark; ChatClick 221
    $r = WaitLog "\[ai\] ui signout ok=" 8
    Check "S3b" "로컬에서만 로그아웃 → 원격 미확인 안내" ($r -match "ok=true code=revoke_unconfirmed" -and (ChatText 218) -match "원격 해제" -and (CredPlain) -notmatch "refresh_token") "$r"
    Mock "reset=1"

    # ---- 단축키 ----
    $null = [P3.N]::PostMessageW((Chat), $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    Mark
    [P3.N]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero); [P3.N]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [P3.N]::keybd_event(0x78, 0, 0, [UIntPtr]::Zero); [P3.N]::keybd_event(0x78, 0, 2, [UIntPtr]::Zero)
    [P3.N]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero); [P3.N]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    $hk = WaitLog "\[ai\] hotkey → chat pet=0" 3
    Start-Sleep -Milliseconds 300
    Check "K1" "전역 단축키(Ctrl+Alt+F9) → 대화창" ($hk -and [P3.N]::IsWindowVisible((Chat))) "$hk"

    $x = StopPet $p
    Check "X0" "정상 종료" ($x.Exited) "$($x.Ms)ms"

    # ---- 재실행 후 다시 로그인, 스트림 중 종료 ----
    $p = StartPet
    $r = SignIn
    $a = (Stats).last_authorize
    Check "S2" "재실행 후 다시 로그인(로그아웃 상태): 저장된 client_id·같은 host id·login_hint, id_token_hint 없음, prompt 없음" ($r -match "ok=true" -and $a.client_id -like "oaiapp_mock*" -and $a.ext_agent_host_id -eq $hostId -and $a.id_token_hint -eq $false -and $a.login_hint -eq $true -and $a.agent_name_hint -eq "" -and $a.prompt -eq "") "client=$($a.client_id)"
    $null = WaitLog "\[ai\] ui models" 5
    Mock "responses=slow"
    Mark; Say "종료 테스트"; $null = WaitLog "\[ai\] send" 3; Start-Sleep -Milliseconds 800
    $x = StopPet $p
    Check "X1" "스트림 중 종료 → 5초 안에 종료" ($x.Exited -and $x.Ms -lt 5000) "$($x.Ms)ms"
    Mock "reset=1"

    # ---- 비밀 유출 없음 ----
    $leaks = @()
    foreach ($l in $script:allLogs) {
        $t = Get-Content $l -Raw -Encoding UTF8
        foreach ($pat in @("mock_at_", "mock_rt_", "mock_code_", "eyJ", $secret, "모의 응답", "code_challenge=", "id_token_hint=", "pet-tester@example.test")) { if ($t.Contains($pat)) { $leaks += $pat } }
    }
    Check "P1a" "로그에 토큰·인가 URL·대화 원문·이메일 없음" ($leaks.Count -eq 0) ($leaks -join ",")
    $raw = CredRaw
    Check "P1b" "자격 증명 파일에 평문 토큰 없음 (DPAPI)" ($raw -notmatch "mock_rt_|mock_at_|oaiapp_mock") ""
}
finally {
    if ($p -and -not $p.HasExited) { $null = StopPet $p }
    if ($mock -and -not $mock.HasExited) { Stop-Process -Id $mock.Id -Force }
    $i = 0; foreach ($l in $script:allLogs) { if ($l) { Copy-Item $l (Join-Path $outDir ("desktop-pet_{0}.log" -f $i++)) } }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    $results | Export-Csv (Join-Path $outDir "results.csv") -NoTypeInformation -Encoding UTF8
    $pass = @($results | Where-Object Pass).Count
    Write-Host ("결과: {0}/{1} 통과 → {2}" -f $pass, $results.Count, $outDir)
}
