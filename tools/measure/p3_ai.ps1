# P3-10 AI 대화 성능 (P3 계획 §4.3). 60초 × 3회 단축 방식. 모의 SIWC 서버(127.0.0.1)만 사용
#   I0 = AI 끔 1마리 정지 (P2 M1 조건), I1 = AI 켬 + 로그인 상태 1마리 정지 (대화창 닫음),
#   S1 = 응답 스트리밍 계속 (초당 약 10 delta, 말풍선·대화창·talk 애니메이션)
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
$outDir = Join-Path $root ("logs\p3\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_ai")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace PA -Name N -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr after, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern System.IntPtr GetDlgItem(System.IntPtr h, int id);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr SendMessageW(System.IntPtr h, uint m, System.IntPtr w, string l);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
'@
[void][PA.N]::SetThreadExecutionState([uint32]"0x80000003")


$mockExe = Join-Path $bin "mock-siwc.exe"
$tmp = Join-Path $env:TEMP ("dp_p3m_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force "$tmp\cfg" | Out-Null
$cfg = Join-Path $tmp "cfg\config.toml"
[IO.File]::WriteAllText($cfg, "[ai]`nhotkey = `"Ctrl+Alt+F9`"`n")
$portFile = Join-Path $tmp "port.txt"
$mock = Start-Process $mockExe -ArgumentList @("--port-file=$portFile") -PassThru -WindowStyle Hidden
for ($i = 0; $i -lt 100 -and -not (Test-Path $portFile); $i++) { Start-Sleep -Milliseconds 100 }
$base = "http://127.0.0.1:" + (Get-Content $portFile).Trim()
$urlFile = Join-Path $tmp "url.txt"
function Chat { [PA.N]::FindWindowW("DesktopPet.Chat", [IntPtr]::Zero) }
function ChatClick($id) { $c = Chat; [void][PA.N]::PostMessageW($c, 0x111, [IntPtr]$id, [PA.N]::GetDlgItem($c, $id)) }

$scenarios = @(
    @{ Id = "I0"; Name = "AI 끔 1마리 정지"; Ai = $false },
    @{ Id = "I1"; Name = "AI 켬·로그인 1마리 정지"; Ai = $true },
    @{ Id = "S1"; Name = "응답 스트리밍 계속"; Ai = $true; Stream = $true }
)
if ($Only.Count) { $scenarios = $scenarios | Where-Object { $Only -contains $_.Id } }

try {
foreach ($run in 1..$Runs) {
    foreach ($s in $scenarios) {
        $name = "{0}_run{1}" -f $s.Id, $run
        Write-Host ("[{0}] {1} {2}" -f (Get-Date -Format "HH:mm:ss"), $name, $s.Name)
        [void][PA.N]::SetCursorPos(80, 80)
        $csv = Join-Path $outDir "$name.csv"
        $petArgs = @("--config=$cfg", "--no-behavior", "--pets=1", "--character=test-orange", "--spawn=2300,300",
                  "--metrics=$csv", "--duration=$($Seconds + $WarmupSec)")
        if ($s.Ai) { $petArgs += @("--ai", "--ai-endpoint=$base", "--ai-open-url-file=$urlFile") }
        $null = Invoke-RestMethod -UseBasicParsing "$base/control?reset=1&responses=$(if ($s.Stream) { 'slow' } else { 'ok' })"
        $pp = Start-Process $pet -ArgumentList $petArgs -PassThru
        $cp = Start-Process $counters -ArgumentList @("--pid=$($pp.Id)", "--out=$(Join-Path $outDir "$name`_counters.csv")") -PassThru -WindowStyle Hidden
        if ($s.Ai) {
            # 안정화 구간(10초) 안에 로그인을 끝낸다
            Start-Sleep -Seconds 2
            [void][PA.N]::PostMessageW([PA.N]::FindWindowW("DesktopPet.App", [IntPtr]::Zero), 0x111, [IntPtr]110, [IntPtr]::Zero)
            for ($i = 0; $i -lt 30 -and (Chat) -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100 }
            Remove-Item $urlFile -ErrorAction SilentlyContinue
            ChatClick 211  # Continue with ChatGPT (회차마다 auth 폴더를 지워 로그아웃 상태에서 시작)
            for ($i = 0; $i -lt 50 -and -not (Test-Path $urlFile); $i++) { Start-Sleep -Milliseconds 100 }
            if (Test-Path $urlFile) { try { $null = Invoke-WebRequest -UseBasicParsing (Get-Content $urlFile -Raw).Trim() -TimeoutSec 10 } catch { } }
            Start-Sleep -Seconds 2
            if (-not $s.Stream) { [void][PA.N]::PostMessageW((Chat), 0x10, [IntPtr]::Zero, [IntPtr]::Zero) }  # 대화창 닫기
        }
        if ($s.Stream) {
            while (-not $pp.HasExited) {
                [void][PA.N]::SendMessageW([PA.N]::GetDlgItem((Chat), 216), 0x0C, [IntPtr]::Zero, "keep talking")
                ChatClick 217
                Start-Sleep -Seconds 24  # 느린 모드 응답 약 20초 + 여유 (응답 중에 누르면 중지가 된다)
            }
        }
        $pp.WaitForExit()
        if (-not $cp.WaitForExit(5000)) { Stop-Process -Id $cp.Id -Force }
        Remove-Item -Recurse -Force (Join-Path $tmp "cfg\auth") -ErrorAction SilentlyContinue  # 다음 회차도 로그아웃 상태에서 시작
        Start-Sleep -Seconds 3
    }
}
}
finally {
    if ($mock -and -not $mock.HasExited) { Stop-Process -Id $mock.Id -Force }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
[void][PA.N]::SetThreadExecutionState([uint32]"0x80000000")

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
