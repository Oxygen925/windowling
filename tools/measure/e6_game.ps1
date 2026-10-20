# E6 Mindustry 병행 비교 (단축 검증, 조건별 1회). PresentMon 때문에 관리자 권한으로 실행한다.
#   G0 게임 단독 / G1 소형 창 5마리 / G2 단일 창 5마리 / G3 펫 실행·오버레이 숨김
#   (G4 다른 모니터: 측정 PC 모니터 1대라 생략)
# 펫은 hit-test B, 30fps 애니메이션, 좌상단 배치. 게임은 미리 전체화면으로 띄워 두고 같은 장면을 유지한다.
param(
    [string]$Preset = "x64-release",
    [int]$Seconds = 180,
    [string]$GameProcess = "Mindustry",
    [string[]]$Conditions = @("G0", "G1", "G2", "G3")
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bin = Join-Path $root "build\$Preset"
$spike = Join-Path $bin "p0-spike.exe"
$counters = Join-Path $bin "p0-counters.exe"
$pm = Join-Path $root "tools\external\presentmon\PresentMon-2.6.0-x64.exe"
$Conditions = @($Conditions | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$outDir = Join-Path $root ("logs\p0\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_e6")
New-Item -ItemType Directory -Force $outDir | Out-Null
$transcript = Join-Path $outDir "e6_run.log"
Start-Transcript -Path $transcript | Out-Null

Add-Type -Namespace E6 -Name N -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
'@
[void][E6.N]::SetThreadExecutionState([uint32]"0x80000003")

$game = Get-Process $GameProcess -ErrorAction Stop | Select-Object -First 1
"game pid=$($game.Id) hwnd=$($game.MainWindowHandle)"
function Ensure-GameForeground {
    if ([E6.N]::GetForegroundWindow() -ne $game.MainWindowHandle) { [void][E6.N]::SetForegroundWindow($game.MainWindowHandle); Start-Sleep -Milliseconds 500 }
    "foreground=game:" + ([E6.N]::GetForegroundWindow() -eq $game.MainWindowHandle)
}

$petArgs = @{
    "G1" = @("--pets=5", "--anim=30", "--hittest=B", "--overlay=small");
    "G2" = @("--pets=5", "--anim=30", "--hittest=B", "--overlay=single");
    "G3" = @("--pets=5", "--anim=30", "--hittest=B", "--overlay=small")
}
foreach ($c in $Conditions) {
    "=== $c $(Get-Date -Format HH:mm:ss)"
    $sp = $null
    if ($petArgs.ContainsKey($c)) {
        $a = $petArgs[$c] + @("--anchor=top-left", "--debug-layer=off", "--warmup=5", "--duration=$($Seconds + 10)",
                              "--scenario=E6-$c", "--variant=$c", "--log=$(Join-Path $outDir "$c`_pet.csv")")
        $sp = Start-Process $spike -ArgumentList $a -PassThru
        Start-Sleep -Seconds 2
        if ($c -eq "G3") { [void][E6.N]::PostMessageW([E6.N]::FindWindowW("DesktopPet.P0.App", [IntPtr]::Zero), 0x111, [IntPtr]104, [IntPtr]::Zero); "overlay hidden" }
    }
    Ensure-GameForeground
    Start-Sleep -Seconds 5
    $cp = Start-Process $counters -ArgumentList @("--pid=$($game.Id)", "--out=$(Join-Path $outDir "$c`_counters.csv")", "--duration=$Seconds") -PassThru -WindowStyle Hidden
    $pp = Start-Process $pm -ArgumentList @("--process_name", "$GameProcess.exe", "--output_file", (Join-Path $outDir "$c`_presentmon.csv"),
                                            "--timed", "$Seconds", "--terminate_after_timed", "--no_console_stats", "--stop_existing_session") -PassThru -WindowStyle Hidden
    $t0 = Get-Date
    $pp.WaitForExit(); "presentmon exit=$($pp.ExitCode)"
    # 관리자 권한이 없으면 PresentMon이 즉시 종료(exit 6) → 측정 시간은 그대로 채운다
    $left = $Seconds - ((Get-Date) - $t0).TotalSeconds
    if ($left -gt 1) { "presentmon 사용 불가 — 카운터만 기록"; Start-Sleep -Seconds ([int]$left) }
    if (-not $cp.WaitForExit(5000)) { Stop-Process -Id $cp.Id -Force }
    if ($sp) {
        [void][E6.N]::PostMessageW([E6.N]::FindWindowW("DesktopPet.P0.App", [IntPtr]::Zero), 0x111, [IntPtr]105, [IntPtr]::Zero)
        if (-not $sp.WaitForExit(5000)) { Stop-Process -Id $sp.Id -Force }
    }
    Start-Sleep -Seconds 3
}
[void][E6.N]::SetThreadExecutionState([uint32]"0x80000000")
"done → $outDir"
Stop-Transcript | Out-Null
