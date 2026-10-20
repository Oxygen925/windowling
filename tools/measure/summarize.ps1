# run_scenarios.ps1 결과 요약 (계획서 §4.2 보고 규칙, §10 C2 판정 규칙)
#
# CPU 판정 지표 (C2 조건부 승인):
#   CpuPct      = GetProcessTimes 를 60초 창으로 집계한 CPU% 의 평균 (판정 기준)
#   CpuPctPdh   = Windows 성능 카운터(\Process(...)\% Processor Time ÷ 논리 프로세서) 평균 → 교차 검증
#   CpuPctCycle = QueryProcessCycleTime 기반 (보조 지표, 단독 판정 금지)
# D3D11 debug layer 가 켜진 실행은 정식 판정에서 제외한다.
param(
    [Parameter(Mandatory)][string]$Dir,
    [int]$WindowSec = 60
)
$ErrorActionPreference = "Stop"

function Read-P0Csv($path) {
    $all = Get-Content $path -Encoding UTF8
    $meta = @{}
    foreach ($l in $all | Where-Object { $_ -match '^# ' }) {
        foreach ($kv in ($l.Substring(2) -split ' ')) { if ($kv -match '^([\w\(\)]+)=(.*)$') { $meta[$Matches[1]] = $Matches[2] } }
    }
    $lines = @($all | Where-Object { $_ -notmatch '^#' })
    $rows = if ($lines.Count -ge 2) { $lines | ConvertFrom-Csv } else { @() }
    [pscustomobject]@{ Meta = $meta; Rows = @($rows) }
}
function Median($v) { $s = @($v | Sort-Object); if ($s.Count -eq 0) { return $null }; $n = $s.Count
    if ($n % 2) { $s[[int][math]::Floor($n / 2)] } else { ($s[$n / 2 - 1] + $s[$n / 2]) / 2 } }
function Avg($rows, $col) { $v = @($rows | Where-Object { $_.$col -ne "" -and $null -ne $_.$col } | ForEach-Object { [double]$_.$col })
    if ($v.Count -eq 0) { $null } else { ($v | Measure-Object -Average).Average } }

# GetProcessTimes 창 집계: 창마다 Σcpu_ms / (Σinterval × 1000 × 논리 프로세서) × 100
function WindowedCpu($rows, $procs, $win) {
    # 60초 측정은 샘플 경계 때문에 59행일 수 있어, 창 길이의 90% 이상이면 한 창으로 본다
    if ($rows.Count -lt $win -and $rows.Count -ge [math]::Ceiling($win * 0.9)) { $win = $rows.Count }
    $vals = @()
    for ($i = 0; $i + $win -le $rows.Count; $i += $win) {
        $chunk = $rows[$i..($i + $win - 1)]
        $ms = ($chunk | ForEach-Object { [double]$_.proc_cpu_ms_delta } | Measure-Object -Sum).Sum
        $sec = ($chunk | ForEach-Object { [double]$_.interval_sec } | Measure-Object -Sum).Sum
        if ($sec -gt 0) { $vals += 100.0 * $ms / ($sec * 1000.0 * $procs) }
    }
    if ($vals.Count -eq 0) {
        # 창 길이보다 짧은 실행(-Quick): 전체 구간 집계로 대체 (Windows=0 으로 표시)
        $ms = ($rows | ForEach-Object { [double]$_.proc_cpu_ms_delta } | Measure-Object -Sum).Sum
        $sec = ($rows | ForEach-Object { [double]$_.interval_sec } | Measure-Object -Sum).Sum
        if ($sec -gt 0) { return [pscustomobject]@{ Value = 100.0 * $ms / ($sec * 1000.0 * $procs); Windows = 0 } }
        return $null
    }
    [pscustomobject]@{ Value = ($vals | Measure-Object -Average).Average; Windows = $vals.Count }
}

