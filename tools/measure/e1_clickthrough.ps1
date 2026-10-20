# E1 자동 클릭 통과 검증 (SendInput 계열로 실제 커서·클릭 발생 → OS hit-test 경로를 그대로 통과)
# 대상 창(별도 프로세스) 위에 펫을 띄우고, 투명/불투명 지점 입력이 어디로 가는지 기록한다.
# 실행 중 마우스가 자동으로 움직이므로 PC를 만지지 않는다.
param(
    [string]$Preset = "x64-release",
    [string[]]$Variants = @("A", "A-fast", "B", "C", "none")   # A-fast: 커서 확인 주기 16/33ms
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bin = Join-Path $root "build\$Preset"
$spike = Join-Path $bin "p0-spike.exe"
$Variants = @($Variants | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$outDir = Join-Path $root ("logs\p0\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_e1")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace E1 -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int data, UIntPtr extra);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, IntPtr title);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
'@
$LDOWN = 0x02; $LUP = 0x04; $WHEEL = 0x800
function CurMove($x, $y) { [void][E1.Native]::SetCursorPos($x, $y) }
function CurClick($x, $y) { CurMove $x $y; Start-Sleep -Milliseconds 15; [E1.Native]::mouse_event($LDOWN, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 30; [E1.Native]::mouse_event($LUP, 0, 0, 0, [UIntPtr]::Zero) }
function CurWheel($x, $y) { CurMove $x $y; Start-Sleep -Milliseconds 15; [E1.Native]::mouse_event($WHEEL, 0, 0, -120, [UIntPtr]::Zero) }
function PetRect { $h = [E1.Native]::FindWindowW("DesktopPet.P0.Pet", [IntPtr]::Zero); $r = New-Object E1.Native+RECT; $null = [E1.Native]::GetWindowRect($h, [ref]$r); return [pscustomobject]@{ L = [int]$r.L; T = [int]$r.T } }

# ---- 대상 창 ----
$targetLog = Join-Path $outDir "target_events.log"
$tp = Start-Process powershell -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "e1_target.ps1"), "-EventLog", $targetLog) -PassThru
for ($i = 0; $i -lt 50 -and -not ((Test-Path $targetLog) -and (Select-String -Path $targetLog -Pattern " hwnd " -Quiet)); $i++) { Start-Sleep -Milliseconds 200 }
$targetHwnd = [IntPtr][long]((Select-String -Path $targetLog -Pattern "hwnd (\d+)").Matches[0].Groups[1].Value)
function TargetLines { @(Get-Content $targetLog) }

