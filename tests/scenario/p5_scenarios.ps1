# P5 3D 시나리오 (P5 계획 §4): 3D 펫(test-fox)의 착지·행동→애니메이션·클릭 판정(Region)·GPU 재생성, 코어 불변.
# 화면 캡처는 하지 않는다. 펫이 그린 픽셀만 --render-dump 로 받아 클릭할 점(불투명·투명)을 고른다.
# 실행 중 테스트 창 2개가 뜨고 마우스가 움직이며 클릭한다. PC 를 만지지 않는다.
param([string]$Preset = "x64-release", [string]$BaseCommit = "57bceb8")
$ErrorActionPreference = "Stop"
$env:DP_TEST_LANGUAGE = "ko"; $env:DP_TEST_NO_WELCOME = "1"  # 시나리오: 화면 문구 한국어 고정, 환영 창 끔
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pet = Join-Path $root "build\$Preset\desktop-pet.exe"
$outDir = Join-Path $root ("logs\p5\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_scenarios")
$tmp = Join-Path $env:TEMP ("dp_p5_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force $outDir, $tmp | Out-Null
Add-Type -AssemblyName System.Drawing

Add-Type -Namespace P5 -Name N -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int data, System.UIntPtr extra);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
'@
function Rect($h) { $r = New-Object P5.N+RECT; $null = [P5.N]::GetWindowRect($h, [ref]$r); [pscustomobject]@{ L = $r.L; T = $r.T; R = $r.R; B = $r.B } }
function CurClick($x, $y) { $null = [P5.N]::SetCursorPos($x, $y); Start-Sleep -Milliseconds 30; [P5.N]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [P5.N]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero) }
function AppCommand($id) { $null = [P5.N]::PostMessageW([P5.N]::FindWindowW("DesktopPet.App", [IntPtr]::Zero), 0x111, [IntPtr]$id, [IntPtr]::Zero) }
function PetRect { Rect ([P5.N]::FindWindowW("DesktopPet.Pet", [IntPtr]::Zero)) }

function Start-Target($name, $x, $y, $w, $h, $argb) {
    $log = Join-Path $tmp "$name.log"
    $p = Start-Process powershell -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "target_window.ps1"),
        "-EventLog", $log, "-X", $x, "-Y", $y, "-W", $w, "-H", $h, "-Title", $name, "-Argb", $argb) -PassThru
    for ($i = 0; $i -lt 60 -and -not ((Test-Path $log) -and (Select-String -Path $log -Pattern " hwnd " -Quiet)); $i++) { Start-Sleep -Milliseconds 200 }
    [pscustomobject]@{ Proc = $p; Log = $log }
}

$logDir = Join-Path $env:LOCALAPPDATA "DesktopPet\logs"
$script:petLog = $null; $script:mark = 0
function PetLines { @(Get-Content $script:petLog -Encoding UTF8) }
function Mark { $script:mark = (PetLines).Count }
function WaitLog($pattern, $sec = 5) {
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        $hit = PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match $pattern } | Select-Object -First 1
        if ($hit) { return $hit }
        Start-Sleep -Milliseconds 100
    }
    return $null
}
$results = New-Object System.Collections.ArrayList
function Check($id, $name, $ok, $detail) {
    [void]$results.Add([pscustomobject]@{ Id = $id; Scenario = $name; Pass = [bool]$ok; Detail = "$detail" })
    Write-Host ("[{0}] {1} {2} — {3}" -f $id, $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail)
}

# 펫이 그린 픽셀(덤프)에서 불투명 점(가장자리에서 먼)과 투명 점(실루엣에서 먼)을 고른다
function PickPoints($png) {
    $b = [Drawing.Bitmap]::FromFile($png)
    try {
        $w = $b.Width; $h = $b.Height
        $a = New-Object 'int[,]' $w, $h
        for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) { $a[$x, $y] = $b.GetPixel($x, $y).A } }
    } finally { $b.Dispose() }
    $best = $null; $bestD = -1; $far = $null; $farD = -1
    for ($y = 2; $y -lt $h - 2; $y += 2) {
        for ($x = 2; $x -lt $w - 2; $x += 2) {
            # 가장 가까운 반대 픽셀까지 거리 (작은 창이라 무차별 탐색)
            $opaque = $a[$x, $y] -ge 128; $d = 99
            for ($r = 1; $r -lt 20 -and $d -eq 99; $r++) {
                foreach ($o in @(@($r, 0), @(-$r, 0), @(0, $r), @(0, -$r), @($r, $r), @(-$r, -$r), @($r, -$r), @(-$r, $r))) {
                    $xx = $x + $o[0]; $yy = $y + $o[1]
                    if ($xx -lt 0 -or $yy -lt 0 -or $xx -ge $w -or $yy -ge $h) { continue }
                    if (($a[$xx, $yy] -ge 128) -ne $opaque) { $d = $r; break }
                }
            }
            if ($opaque -and $d -gt $bestD) { $bestD = $d; $best = @($x, $y) }
            if (-not $opaque -and $d -gt $farD) { $farD = $d; $far = @($x, $y) }
        }
    }
    [pscustomobject]@{ Opaque = $best; OpaqueDist = $bestD; Clear = $far; ClearDist = $farD; W = $w; H = $h }
}

