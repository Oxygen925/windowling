# P2-09 다중 펫 성능 (P2 계획 §4.3). 60초 × 3회 단축 방식 (P1-D4 와 같은 방식)
#   M1/M3/M5 = 1·3·5마리 정지, W1/W3/W5 = 1·3·5마리 계속 걷기. 1마리 값은 P1 R1/R2 와 같은 조건 (리팩터 비회귀 확인)
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
$outDir = Join-Path $root ("logs\p2\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_pets")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace PP -Name N -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr after, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
'@
[void][PP.N]::SetThreadExecutionState([uint32]"0x80000003")


$scenarios = @(
    @{ Id = "M1"; Name = "1마리 정지"; Args = @("--no-behavior", "--pets=1", "--spawn=2300,300") },
    @{ Id = "M3"; Name = "3마리 정지"; Args = @("--no-behavior", "--pets=3", "--spawn=2300,300") },
    @{ Id = "M5"; Name = "5마리 정지"; Args = @("--no-behavior", "--pets=5", "--spawn=2300,300") },
    @{ Id = "W1"; Name = "1마리 계속 걷기"; Args = @("--force-walk", "--pets=1", "--spawn=1800,300") },
    @{ Id = "W3"; Name = "3마리 계속 걷기"; Args = @("--force-walk", "--pets=3", "--spawn=1800,300") },
    @{ Id = "W5"; Name = "5마리 계속 걷기"; Args = @("--force-walk", "--pets=5", "--spawn=1800,300") }
)
if ($Only.Count) { $scenarios = $scenarios | Where-Object { $Only -contains $_.Id } }
$common = @("--character=test-orange", "--spawn-step=-150")

foreach ($run in 1..$Runs) {
    foreach ($s in $scenarios) {
        $name = "{0}_run{1}" -f $s.Id, $run
        Write-Host ("[{0}] {1} {2}" -f (Get-Date -Format "HH:mm:ss"), $name, $s.Name)
        [void][PP.N]::SetCursorPos(80, 80)  # 커서를 펫에서 멀리
        $csv = Join-Path $outDir "$name.csv"
        $pp = Start-Process $pet -ArgumentList ($s.Args + $common + @("--metrics=$csv", "--duration=$($Seconds + $WarmupSec)")) -PassThru
        $cp = Start-Process $counters -ArgumentList @("--pid=$($pp.Id)", "--out=$(Join-Path $outDir "$name`_counters.csv")") -PassThru -WindowStyle Hidden
        $pp.WaitForExit()
        if (-not $cp.WaitForExit(5000)) { Stop-Process -Id $cp.Id -Force }
        Start-Sleep -Seconds 3
    }
}
[void][PP.N]::SetThreadExecutionState([uint32]"0x80000000")

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
        Priv = ($r | ForEach-Object { [double]$_.private_mb } | Measure-Object -Average).Average
        Vram = ($r | ForEach-Object { [double]$_.vram_local_mb } | Measure-Object -Average).Average
            Wake = ($r | ForEach-Object { [double]$_.wakeups_delta } | Measure-Object -Average).Average
            Events = ($raw1 - $raw0) / $span
        }
    }
    [pscustomobject]@{
        Id = $s.Id; Scenario = $s.Name; Runs = @($per).Count
        CpuPct = "{0:N3}" -f (Med ($per.Cpu)); PdhPct = "{0:N3}" -f (Med ($per.Pdh)); CyclePct = "{0:N3}" -f (Med ($per.Cyc))
        WsMb = "{0:N1}" -f (Med ($per.Ws)); PrivMb = "{0:N1}" -f (Med ($per.Priv)); VramMb = "{0:N1}" -f (Med ($per.Vram)); PresentPerSec = "{0:N2}" -f (Med ($per.Present)); WakePerSec = "{0:N1}" -f (Med ($per.Wake))
        EventsPerSec = "{0:N0}" -f (Med ($per.Events))
    }
}
$summary | Format-Table -AutoSize | Out-String -Width 250 | Tee-Object -FilePath (Join-Path $outDir "summary.txt")
Write-Host "→ $outDir"
