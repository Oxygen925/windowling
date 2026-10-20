# P1 성능 비회귀 (P1 계획 §4.4, P1-D4: 정식 10분 측정이 아닌 60초 × 3회 단축 방식)
#   R1 1마리 정지 / R2 1마리 계속 걷기 / R3 창 이벤트 폭주(부착된 창을 100Hz로 이동) / R4 창 50개 열린 상태 정지
# CPU 판정은 GetProcessTimes 60초 집계(D15), PDH 교차 검증, 사이클은 보조.
param(
    [string]$Preset = "x64-release",
    [int]$Runs = 3,
    [int]$Seconds = 60,
    [int]$WarmupSec = 10,
    [string[]]$Only = @()
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bin = Join-Path $root "build\$Preset"
$pet = Join-Path $bin "desktop-pet.exe"
$counters = Join-Path $bin "p0-counters.exe"
$scen = Join-Path $root "tests\scenario"
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$outDir = Join-Path $root ("logs\p1\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_regression")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace PR -Name N -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr after, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
'@
[void][PR.N]::SetThreadExecutionState([uint32]"0x80000003")

function Start-Target($x, $y, $w, $h) {
    $log = Join-Path $outDir ("target_" + (Get-Date -Format "HHmmssfff") + ".log")
    $p = Start-Process powershell -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $scen "target_window.ps1"),
        "-EventLog", $log, "-X", $x, "-Y", $y, "-W", $w, "-H", $h) -PassThru
    for ($i = 0; $i -lt 60 -and -not ((Test-Path $log) -and (Select-String -Path $log -Pattern " hwnd " -Quiet)); $i++) { Start-Sleep -Milliseconds 200 }
    $m = Select-String -Path $log -Pattern "hwnd ([0-9]+)"
    [pscustomobject]@{ Proc = $p; Hwnd = [IntPtr][long]$m.Matches[0].Groups[1].Value }
}

$scenarios = @(
    @{ Id = "R1"; Name = "1마리 정지"; Args = @("--no-behavior", "--spawn=2300,300") },
    @{ Id = "R2"; Name = "1마리 계속 걷기"; Args = @("--force-walk", "--spawn=1800,300") },
    @{ Id = "R3"; Name = "창 이벤트 폭주(부착 창 100Hz 이동)"; Args = @("--no-behavior", "--spawn=1000,150"); Storm = $true },
    @{ Id = "R4"; Name = "창 50개 열린 상태 정지"; Args = @("--no-behavior", "--spawn=2400,300"); Many = $true },
    @{ Id = "R5"; Name = "커서 계속 이동 중 정지(펫에서 멀리)"; Args = @("--no-behavior", "--spawn=2400,300"); Cursor = $true }
)
if ($Only.Count) { $scenarios = $scenarios | Where-Object { $Only -contains $_.Id } }

foreach ($run in 1..$Runs) {
    foreach ($s in $scenarios) {
        $name = "{0}_run{1}" -f $s.Id, $run
        Write-Host ("[{0}] {1} {2}" -f (Get-Date -Format "HH:mm:ss"), $name, $s.Name)
        [void][PR.N]::SetCursorPos(80, 80)  # 커서를 펫에서 멀리 (근접 반응 영향 제거)
        $helpers = @()
        $T = $null
        if ($s.Storm) { $T = Start-Target 600 500 900 400; $helpers += $T.Proc }
        if ($s.Many) {
            $helpers += Start-Process powershell -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $scen "many_windows.ps1"),
                "-Count", "50", "-Seconds", ($Seconds + $WarmupSec + 20)) -PassThru
            Start-Sleep -Seconds 4
        }
        $csv = Join-Path $outDir "$name.csv"
        $pp = Start-Process $pet -ArgumentList ($s.Args + @("--metrics=$csv", "--duration=$($Seconds + $WarmupSec)")) -PassThru
        $cp = Start-Process $counters -ArgumentList @("--pid=$($pp.Id)", "--out=$(Join-Path $outDir "$name`_counters.csv")") -PassThru -WindowStyle Hidden
        if ($s.Storm) {
            Start-Sleep -Seconds 2  # 착지 대기
            $sw = [Diagnostics.Stopwatch]::StartNew(); $k = 0
            while (-not $pp.HasExited) {
                $a = $k * 0.02
                [void][PR.N]::SetWindowPos($T.Hwnd, [IntPtr]::Zero, [int](600 + 200 * [math]::Cos($a)), [int](500 + 100 * [math]::Sin($a)), 900, 400, 0x14)
                $k++
                Start-Sleep -Milliseconds 10
            }
            "storm moves=$k ({0:N0}/s)" -f ($k / $sw.Elapsed.TotalSeconds) | Out-File (Join-Path $outDir "$name`_storm.txt")
        }
        if ($s.Cursor) {
            # 시스템 전체 LOCATIONCHANGE 훅이 커서 이동(OBJID_CURSOR)마다 앱을 깨우는 비용 측정
            $k = 0
            while (-not $pp.HasExited) {
                $a = $k * 0.05
                [void][PR.N]::SetCursorPos([int](400 + 150 * [math]::Cos($a)), [int](400 + 150 * [math]::Sin($a)))
                $k++
                Start-Sleep -Milliseconds 8
            }
        }
        $pp.WaitForExit()
        if (-not $cp.WaitForExit(5000)) { Stop-Process -Id $cp.Id -Force }
        foreach ($h in $helpers) { if (-not $h.HasExited) { Stop-Process -Id $h.Id -Force } }
        Start-Sleep -Seconds 3
    }
}
[void][PR.N]::SetThreadExecutionState([uint32]"0x80000000")