$p = $null; $bg = $null; $ledge = $null
try {
    # 배경 창(넓게) + 받침 창(앞, 펫이 착지할 곳)
    $bg = Start-Target "P5-bg" 1000 300 1000 500 -16744448  # 받침 창(800~)과 겹치지 않게: 클릭해 앞으로 나와도 받침을 가리지 않음
    $ledge = Start-Target "P5-ledge" 1250 800 500 60 -12566464
    $cfg = Join-Path $tmp "config.toml"
    [IO.File]::WriteAllText($cfg, "[display]`nproximity_reaction = false`n")
    $dump = Join-Path $tmp "render.png"
    $before = @(Get-ChildItem $logDir -Filter *.log | Select-Object -ExpandProperty Name)
    $p = Start-Process $pet -ArgumentList @("--config=$cfg", "--pets=1", "--character=test-fox", "--no-behavior", "--spawn=1500,400",
        "--render-dump=$dump", "--test-hooks", "--duration=120") -PassThru
    for ($i = 0; $i -lt 50 -and -not $script:petLog; $i++) {
        Start-Sleep -Milliseconds 200
        $new = Get-ChildItem $logDir -Filter *.log | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($new) { $script:petLog = $new.FullName }
    }

    $lib = WaitLog "\[library\] loaded id=test-fox .*renderer=gltf" 5
    Check "L1" "3D 패키지 로드 (renderer=gltf)" $lib "$lib"
    $land = WaitLog "\[pet0\] event=Landed" 8
    $m = [regex]::Match("$land", "foot=\((-?\d+),(-?\d+)\)")
    Check "F1" "3D 펫이 받침 창 위에 착지 (발 y = 창 상단 800)" ($m.Success -and [int]$m.Groups[2].Value -eq 800) "$land"
    $r = PetRect
    Check "F2" "펫 창 아래 가장자리 = 발 위치, 너비는 모델 비율" ($r.B -eq 800 -and ($r.R - $r.L) -gt ($r.B - $r.T)) ("rect=({0},{1},{2},{3})" -f $r.L, $r.T, $r.R, $r.B)
    $a1 = WaitLog "\[pet0\] model anim=Survey action=idle" 4
    Check "A1" "행동 → 애니메이션 (idle → Survey, 낙하는 Run)" ($a1 -and (PetLines | Where-Object { $_ -match "model anim=Run action=fall" })) "$a1"

    # 클릭 판정: 최신 덤프에서 점을 고른다 (Region 은 최대 0.1초 + 1프레임 늦게 따라온다)
    Start-Sleep -Seconds 2
    $png = Get-ChildItem $tmp -Filter "render_*.png" | Sort-Object LastWriteTime | Select-Object -Last 1
    $pt = PickPoints $png.FullName
    $r = PetRect
    $ox = $r.L + $pt.Opaque[0]; $oy = $r.T + $pt.Opaque[1]
    $cx = $r.L + $pt.Clear[0]; $cy = $r.T + $pt.Clear[1]
    Mark
    $bgBefore = @(Get-Content $bg.Log).Count
    CurClick $cx $cy; Start-Sleep -Milliseconds 400
    $bgHit = @(Get-Content $bg.Log | Select-Object -Skip $bgBefore | Where-Object { $_ -match " down " }).Count -gt 0
    Check "H1" "모델 바깥 투명 부분 클릭 → 뒤 창이 받음" ($bgHit -and -not (WaitLog "\[pet0\] click" 1)) ("clear=({0},{1}) dist={2}" -f $cx, $cy, $pt.ClearDist)
    Mark
    CurClick $ox $oy
    $c = WaitLog "\[pet0\] click" 2
    Check "H2" "모델 몸 클릭 → 펫이 받음" $c ("opaque=({0},{1}) dist={2} | {3}" -f $ox, $oy, $pt.OpaqueDist, $c)

    # GPU 재생성 강제 → 다시 그림
    Mark
    AppCommand 9000
    $g = WaitLog "\[render\] GPU 재생성 완료 \(test\)" 5
    $a2 = WaitLog "\[pet0\] model anim=" 3
    Check "G1" "GPU 재생성 후 3D 펫을 다시 그림" ($g -and $a2) "$a2"

    AppCommand 106
    $exited = $p.WaitForExit(8000)
    Check "X0" "정상 종료" $exited ""

    # 코어 불변 (기준선 P5 완료 판정)
    $diff = git -C $root diff --stat $BaseCommit -- src/core/physics.h src/core/physics.cpp src/core/world.h src/core/world.cpp src/core/behavior
    Check "C1" "물리·월드·행동 코어 변경 없음 (P5 시작 $BaseCommit 대비)" ([string]::IsNullOrWhiteSpace(($diff -join ""))) ($diff -join " ")
}
finally {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    foreach ($t in @($bg, $ledge)) { if ($t -and $t.Proc -and -not $t.Proc.HasExited) { Stop-Process -Id $t.Proc.Id -Force } }
    if ($script:petLog) { Copy-Item $script:petLog (Join-Path $outDir "desktop-pet.log") }
    Get-ChildItem $tmp -Filter "render_*.png" -ErrorAction SilentlyContinue | Select-Object -Last 1 | Copy-Item -Destination $outDir
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    $results | Export-Csv (Join-Path $outDir "results.csv") -NoTypeInformation -Encoding UTF8
    $pass = @($results | Where-Object Pass).Count
    Write-Host ("결과: {0}/{1} 통과 → {2}" -f $pass, $results.Count, $outDir)
}
