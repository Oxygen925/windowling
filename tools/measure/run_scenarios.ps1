# P0 반복 측정 스크립트 (계획서 §4.2, D8: 시나리오별 3회 이상)
#
# 예)
#   전체 자동 시나리오 3회:   powershell -ExecutionPolicy Bypass -File tools\measure\run_scenarios.ps1
#   핵심 시나리오 단축(예비 판정용, D20): ... -Core   (10개 × 3회 × 60초, 약 38분)
#   빠른 점검(20초·1회):      ... -Quick
#   일부만:                   ... -Only S1,S2
#   S5(Mindustry 병행):       게임을 먼저 실행한 뒤 ... -Only S5 -GameMode borderless [-PresentMonPath C:\tools\PresentMon.exe]
#
# 출력: logs\p0\<시각>\<scenario>_<variant>_run<N>.csv (+ _counters.csv, _presentmon.csv)
# S4(창 드래그)는 사람 조작이 필요해 자동 실행하지 않는다 → docs\p0_manual_checklist.md
param(
    [string]$Preset = "x64-release",
    [int]$Runs = 3,
    [int]$DurationSec = 600,
    [int]$WarmupSec = 120,
    [string[]]$Only = @(),
    [switch]$Quick,
    [switch]$Core,                             # 예비 판정용 핵심 시나리오 (D20)
    [string]$GameMode = "fullscreen",          # S5 variant 이름에 기록 (게임 내 설정은 사용자가 직접 변경)
    [string]$GameProcess = "Mindustry.exe",    # [검증] 실제 렌더링 프로세스 이름 확인 필요
    [string]$PresentMonPath = "",
    [int]$CooldownSec = 10,
    [ValidateSet("top-left", "bottom")][string]$Anchor = "top-left"   # 측정 중 커서 간섭 방지: 좌상단 배치
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bin = Join-Path $root "build\$Preset"
$spike = Join-Path $bin "p0-spike.exe"
$counters = Join-Path $bin "p0-counters.exe"
if (-not (Test-Path $spike)) { throw "빌드 산출물 없음: $spike (tools\build.ps1 -Preset $Preset 먼저 실행)" }
if ($Core) { $Runs = 3; $DurationSec = 60; $WarmupSec = 10; $CooldownSec = 5 }
if ($Quick) { $Runs = 1; $DurationSec = 20; $WarmupSec = 3; $CooldownSec = 2 }

# ---- 시나리오 정의 (scenario, variant, 인자) ----
$matrix = @(
    @{ S = "S0-A"; V = "tray-only";      A = @("--pets=0", "--gpu=off") },   # 트레이만, GPU 장치 미생성
    @{ S = "S0-B"; V = "gpu-no-window";  A = @("--pets=0") },                # GPU 장치 생성, 펫 창 없음
    @{ S = "S1";  V = "A";               A = @("--pets=1", "--anim=0", "--proximity=on", "--hittest=A") },
    @{ S = "S1";  V = "B";               A = @("--pets=1", "--anim=0", "--proximity=on", "--hittest=B") },
    @{ S = "S1";  V = "C";               A = @("--pets=1", "--anim=0", "--proximity=on", "--hittest=C") },
    @{ S = "S1p"; V = "A";               A = @("--pets=1", "--anim=0", "--proximity=off", "--hittest=A") },
    @{ S = "S1p"; V = "B";               A = @("--pets=1", "--anim=0", "--proximity=off", "--hittest=B") },
    @{ S = "S2";  V = "A-hires";         A = @("--pets=1", "--anim=30", "--hittest=A", "--timer=hires") },
    @{ S = "S2";  V = "A-msgwait";       A = @("--pets=1", "--anim=30", "--hittest=A", "--timer=msgwait") },
    @{ S = "S2";  V = "A-settimer";      A = @("--pets=1", "--anim=30", "--hittest=A", "--timer=settimer") },
    @{ S = "S2";  V = "A-surface";       A = @("--pets=1", "--anim=30", "--hittest=A", "--content=surface") },
    @{ S = "S2";  V = "B-hires";         A = @("--pets=1", "--anim=30", "--hittest=B") },
    @{ S = "S2";  V = "C-hires";         A = @("--pets=1", "--anim=30", "--hittest=C") },
    @{ S = "S2";  V = "A-ecoqos";        A = @("--pets=1", "--anim=30", "--hittest=A", "--ecoqos") },
    @{ S = "S3";  V = "small-3";         A = @("--pets=3", "--anim=30", "--hittest=A", "--overlay=small") },
    @{ S = "S3";  V = "small-5";         A = @("--pets=5", "--anim=30", "--hittest=A", "--overlay=small") },
    @{ S = "S3";  V = "single-3";        A = @("--pets=3", "--anim=30", "--hittest=A", "--overlay=single") },
    @{ S = "S3";  V = "single-5";        A = @("--pets=5", "--anim=30", "--hittest=A", "--overlay=single") },
    @{ S = "S3";  V = "small-5-surface"; A = @("--pets=5", "--anim=30", "--hittest=A", "--content=surface") },
    @{ S = "S6";  V = "cube";            A = @("--pets=1", "--anim=30", "--hittest=A", "--cube") },
    @{ S = "S5";  V = "$GameMode-hidden"; A = @("--pets=0"); Game = $true },
    @{ S = "S5";  V = "$GameMode-static"; A = @("--pets=1", "--anim=0", "--hittest=A"); Game = $true },
    @{ S = "S5";  V = "$GameMode-anim";   A = @("--pets=1", "--anim=30", "--hittest=A"); Game = $true }
)
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })  # -File 호출 시 "S1,S2" 한 문자열로 들어옴
if ($Only.Count -gt 0) { $matrix = $matrix | Where-Object { $Only -contains $_.S } }
else { $matrix = $matrix | Where-Object { -not $_.Game } }  # S5는 명시적으로 -Only S5 일 때만
# 핵심 시나리오 (D20, 예비 판정용 — 계획서 기준 10분×3회 정식 측정과 구분)
$coreKeys = @("S0-A/tray-only", "S0-B/gpu-no-window", "S1/A", "S1/B", "S1/C", "S2/A-hires", "S2/A-msgwait", "S2/C-hires", "S3/small-5", "S3/single-5")
if ($Core) { $matrix = $matrix | Where-Object { $coreKeys -contains ("{0}/{1}" -f $_.S, $_.V) } }