# ---- 요약 ----
function Rows($p) { @(Get-Content $p | Where-Object { $_ -notmatch '^#' } | ConvertFrom-Csv) }
function Med($v) { $s = @($v | Sort-Object); if (-not $s.Count) { return $null }; $n = $s.Count; if ($n % 2) { $s[[int][math]::Floor($n / 2)] } else { ($s[$n/2-1] + $s[$n/2]) / 2 } }
$procs = [double](Get-CimInstance Win32_Processor | Measure-Object NumberOfLogicalProcessors -Sum).Sum
$summary = foreach ($s in $scenarios) {
    $per = foreach ($run in 1..$Runs) {
        $name = "{0}_run{1}" -f $s.Id, $run
        $r = Rows (Join-Path $outDir "$name.csv") | Where-Object { [double]$_.t -gt $WarmupSec }
        if (-not $r) { continue }
        $c = Rows (Join-Path $outDir "$name`_counters.csv")
        $span = [double]$r[-1].t - [double]$r[0].t + 1.0
        $cpuMs = ($r | ForEach-Object { [double]$_.proc_cpu_ms_delta } | Measure-Object -Sum).Sum
        $raw0 = [double]$r[0].we_raw; $raw1 = [double]$r[-1].we_raw
        [pscustomobject]@{
            Cpu = 100.0 * $cpuMs / ($span * 1000.0 * $procs)
            Pdh = ($c | Where-Object { $_.target_cpu_pct_total -ne "" } | ForEach-Object { [double]$_.target_cpu_pct_total } | Measure-Object -Average).Average
            Cyc = ($r | ForEach-Object { [double]$_.proc_cpu_pct_cycles } | Measure-Object -Average).Average
            Ws = ($r | ForEach-Object { [double]$_.working_set_mb } | Measure-Object -Average).Average
            Present = ($r | ForEach-Object { [double]$_.present_count_delta } | Measure-Object -Average).Average
            Wake = ($r | ForEach-Object { [double]$_.wakeups_delta } | Measure-Object -Average).Average
            Events = ($raw1 - $raw0) / $span
        }
    }
    [pscustomobject]@{
        Id = $s.Id; Scenario = $s.Name; Runs = @($per).Count
        CpuPct = "{0:N3}" -f (Med ($per.Cpu)); PdhPct = "{0:N3}" -f (Med ($per.Pdh)); CyclePct = "{0:N3}" -f (Med ($per.Cyc))
        WsMb = "{0:N1}" -f (Med ($per.Ws)); PresentPerSec = "{0:N2}" -f (Med ($per.Present)); WakePerSec = "{0:N1}" -f (Med ($per.Wake))
        EventsPerSec = "{0:N0}" -f (Med ($per.Events))
    }
}
$summary | Format-Table -AutoSize | Out-String -Width 250 | Tee-Object -FilePath (Join-Path $outDir "summary.txt")
Write-Host "→ $outDir"
