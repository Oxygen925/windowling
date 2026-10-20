# E6 결과 요약: 조건별 게임 GPU/CPU, DWM CPU/GPU, 펫 CPU·메모리, (있으면) PresentMon frame time
param([Parameter(Mandatory)][string]$Dir)
$ErrorActionPreference = "Stop"
function Rows($p) { if (-not (Test-Path $p)) { return @() }; @(Get-Content $p -Encoding UTF8 | Where-Object { $_ -notmatch '^#' } | ConvertFrom-Csv) }
function Avg($rows, $col) { $v = @($rows | Where-Object { $_.$col -ne "" -and $null -ne $_.$col } | ForEach-Object { [double]$_.$col }); if ($v.Count) { ($v | Measure-Object -Average).Average } else { $null } }
function Pct($v, $p) { $s = @($v | Sort-Object); if (-not $s.Count) { return $null }; $s[[int][math]::Min($s.Count - 1, [math]::Floor($p * ($s.Count - 1)))] }

$(foreach ($c in "G0", "G1", "G2", "G3") {
    $cnt = Rows (Join-Path $Dir "$c`_counters.csv")
    if (-not $cnt.Count) { continue }
    $pet = Rows (Join-Path $Dir "$c`_pet.csv")
    $pm = Rows (Join-Path $Dir "$c`_presentmon.csv")
    $o = [ordered]@{
        Condition    = $c
        GameGpuPct   = Avg $cnt "target_gpu_util_pct"
        GameCpuPct   = Avg $cnt "target_cpu_pct_total"
        DwmCpuPct    = Avg $cnt "dwm_cpu_pct_total"
        DwmGpuPct    = Avg $cnt "dwm_gpu_util_pct"
        PetCpuPct    = if ($pet.Count) { 100.0 * (($pet | ForEach-Object { [double]$_.proc_cpu_ms_delta } | Measure-Object -Sum).Sum) / ((($pet | ForEach-Object { [double]$_.interval_sec } | Measure-Object -Sum).Sum) * 1000 * 16) } else { $null }
        PetCpuCycle  = Avg $pet "proc_cpu_pct_cycles"
        PetWsMb      = Avg $pet "working_set_mb"
        PetVramMb    = Avg $pet "vram_local_mb"
        PetPresentSec = Avg $pet "present_count_delta"
    }
    if ($pm.Count) {
        $ftCol = @("FrameTime", "MsBetweenPresents", "msBetweenPresents") | Where-Object { $pm[0].PSObject.Properties.Name -contains $_ } | Select-Object -First 1
        $ft = @($pm | ForEach-Object { [double]$_.$ftCol } | Where-Object { $_ -gt 0 })
        $o["Frames"] = $ft.Count
        $o["FrameTimeAvg"] = ($ft | Measure-Object -Average).Average
        $o["FrameTimeP95"] = Pct $ft 0.95
        $o["FrameTimeP99"] = Pct $ft 0.99
        # 화면 반영 간격(실제 표시 기준)과 GPU 시간
        $dc = @($pm | Where-Object { $_.MsBetweenDisplayChange -and $_.MsBetweenDisplayChange -ne "NA" } | ForEach-Object { [double]$_.MsBetweenDisplayChange } | Where-Object { $_ -gt 0 })
        if ($dc.Count) { $o["DisplayChangeAvg"] = ($dc | Measure-Object -Average).Average; $o["DisplayChangeP95"] = Pct $dc 0.95; $o["DisplayChangeP99"] = Pct $dc 0.99 }
        $ud = @($pm | Where-Object { $_.MsUntilDisplayed -and $_.MsUntilDisplayed -ne "NA" } | ForEach-Object { [double]$_.MsUntilDisplayed })
        if ($ud.Count) { $o["UntilDisplayedAvg"] = ($ud | Measure-Object -Average).Average; $o["UntilDisplayedP95"] = Pct $ud 0.95 }
        $gb = @($pm | Where-Object { $_.MsGPUBusy -and $_.MsGPUBusy -ne "NA" } | ForEach-Object { [double]$_.MsGPUBusy })
        if ($gb.Count) { $o["GpuBusyMsAvg"] = ($gb | Measure-Object -Average).Average }
        $o["DroppedOrNotDisplayed"] = @($pm | Where-Object { $_.MsUntilDisplayed -eq "NA" }).Count
        $o["PresentModes"] = (($pm | Group-Object PresentMode | Sort-Object Count -Descending | ForEach-Object { "{0}={1}" -f $_.Name, $_.Count }) -join "; ")
    } else { $o["PresentMon"] = "없음 (관리자 권한 필요)" }
    [pscustomobject]$o
}) | Format-List
