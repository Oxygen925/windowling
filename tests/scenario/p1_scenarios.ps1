# P1-12 자동 창 시나리오 테스트 (P1 계획 §4.2 W1~W7, W12 + 드래그·클릭)
# 별도 프로세스 대상 창을 움직이고, desktop-pet 로그로 펫 상태를 판정한다.
# 실행 중 마우스가 자동으로 움직인다 (드래그·클릭). PC를 만지지 않는다.
param([string]$Preset = "x64-release")
$ErrorActionPreference = "Stop"
$env:DP_TEST_LANGUAGE = "ko"; $env:DP_TEST_NO_WELCOME = "1"  # 시나리오: 화면 문구 한국어 고정, 환영 창 끔
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pet = Join-Path $root "build\$Preset\desktop-pet.exe"
$outDir = Join-Path $root ("logs\p1\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_scenarios")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace P1 -Name N -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr after, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int cmd);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int data, System.UIntPtr extra);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
'@
$SWP_NOACTIVATE = 0x10; $SWP_NOZORDER = 0x4; $HWND_TOP = [IntPtr]::Zero
function Rect($h) { $r = New-Object P1.N+RECT; $null = [P1.N]::GetWindowRect($h, [ref]$r); [pscustomobject]@{ L = $r.L; T = $r.T; R = $r.R; B = $r.B } }
function MoveWin($h, $x, $y, $w, $hh, [switch]$Top, [switch]$Topmost, [switch]$NoTopmost) {
    $flags = $SWP_NOACTIVATE; $after = $HWND_TOP
    if ($Topmost) { $after = [IntPtr](-1) } elseif ($NoTopmost) { $after = [IntPtr](-2) } elseif (-not $Top) { $flags = $flags -bor $SWP_NOZORDER }
    $null = [P1.N]::SetWindowPos($h, $after, $x, $y, $w, $hh, $flags)
}
function CurMove($x, $y) { $null = [P1.N]::SetCursorPos($x, $y) }
function CurClick($x, $y) { CurMove $x $y; Start-Sleep -Milliseconds 30; [P1.N]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [P1.N]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero) }
function CurDrag($x0, $y0, $x1, $y1) {
    CurMove $x0 $y0; Start-Sleep -Milliseconds 200
    [P1.N]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    for ($k = 1; $k -le 20; $k++) { CurMove ([int]($x0 + ($x1 - $x0) * $k / 20)) ([int]($y0 + ($y1 - $y0) * $k / 20)); Start-Sleep -Milliseconds 15 }
    [P1.N]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}

function Start-Target($name, $x, $y, $w, $h, $argb) {
    $log = Join-Path $outDir "$name.log"
    $p = Start-Process powershell -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "target_window.ps1"),
        "-EventLog", $log, "-X", $x, "-Y", $y, "-W", $w, "-H", $h, "-Title", $name, "-Argb", $argb) -PassThru
    for ($i = 0; $i -lt 60 -and -not ((Test-Path $log) -and (Select-String -Path $log -Pattern " hwnd " -Quiet)); $i++) { Start-Sleep -Milliseconds 200 }
    $hwnd = [IntPtr][long]((Select-String -Path $log -Pattern "hwnd (\d+)").Matches[0].Groups[1].Value)
    [pscustomobject]@{ Proc = $p; Hwnd = $hwnd; Hex = ("0x{0:X}" -f [long]$hwnd) }
}

# ---- 펫 로그 ----
$logDir = Join-Path $env:LOCALAPPDATA "DesktopPet\logs"
$script:petLog = $null
$script:mark = 0
function PetLines { @(Get-Content $script:petLog -Encoding UTF8) }
function Mark { $script:mark = (PetLines).Count }
function WaitLog($pattern, $sec = 5) {
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        $hit = PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match $pattern } | Select-Object -First 1
        if ($hit) { return $hit }
        Start-Sleep -Milliseconds 100
    }
    return $null
}
function SinceMark($pattern) { @(PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match $pattern }) }
function PetRect { Rect ([P1.N]::FindWindowW("DesktopPet.Pet", [IntPtr]::Zero)) }
function PetFoot { $r = PetRect; [pscustomobject]@{ X = ($r.L + $r.R) / 2.0; Y = $r.B } }
function PetBelly { $r = PetRect; @([int](($r.L + $r.R) / 2), [int]($r.T + ($r.B - $r.T) * 0.75)) }

