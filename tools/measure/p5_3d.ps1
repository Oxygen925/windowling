# P5-09: 3D 펫(test-fox) 과 스프라이트 펫(test-blue) 비용 비교. 같은 조건(정지 1마리 / 걷기 3마리)에서 --metrics CSV 를 평균낸다.
# 화면 캡처 없음. 실행 중 펫이 화면 아래쪽을 걷는다. Release 빌드 기준, 디버그 레이어 측정은 공식 판정에서 제외(사용자 결정).
param([string]$Preset = "x64-release", [int]$Seconds = 30)
$ErrorActionPreference = "Stop"
$env:DP_TEST_LANGUAGE = "ko"; $env:DP_TEST_NO_WELCOME = "1"  # 시나리오: 화면 문구 한국어 고정, 환영 창 끔
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pet = Join-Path $root "build\$Preset\desktop-pet.exe"
$outDir = Join-Path $root ("logs\p5\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_measure")
New-Item -ItemType Directory -Force $outDir | Out-Null
$cfg = Join-Path $outDir "config.toml"
[IO.File]::WriteAllText($cfg, "[display]`nproximity_reaction = false`n")

$cases = @(
    @{ Name = "sprite_idle_1"; Args = @("--character=test-blue", "--pets=1", "--no-behavior") },
    @{ Name = "fox_idle_1"; Args = @("--character=test-fox", "--pets=1", "--no-behavior") },
    @{ Name = "sprite_walk_3"; Args = @("--character=test-blue", "--pets=3", "--force-walk") },
    @{ Name = "fox_walk_3"; Args = @("--character=test-fox", "--pets=3", "--force-walk") }
)
$rows = foreach ($c in $cases) {
    $csv = Join-Path $outDir "$($c.Name).csv"
    $a = @("--config=$cfg", "--metrics=$csv", "--duration=$($Seconds + 5)", "--spawn=900,150", "--spawn-step=300") + $c.Args
    $p = Start-Process $pet -ArgumentList $a -PassThru
    $null = $p.WaitForExit(($Seconds + 30) * 1000)
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    # 처음 5초(착지·준비)는 버린다
    $d = Get-Content $csv | Where-Object { $_ -notmatch "^#" } | ConvertFrom-Csv | Where-Object { [double]$_.t -ge 5 }
    $avg = { param($k) [math]::Round(($d | Measure-Object -Property $k -Average).Average, 3) }
    [pscustomobject]@{
        Case = $c.Name; Samples = @($d).Count
        CpuPct = & $avg "proc_cpu_pct"; WorkingSetMb = & $avg "working_set_mb"; PrivateMb = & $avg "private_mb"
        VramMb = & $avg "vram_local_mb"; PresentPerSec = & $avg "present_count_delta"; WakeupsPerSec = & $avg "wakeups_delta"
        RegionBuildsPerSec = & $avg "n_region_build"; RenderKcyc = & $avg "kc_render"; RegionKcyc = & $avg "kc_region"
    }
}
$rows | Format-Table -AutoSize | Out-String -Width 220
$rows | Export-Csv (Join-Path $outDir "summary.csv") -NoTypeInformation -Encoding UTF8
Write-Host "결과: $outDir"