$excluded = @()
$runs = foreach ($f in Get-ChildItem $Dir -Filter "*_run*.csv" | Where-Object { $_.Name -notmatch '_counters|_presentmon' }) {
    $d = Read-P0Csv $f.FullName
    $rows = $d.Rows
    if (-not $rows) { continue }
    if ($d.Meta["d3d_debug_layer"] -eq "true") { $excluded += $f.Name; continue }
    $procs = 0.0
    if ($d.Meta.ContainsKey("logical_processors")) { $procs = [double]$d.Meta["logical_processors"] }
    if ($procs -le 0) { $procs = [double](Get-CimInstance Win32_Processor | Measure-Object NumberOfLogicalProcessors -Sum).Sum }
    $cnt = Join-Path $Dir ($f.BaseName + "_counters.csv")
    $crow = if (Test-Path $cnt) { (Read-P0Csv $cnt).Rows } else { @() }
    $w = WindowedCpu $rows $procs $WindowSec
    $active = @($rows | Where-Object { $_.suspended -ne "1" })
    [pscustomobject]@{
        Key         = "{0}/{1}" -f $rows[0].scenario, $rows[0].variant
        CpuPct      = if ($w) { $w.Value } else { $null }
        CpuWindows  = if ($w) { $w.Windows } else { 0 }
        CpuPctPdh   = Avg $crow "target_cpu_pct_total"
        CpuPctCycle = Avg $rows "proc_cpu_pct_cycles"
        CpuMsPerMin = (Avg $rows "proc_cpu_ms_delta") * 60
        WakeupsSec  = Avg $active "wakeups_delta"
        WsMb        = Avg $rows "working_set_mb"
        PrivMb      = Avg $rows "private_mb"
        VramMb      = Avg $rows "vram_local_mb"
        PresentSec  = Avg $rows "present_count_delta"
        IdlePresentViolations = @($rows | Where-Object { $_.state_changed -eq "0" -and [int]$_.present_count_delta -gt 0 }).Count
        PresentIntervalMs = Avg $rows "present_interval_ms_mean"
        PresentJitterP95  = Avg $rows "present_jitter_ms_p95"
        TickJitterP95     = Avg $rows "tick_jitter_ms_p95"
        DwmCpuPct   = Avg $crow "dwm_cpu_pct_total"
        DwmGpuPct   = Avg $crow "dwm_gpu_util_pct"
        GpuPct      = Avg $crow "target_gpu_util_pct"
    }
}
$cols = "CpuPct", "CpuPctPdh", "CpuPctCycle", "CpuMsPerMin", "WakeupsSec", "WsMb", "PrivMb", "VramMb", "PresentSec",
        "IdlePresentViolations", "PresentIntervalMs", "PresentJitterP95", "TickJitterP95", "DwmCpuPct", "DwmGpuPct", "GpuPct"
$runs | Group-Object Key | Sort-Object Name | ForEach-Object {
    $g = $_.Group
    $o = [ordered]@{ Scenario = $_.Name; Runs = $g.Count; CpuWindowsPerRun = ($g | Measure-Object CpuWindows -Minimum).Minimum }
    foreach ($c in $cols) {
        $v = @($g | Where-Object { $null -ne $_.$c } | ForEach-Object { [double]$_.$c })
        if ($v.Count -eq 0) { $o[$c] = ""; continue }
        $o[$c] = "{0:N3} [{1:N3}-{2:N3}]" -f (Median $v), ($v | Measure-Object -Minimum).Minimum, ($v | Measure-Object -Maximum).Maximum
    }
    # 교차 검증: GetProcessTimes 창 집계 − 성능 카운터 (중앙값 기준)
    $a = Median @($g | Where-Object { $null -ne $_.CpuPct } | ForEach-Object { [double]$_.CpuPct })
    $b = Median @($g | Where-Object { $null -ne $_.CpuPctPdh } | ForEach-Object { [double]$_.CpuPctPdh })
    $o["CpuCrossCheckDiff"] = if ($null -ne $a -and $null -ne $b) { "{0:N4}" -f ($a - $b) } else { "" }
    if ($g.Count -lt 3) { $o["Note"] = "반복 3회 미만 — 판정 보류" }
    [pscustomobject]$o
} | Format-List
if ($excluded) { Write-Warning ("debug layer 활성 실행 제외: " + ($excluded -join ", ")) }
