# P1-07 / P1-D5: 전체화면 게임으로 돌아왔을 때 직접 출력(Hardware Flip) 복귀 시간 측정
#
# 실제 상황 재현: 게임을 하다 다른 창으로 전환(펫이 보임) → 게임으로 복귀(펫 숨김 SW_HIDE) → 게임이 언제 직접 출력으로 돌아오는가.
# 원인 분리(P1-D5): 펫 없이 같은 전환을 반복한 기준선(nopet)과 펫 실행(pet)을 비교한다.
# 고정 임계값 없음 — 기록만 한다.
#
# 준비: 게임을 전체화면으로 실행해 둔 뒤, 관리자 PowerShell에서 실행 (PresentMon ETW 권한)
#   powershell -ExecutionPolicy Bypass -File tools\measure\p1_fullscreen_recovery.ps1
param(
    [string]$Preset = "x64-release",
    [string]$GameProcess = "Mindustry",
    [int]$Cycles = 3,
    [int]$AwaySec = 3,
    [int]$BackSec = 8
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pet = Join-Path $root "build\$Preset\desktop-pet.exe"
$pm = Join-Path $root "tools\external\presentmon\PresentMon-2.6.0-x64.exe"
$outDir = Join-Path $root ("logs\p1\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_fsrecovery")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace FR -Name N -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
'@
$game = Get-Process $GameProcess -ErrorAction Stop | Select-Object -First 1
$gameHwnd = $game.MainWindowHandle

# 전환 대상(펫이 보일 일반 창)
$awayLog = Join-Path $outDir "away.log"
$away = Start-Process powershell -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "tests\scenario\target_window.ps1"),
    "-EventLog", $awayLog, "-X", "700", "-Y", "500", "-W", "1100", "-H", "500", "-Title", "away") -PassThru
for ($i = 0; $i -lt 60 -and -not ((Test-Path $awayLog) -and (Select-String -Path $awayLog -Pattern " hwnd " -Quiet)); $i++) { Start-Sleep -Milliseconds 200 }
$awayHwnd = [IntPtr][long](Select-String -Path $awayLog -Pattern "hwnd ([0-9]+)").Matches[0].Groups[1].Value

# 전경이 아닌 프로세스는 SetForegroundWindow 가 막힌다(1차 측정에서 전환 6회 모두 실패).
# Alt 키 입력을 먼저 보내면 전경 전환 제한이 풀리는 동작을 이용한다 [검증: 이 PC에서 아래 AwayOk 로 확인]
function Activate($h) {
    for ($try = 0; $try -lt 3; $try++) {
        [FR.N]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [FR.N]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)  # VK_MENU down/up
        [void][FR.N]::ShowWindow($h, 9)
        [void][FR.N]::SetForegroundWindow($h)
        Start-Sleep -Milliseconds 300
        if ([FR.N]::GetForegroundWindow() -eq $h) { return $true }
    }
    return $false
}

$events = New-Object System.Collections.ArrayList
foreach ($cond in "nopet", "pet") {
    $pp = $null
    if ($cond -eq "pet") {
        $pp = Start-Process $pet -ArgumentList @("--no-behavior", "--spawn=1250,200") -PassThru
        Start-Sleep -Seconds 2
    }
    $csv = Join-Path $outDir "$cond`_presentmon.csv"
    $total = $Cycles * ($AwaySec + $BackSec) + 4
    $pmStart = Get-Date
    $pmp = Start-Process $pm -ArgumentList @("--process_name", "$GameProcess.exe", "--output_file", $csv, "--timed", "$total",
        "--terminate_after_timed", "--no_console_stats", "--stop_existing_session") -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 2
    for ($c = 1; $c -le $Cycles; $c++) {
        $okAway = Activate $awayHwnd
        if (-not $okAway) {
            Write-Warning "다른 창으로 전환 실패 — 전환이 일어나지 않으면 복귀 시간을 잴 수 없어 중단한다"
            $pmp.Kill()
            if ($pp) { Stop-Process -Id $pp.Id -Force }
            Stop-Process -Id $away.Id -Force -ErrorAction SilentlyContinue
            exit 2
        }
        Start-Sleep -Seconds $AwaySec
        $back = Get-Date
        $okBack = Activate $gameHwnd
        [void]$events.Add([pscustomobject]@{ Cond = $cond; Cycle = $c; BackAtMs = ($back - $pmStart).TotalMilliseconds; AwayOk = $okAway; BackOk = $okBack })
        Start-Sleep -Seconds $BackSec
    }
    $pmp.WaitForExit()
    if ($pp) {
        [void][FR.N]::PostMessageW([FR.N]::FindWindowW("DesktopPet.App", [IntPtr]::Zero), 0x111, [IntPtr]106, [IntPtr]::Zero)
        if (-not $pp.WaitForExit(5000)) { Stop-Process -Id $pp.Id -Force }
    }
}
Stop-Process -Id $away.Id -Force -ErrorAction SilentlyContinue
$events | Export-Csv (Join-Path $outDir "cycles.csv") -NoTypeInformation -Encoding UTF8

# ---- 분석: 복귀 시각 이후 Hardware Flip 이 30프레임 연속 이어지기 시작한 시점 ----
function Pct($v, $p) { $s = @($v | Sort-Object); if (-not $s.Count) { return $null }; $s[[int][math]::Min($s.Count - 1, [math]::Floor($p * ($s.Count - 1)))] }
$rows = foreach ($e in $events) {
    $pmRows = @(Import-Csv (Join-Path $outDir "$($e.Cond)_presentmon.csv"))
    $after = @($pmRows | Where-Object { [double]$_.TimeInMs -ge $e.BackAtMs -and [double]$_.TimeInMs -lt $e.BackAtMs + $BackSec * 1000 })
    $recover = $null; $run = 0
    for ($i = 0; $i -lt $after.Count; $i++) {
        if ($after[$i].PresentMode -like "Hardware*Flip*") { $run++; if ($run -eq 30) { $recover = [double]$after[$i - 29].TimeInMs - $e.BackAtMs; break } } else { $run = 0 }
    }
    $ft = @($after | ForEach-Object { [double]$_.MsBetweenPresents } | Where-Object { $_ -gt 0 })
    $dc = @($after | Where-Object { $_.MsBetweenDisplayChange -ne "NA" } | ForEach-Object { [double]$_.MsBetweenDisplayChange } | Where-Object { $_ -gt 0 })
    $avgFt = ($ft | Measure-Object -Average).Average
    [pscustomobject]@{
        Cond = $e.Cond; Cycle = $e.Cycle; AwayOk = $e.AwayOk; BackOk = $e.BackOk
        Fps = if ($avgFt) { "{0:N0}" -f (1000.0 / $avgFt) } else { "" }
        RecoverMs = if ($null -ne $recover) { "{0:N0}" -f $recover } else { "미복귀" }
        ComposedPct = if ($after.Count) { "{0:N0}" -f (100.0 * @($after | Where-Object { $_.PresentMode -like "Composed*" }).Count / $after.Count) } else { "" }
        FrameP95 = "{0:N2}" -f (Pct $ft 0.95); FrameP99 = "{0:N2}" -f (Pct $ft 0.99)
        DisplayP95 = "{0:N2}" -f (Pct $dc 0.95); DisplayP99 = "{0:N2}" -f (Pct $dc 0.99)
    }
}
$rows | Format-Table -AutoSize | Out-String -Width 250 | Tee-Object -FilePath (Join-Path $outDir "summary.txt")
Write-Host "→ $outDir"