if ($matrix | Where-Object { $_.Game }) {
    $gp = [IO.Path]::GetFileNameWithoutExtension($GameProcess)
    if (-not (Get-Process -Name $gp -ErrorAction SilentlyContinue)) { throw "S5: $GameProcess 가 실행 중이 아님. 게임을 먼저 실행" }
    if ($PresentMonPath -and -not (Test-Path $PresentMonPath)) { throw "PresentMon 경로 없음: $PresentMonPath" }
    if (-not $PresentMonPath) { Write-Warning "PresentMon 미지정 — present mode/frame time 없이 p0 지표만 기록" }
}

# 장시간 무인 실행 중 디스플레이 꺼짐·절전 방지 (이 스크립트 프로세스 동안만 유효, 설정 변경 아님)
Add-Type -Namespace P0 -Name Power -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint esFlags);'
$ES_CONTINUOUS = [uint32]"0x80000000"; $ES_SYSTEM_REQUIRED = [uint32]1; $ES_DISPLAY_REQUIRED = [uint32]2
[void][P0.Power]::SetThreadExecutionState($ES_CONTINUOUS -bor $ES_SYSTEM_REQUIRED -bor $ES_DISPLAY_REQUIRED)

$total = $matrix.Count * $Runs * ($DurationSec + $WarmupSec + $CooldownSec)
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$tag = if ($Core) { "_core" } elseif ($Quick) { "_quick" } else { "" }
$outDir = Join-Path $root "logs\p0\$stamp$tag"
New-Item -ItemType Directory -Force $outDir | Out-Null
Write-Host ("시나리오 {0}개 × {1}회, 예상 {2:N1}시간 → {3}" -f $matrix.Count, $Runs, ($total / 3600), $outDir)

foreach ($run in 1..$Runs) {
    foreach ($m in $matrix) {
        $name = "{0}_{1}_run{2}" -f $m.S, $m.V, $run
        $csv = Join-Path $outDir "$name.csv"
        $args = @($m.A) + @("--duration=$DurationSec", "--warmup=$WarmupSec", "--scenario=$($m.S)", "--variant=$($m.V)",
                            "--run=$run", "--log=$csv", "--debug-layer=off", "--anchor=$Anchor")
        Write-Host ("[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $name)
        $p = Start-Process -FilePath $spike -ArgumentList $args -PassThru
        $c = Start-Process -FilePath $counters -ArgumentList @("--pid=$($p.Id)", "--out=$(Join-Path $outDir "$name`_counters.csv")") -PassThru -WindowStyle Hidden
        $pm = $null
        if ($m.Game -and $PresentMonPath) {
            # [검증] PresentMon 2.x CLI 옵션. 버전에 따라 이름이 다를 수 있음
            $pm = Start-Process -FilePath $PresentMonPath -ArgumentList @("--process_name", $GameProcess, "--output_file",
                (Join-Path $outDir "$name`_presentmon.csv"), "--timed", "$($DurationSec + $WarmupSec)",
                "--terminate_after_timed", "--no_console_stats") -PassThru -WindowStyle Hidden
        }
        $p.WaitForExit()
        if ($p.ExitCode -ne 0) { Write-Warning "$name exit=$($p.ExitCode)" }
        if (-not $c.WaitForExit(5000)) { Stop-Process -Id $c.Id -Force }
        if ($pm -and -not $pm.WaitForExit(15000)) { Stop-Process -Id $pm.Id -Force }
        Start-Sleep -Seconds $CooldownSec
    }
}
[void][P0.Power]::SetThreadExecutionState($ES_CONTINUOUS)
Write-Host "완료 → $outDir"
Write-Host "요약: powershell -ExecutionPolicy Bypass -File tools\measure\summarize.ps1 -Dir `"$outDir`""