$results = New-Object System.Collections.ArrayList
function Check($id, $name, $ok, $detail) {
    [void]$results.Add([pscustomobject]@{ Id = $id; Scenario = $name; Pass = [bool]$ok; Detail = $detail })
    Write-Host ("[{0}] {1} {2} — {3}" -f $id, $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail)
}

# ---- 준비 ----
$T = Start-Target "target" 600 500 900 400 -13421705
$C = Start-Target "cover" -3000 0 300 300 -6710887      # 가림 실험용 (화면 밖 대기)
CurClick 1400 860; Start-Sleep -Milliseconds 300        # 대상 창 활성화 (펫과 겹치지 않는 지점)

$before = @(Get-ChildItem $logDir -Filter *.log | Select-Object -ExpandProperty Name)
$pp = Start-Process $pet -ArgumentList @("--no-behavior", "--spawn=1000,150", "--duration=300") -PassThru
for ($i = 0; $i -lt 50 -and -not $script:petLog; $i++) {
    Start-Sleep -Milliseconds 200
    $new = Get-ChildItem $logDir -Filter *.log | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($new) { $script:petLog = $new.FullName }
}

try {
    # W1 착지
    $hit = WaitLog ("mode=Attached host=" + $T.Hex) 6
    Check "W1" "대상 창 위 착지" $hit "$hit"
    $localX = if ($hit -match "localX=(-?\d+)") { [int]$Matches[1] } else { 0 }

    # W2 창 이동 (느리게·빠르게) — 상대 좌표 유지
    Mark
    for ($k = 1; $k -le 30; $k++) { MoveWin $T.Hwnd (600 + 10 * $k) (500 + 3 * $k) 900 400; Start-Sleep -Milliseconds 30 }
    for ($k = 1; $k -le 30; $k++) { MoveWin $T.Hwnd (900 - 20 * $k) (590 - 3 * $k) 900 400; Start-Sleep -Milliseconds 5 }
    Start-Sleep -Milliseconds 400
    $tr = Rect $T.Hwnd; $f = PetFoot
    $dx = [math]::Abs($f.X - ($tr.L + $localX)); $dy = [math]::Abs($f.Y - $tr.T)
    Check "W2" "창 이동 중 부착 유지" ((SinceMark "Detached").Count -eq 0 -and $dx -le 1.5 -and $dy -le 1) ("오차 dx={0:N1} dy={1:N1}" -f $dx, $dy)

    # W5 크기 변경 (좁히기) — 발 위치 클램프
    Mark
    MoveWin $T.Hwnd $tr.L $tr.T 300 400; Start-Sleep -Milliseconds 500
    $f = PetFoot; $tr2 = Rect $T.Hwnd
    Check "W5" "창 좁히기 → 가장자리 클램프" ((SinceMark "Detached").Count -eq 0 -and $f.X -ge $tr2.L -and $f.X -le $tr2.R) ("foot.x={0:N0} 창=[{1},{2})" -f $f.X, $tr2.L, $tr2.R)
    MoveWin $T.Hwnd 600 500 900 400; Start-Sleep -Milliseconds 400

    # W6a 순간 가림 (유예 시간 이내) — 낙하하지 않음
    $f = PetFoot
    Mark
    # 다른 프로세스의 SetWindowPos(HWND_TOP)만으로는 전경 창 위로 올라가지 않으므로(확인됨) 가림 창을 topmost 로 올린다
    MoveWin $C.Hwnd ([int]$f.X - 150) ([int]$f.Y - 150) 300 300 -Topmost; Start-Sleep -Milliseconds 100
    MoveWin $C.Hwnd -3000 0 300 300 -NoTopmost; Start-Sleep -Milliseconds 600
    Check "W6a" "순간 가림(100ms)은 낙하하지 않음" ((SinceMark "Detached").Count -eq 0) ("Detached {0}건" -f (SinceMark "Detached").Count)

    # W6b 지속 가림 — 유예 후 낙하
    Mark
    MoveWin $C.Hwnd ([int]$f.X - 150) ([int]$f.Y - 150) 300 300 -Topmost
    $det = WaitLog "event=Detached.*reason=occluded" 3
    Check "W6b" "지속 가림 → 유예 후 낙하" $det "$det"
    Start-Sleep -Milliseconds 1500
    MoveWin $C.Hwnd -3000 0 300 300 -NoTopmost; Start-Sleep -Milliseconds 300

    # 드래그로 대상 창 위에 다시 올리기 (드래그 → 놓기 → 낙하 → 착지)
    CurClick 1400 860; Start-Sleep -Milliseconds 300
    Mark
    $b = PetBelly; CurDrag $b[0] $b[1] 1050 ([int]((Rect $T.Hwnd).T - 150))
    $hit = WaitLog ("mode=Attached host=" + $T.Hex) 5
    Check "D1" "드래그 후 놓으면 낙하해 재착지" ($hit -and (SinceMark "mode=Dragged").Count -ge 1) "$hit"

    # 클릭 반응 (불투명 영역)
    Mark
    $b = PetBelly; CurClick $b[0] $b[1]
    Check "D2" "펫 클릭 → 반응" (WaitLog "\] click" 2) ""
    CurMove 2400 1300

    # W3 최소화 → 낙하, 복원 후 재착지 가능
    Mark
    $null = [P1.N]::ShowWindow($T.Hwnd, 6)  # SW_MINIMIZE
    $det = WaitLog "event=Detached.*reason=host gone" 3
    $gr = WaitLog "mode=Grounded" 5
    Check "W3a" "최소화 → 부착 해제·바닥 착지" ($det -and $gr) "$det"
    $null = [P1.N]::ShowWindow($T.Hwnd, 9)  # SW_RESTORE
    Start-Sleep -Milliseconds 600
    CurClick 1400 860; Start-Sleep -Milliseconds 300
    Mark
    $b = PetBelly; CurDrag $b[0] $b[1] 1050 ([int]((Rect $T.Hwnd).T - 150))
    Check "W3b" "복원 후 재착지" (WaitLog ("mode=Attached host=" + $T.Hex) 5) ""
    CurMove 2400 1300

    # W7 최대화 — 위 공간 없음 → 낙하, 화면 밖 이탈 없음
    # 테두리 없는 창의 SW_MAXIMIZE 는 작업 표시줄까지 덮어 전체화면(W12)으로 판정되므로, 일반 최대화 창의 배치(작업 영역 전체)로 재현한다
    Mark
    MoveWin $T.Hwnd 0 0 2560 1392
    $det = WaitLog "event=Detached" 3
    Start-Sleep -Milliseconds 1500
    $pr = PetRect
    Check "W7" "최대화 → 착지 불가로 낙하, 화면 안" ($det -and $pr.T -ge 0) ("pet top={0} {1}" -f $pr.T, $det)
    MoveWin $T.Hwnd 600 500 900 400; Start-Sleep -Milliseconds 500

    # W12 전체화면 앱 → 숨김, 해제 → 표시
    Mark
    MoveWin $T.Hwnd 0 0 2560 1440 -Top
    CurClick 1280 720; Start-Sleep -Milliseconds 300
    $hide = WaitLog "fullscreen hide" 3
    MoveWin $T.Hwnd 600 500 900 400
    $show = WaitLog "fullscreen show" 3
    Check "W12" "전체화면 → 숨김 / 해제 → 표시" ($hide -and $show) "$hide"

    # W4 닫기 — 재부착 후 대상 창 종료
    CurClick 1400 860; Start-Sleep -Milliseconds 300
    Mark
    $b = PetBelly; CurDrag $b[0] $b[1] 1050 ([int]((Rect $T.Hwnd).T - 150))
    $att = WaitLog ("mode=Attached host=" + $T.Hex) 5
    Mark
    Stop-Process -Id $T.Proc.Id -Force
    $det = WaitLog "event=Detached.*reason=host gone" 3
    $gr = WaitLog "mode=Grounded" 5
    Check "W4" "창 닫기 → 낙하, 유령 표면 없음" ($att -and $det -and $gr) "$det"
}
finally {
    CurMove 2400 1300
    $app = [P1.N]::FindWindowW("DesktopPet.App", [IntPtr]::Zero)
    if ($app -ne [IntPtr]::Zero) { $null = [P1.N]::PostMessageW($app, 0x111, [IntPtr]106, [IntPtr]::Zero) }
    if (-not $pp.WaitForExit(5000)) { Stop-Process -Id $pp.Id -Force }
    foreach ($w in $T, $C) { if ($w -and -not $w.Proc.HasExited) { Stop-Process -Id $w.Proc.Id -Force } }
    if ($script:petLog) { Copy-Item $script:petLog (Join-Path $outDir "desktop-pet.log") }
    $results | Export-Csv (Join-Path $outDir "results.csv") -NoTypeInformation -Encoding UTF8
    $pass = @($results | Where-Object Pass).Count
    Write-Host ("결과: {0}/{1} 통과 → {2}" -f $pass, $results.Count, $outDir)
}