$results = @()
foreach ($v in $Variants) {
    $ht = if ($v -eq "A-fast") { "A" } else { $v }
    $extra = if ($v -eq "A-fast") { @("--poll-near=16", "--poll-far=33") } else { @() }
    $args = $extra + @("--hittest=$ht", "--pets=1", "--anim=30", "--anchor=top-left", "--proximity=on", "--debug-layer=off", "--duration=45")
    $before = Get-ChildItem (Join-Path $bin "logs") | Select-Object -ExpandProperty Name
    $sp = Start-Process $spike -ArgumentList $args -PassThru
    Start-Sleep -Milliseconds 1500
    $spikeLog = Get-ChildItem (Join-Path $bin "logs") | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
    $r = PetRect
    # 창 기준 좌표: 스프라이트 (0,32)-(128,160), 위쪽 32px는 점프 여유(투명)
    $transA = @(($r.L + 64), ($r.T + 8))     # 스프라이트 위 여백 (창 내부, 완전 투명)
    $transB = @(($r.L + 4), ($r.T + 36))     # 스프라이트 사각형 좌상단 모서리 (알파 0)
    $opaque = @(($r.L + 64), ($r.T + 120))   # 배 (항상 불투명)
    $far = @(1800, 1000)

    CurClick 700 420; Start-Sleep -Milliseconds 400  # 대상 창 활성화 (펫과 겹치지 않는 지점)

    $tests = @(
        @{ Name = "T1 투명(여백) 클릭";    Act = { CurMove $transA[0] $transA[1]; Start-Sleep -Milliseconds 400; CurClick $transA[0] $transA[1] }; Expect = "target" },
        @{ Name = "T2 투명(모서리) 클릭";  Act = { CurMove $transB[0] $transB[1]; Start-Sleep -Milliseconds 400; CurClick $transB[0] $transB[1] }; Expect = "target" },
        @{ Name = "T3 투명 휠";            Act = { CurMove $transA[0] $transA[1]; Start-Sleep -Milliseconds 400; CurWheel $transA[0] $transA[1] }; Expect = "target-wheel" },
        @{ Name = "T4 불투명 클릭";        Act = { CurMove $opaque[0] $opaque[1]; Start-Sleep -Milliseconds 400; CurClick $opaque[0] $opaque[1] }; Expect = "pet" },
        @{ Name = "T5 빠른 접근 후 첫 클릭"; Act = { CurMove $far[0] $far[1]; Start-Sleep -Milliseconds 700; CurClick $opaque[0] $opaque[1] }; Expect = "pet" }
    )
    foreach ($t in $tests) {
        Start-Sleep -Milliseconds 600
        $tl0 = (TargetLines).Count
        $pl0 = @(Select-String -Path $spikeLog.FullName -Pattern "E1 클릭|입력 가로챔" -Encoding UTF8).Count
        & $t.Act
        Start-Sleep -Milliseconds 500
        $newT = @(TargetLines | Select-Object -Skip $tl0)
        $newP = @(Select-String -Path $spikeLog.FullName -Pattern "E1 클릭|입력 가로챔" -Encoding UTF8 | Select-Object -Skip $pl0)
        $gotT = @($newT | Where-Object { $_ -match " down | wheel " }).Count
        $gotW = @($newT | Where-Object { $_ -match " wheel " }).Count
        $gotP = $newP.Count
        $fg = [E1.Native]::GetForegroundWindow() -eq $targetHwnd
        $ok = switch ($t.Expect) { "target" { $gotT -ge 1 -and $gotP -eq 0 } "target-wheel" { $gotW -ge 1 } "pet" { $gotP -ge 1 -and $gotT -eq 0 } }
        $results += [pscustomobject]@{ Variant = $v; Test = $t.Name; Expect = $t.Expect; Target = $gotT; Pet = $gotP; FgKept = $fg; Pass = $ok }
    }

    # T6 드래그: 불투명 지점에서 150px 오른쪽으로
    Start-Sleep -Milliseconds 400
    $r0 = PetRect
    CurMove $opaque[0] $opaque[1]; Start-Sleep -Milliseconds 400
    [E1.Native]::mouse_event($LDOWN, 0, 0, 0, [UIntPtr]::Zero)
    for ($k = 1; $k -le 15; $k++) { CurMove ($opaque[0] + 10 * $k) $opaque[1]; Start-Sleep -Milliseconds 20 }
    [E1.Native]::mouse_event($LUP, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    $r1 = PetRect
    $dx = $r1.L - $r0.L
    $results += [pscustomobject]@{ Variant = $v; Test = "T6 드래그 150px"; Expect = "pet-moved"; Target = ""; Pet = "dx=$dx"; FgKept = ([E1.Native]::GetForegroundWindow() -eq $targetHwnd); Pass = ($dx -ge 120) }

    # T7 애니메이션 중 번갈아 클릭 (안정 대기 없이 150ms 간격, 투명/불투명 각 5회)
    $r = PetRect
    $ta = @(($r.L + 64), ($r.T + 8)); $op = @(($r.L + 64), ($r.T + 120))
    Start-Sleep -Milliseconds 600
    $tl0 = (TargetLines).Count
    $pl0 = @(Select-String -Path $spikeLog.FullName -Pattern "E1 클릭" -Encoding UTF8).Count
    for ($k = 0; $k -lt 5; $k++) { CurClick $ta[0] $ta[1]; Start-Sleep -Milliseconds 150; CurClick $op[0] $op[1]; Start-Sleep -Milliseconds 150 }
    Start-Sleep -Milliseconds 500
    $gotT = @(TargetLines | Select-Object -Skip $tl0 | Where-Object { $_ -match " down " }).Count
    $gotP = @(Select-String -Path $spikeLog.FullName -Pattern "E1 클릭" -Encoding UTF8 | Select-Object -Skip $pl0).Count
    $results += [pscustomobject]@{ Variant = $v; Test = "T7 번갈아 클릭 5+5"; Expect = "target5/pet5"; Target = $gotT; Pet = $gotP; FgKept = ""; Pass = ($gotT -eq 5 -and $gotP -eq 5) }

    CurMove $far[0] $far[1]
    # 트레이 메뉴 "종료"와 같은 경로로 정상 종료 (트레이 아이콘 잔류 방지)
    [void][E1.Native]::PostMessageW([E1.Native]::FindWindowW("DesktopPet.P0.App", [IntPtr]::Zero), 0x111, [IntPtr]105, [IntPtr]::Zero)
    if (-not $sp.WaitForExit(3000)) { Stop-Process -Id $sp.Id -Force }
    Copy-Item $spikeLog.FullName (Join-Path $outDir "spike_$v.log")
    Start-Sleep -Milliseconds 500
}
Stop-Process -Id $tp.Id -Force -ErrorAction SilentlyContinue
$results | Export-Csv -Path (Join-Path $outDir "e1_results.csv") -NoTypeInformation -Encoding UTF8
$results | Format-Table -AutoSize
Write-Host "→ $outDir"
