# P2-08 캐릭터 패키지·다중 펫 시나리오 테스트 (P2 계획 §4.2)
# 사용자 config.toml 은 건드리지 않는다: 임시 폴더의 --config / --characters-dir 로 실행한다.
# 실행 중 마우스가 자동으로 움직인다 (펫 클릭). PC를 만지지 않는다.
param([string]$Preset = "x64-release")
$ErrorActionPreference = "Stop"
$env:DP_TEST_LANGUAGE = "ko"; $env:DP_TEST_NO_WELCOME = "1"  # 시나리오: 화면 문구 한국어 고정, 환영 창 끔
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pet = Join-Path $root "build\$Preset\desktop-pet.exe"
$outDir = Join-Path $root ("logs\p2\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_scenarios")
$tmp = Join-Path $env:TEMP ("dp_p2_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force $outDir, $tmp | Out-Null

Add-Type -Namespace P2 -Name N -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowExW(System.IntPtr parent, System.IntPtr after, string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool IsWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int data, System.UIntPtr extra);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
'@
function CurMove($x, $y) { $null = [P2.N]::SetCursorPos($x, $y) }
function CurClick($x, $y) { CurMove $x $y; Start-Sleep -Milliseconds 30; [P2.N]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [P2.N]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero) }
function PetWindows { $list = @(); $h = [IntPtr]::Zero; while ($true) { $h = [P2.N]::FindWindowExW([IntPtr]::Zero, $h, "DesktopPet.Pet", [IntPtr]::Zero); if ($h -eq [IntPtr]::Zero) { break }; $list += $h }; $list }
function ClickPet($hwnd) {
    $r = New-Object P2.N+RECT; $null = [P2.N]::GetWindowRect($hwnd, [ref]$r)
    CurClick ([int](($r.L + $r.R) / 2)) ([int]($r.T + ($r.B - $r.T) * 0.75))
    CurMove 2400 200  # 커서 근접(look) 영향 제거
}
function AppCommand($id) { $null = [P2.N]::PostMessageW([P2.N]::FindWindowW("DesktopPet.App", [IntPtr]::Zero), 0x111, [IntPtr]$id, [IntPtr]::Zero) }

# ---- 로그 ----
$logDir = Join-Path $env:LOCALAPPDATA "DesktopPet\logs"
$script:petLog = $null
$script:mark = 0
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
function All($pattern) { @(PetLines | Where-Object { $_ -match $pattern }) }
function SinceMark($pattern) { @(PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match $pattern }) }
function HwndOf($petId) {
    $l = All ("\[pet$petId\] spawn .*hwnd=0x([0-9A-F]+)") | Select-Object -Last 1
    if ($l -match "hwnd=0x([0-9A-F]+)") { return [IntPtr][Convert]::ToInt64($Matches[1], 16) }
    return [IntPtr]::Zero
}
function LibraryOrder { @(All "\[library\] loaded id=" | ForEach-Object { if ($_ -match "id=(\S+)") { $Matches[1] } } | Select-Object -Unique) }

$results = New-Object System.Collections.ArrayList
function Check($id, $name, $ok, $detail) {
    [void]$results.Add([pscustomobject]@{ Id = $id; Scenario = $name; Pass = [bool]$ok; Detail = $detail })
    Write-Host ("[{0}] {1} {2} — {3}" -f $id, $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail)
}

# 실행 1회: 시작 → 로그 찾기 → $body 실행 → 106 종료
function Session($name, [string[]]$petArgs, [scriptblock]$body) {
    $before = @(Get-ChildItem $logDir -Filter *.log | Select-Object -ExpandProperty Name)
    $script:petLog = $null
    $p = Start-Process $pet -ArgumentList ($petArgs + @("--duration=120")) -PassThru
    for ($i = 0; $i -lt 50 -and -not $script:petLog; $i++) {
        Start-Sleep -Milliseconds 200
        $new = Get-ChildItem $logDir -Filter *.log | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($new) { $script:petLog = $new.FullName }
    }
    $script:mark = 0
    $null = WaitLog "시작: 펫" 8
    try { & $body $p }
    catch { Check "$name-ERR" "스크립트 예외" $false "$_" }
    finally {
        AppCommand 106
        if (-not $p.WaitForExit(5000)) { Stop-Process -Id $p.Id -Force }
        if ($script:petLog) { Copy-Item $script:petLog (Join-Path $outDir "$name.log") }
    }
    return $p
}

# ---- 임시 패키지·설정 ----
$src = Join-Path $root "characters\test-orange"
function New-Pkg($dir, $id, [hashtable]$replace = @{}, [string]$lua = $null) {
    $d = Join-Path $dir $id
    Copy-Item $src $d -Recurse
    $m = Get-Content (Join-Path $d "manifest.toml") -Raw -Encoding UTF8
    $m = $m -replace 'id = "test-orange"', "id = `"$id`""
    foreach ($k in $replace.Keys) { $m = $m -replace $k, $replace[$k] }
    [IO.File]::WriteAllText((Join-Path $d "manifest.toml"), $m)
    if ($null -ne $lua) {
        [IO.File]::WriteAllText((Join-Path $d "behaviors\custom.lua"), $lua)
        Add-Content (Join-Path $d "behaviors\behavior.toml") "`n[script]`nfile = `"behaviors/custom.lua`""
    }
}
function New-Config($path, [string]$pets) {
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    [IO.File]::WriteAllText($path, "[display]`nproximity_reaction = false`n$pets")
}

$cfgA = Join-Path $tmp "a\config.toml"   # 5마리 2종 + 사용자 폴더 우선순위
New-Config $cfgA "[[pets]]`ncharacter = `"test-orange`"`ncount = 3`n[[pets]]`ncharacter = `"test-blue`"`ncount = 2`n"
New-Pkg (Join-Path $tmp "a\characters") "test-orange" @{ 'version = "1.0.0"' = 'version = "9.9.9"' }

$cfgB = Join-Path $tmp "b\config.toml"   # config 만 바꿔 캐릭터 교체
New-Config $cfgB "[[pets]]`ncharacter = `"test-blue`"`ncount = 1`n"

$bad = Join-Path $tmp "bad"              # 잘못된 패키지 모음 (--characters-dir)
New-Item -ItemType Directory -Force $bad | Out-Null
New-Pkg $bad "bad-escape" @{ 'atlas = "render/sprite/atlas.png"' = 'atlas = "../../../escape.png"' }
New-Pkg $bad "bad-license" @{ '(?s)\[license\].*?(?=\[sprite\])' = '' }
New-Pkg $bad "big-lua" @{} ("-- " + ("x" * 300000) + "`nfunction on_rest(ctx) return nil end")
New-Pkg $bad "loop-load" @{} "while true do end"
New-Pkg $bad "loop-event" @{} "function on_event(name, ctx) while true do end end"
# 깨진 재정의: 뒤 경로의 test-blue 가 거부되면 exe 폴더의 test-blue 가 남아야 한다
Copy-Item (Join-Path $root "characters\test-blue") (Join-Path $bad "test-blue") -Recurse
$mb = Join-Path $bad "test-blue\manifest.toml"
[IO.File]::WriteAllText($mb, ((Get-Content $mb -Raw -Encoding UTF8) -replace 'renderers = \["sprite"\]', 'renderers = ["live2d"]'))
$cfgC = Join-Path $tmp "c\config.toml"
New-Config $cfgC "[[pets]]`ncharacter = `"loop-load`"`ncount = 1`n[[pets]]`ncharacter = `"loop-event`"`ncount = 1`n[[pets]]`ncharacter = `"bad-license`"`ncount = 1`n"

# 크기가 다른 캐릭터 혼합 (P2 보완): 256px 캐릭터를 함께 띄워도 128px 펫은 위 공간 180px 창에 착지해야 한다
$big = Join-Path $tmp "big"
New-Item -ItemType Directory -Force $big | Out-Null
New-Pkg $big "test-big" @{ 'base_size = 128' = 'base_size = 256' }
$cfgD = Join-Path $tmp "d\config.toml"
New-Config $cfgD "[[pets]]`ncharacter = `"test-orange`"`ncount = 1`n[[pets]]`ncharacter = `"test-big`"`ncount = 1`n"
function Start-Target($name, $x, $y, $w, $h) {
    $log = Join-Path $outDir "$name.log"
    $tp = Start-Process powershell -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "target_window.ps1"),
        "-EventLog", $log, "-X", $x, "-Y", $y, "-W", $w, "-H", $h, "-Title", $name, "-Argb", -13421705) -PassThru
    for ($i = 0; $i -lt 60 -and -not ((Test-Path $log) -and (Select-String -Path $log -Pattern " hwnd " -Quiet)); $i++) { Start-Sleep -Milliseconds 200 }
    $hwnd = [long]((Select-String -Path $log -Pattern "hwnd (\d+)").Matches[0].Groups[1].Value)
    [pscustomobject]@{ Proc = $tp; Hex = ("0x{0:X}" -f $hwnd) }
}

CurMove 2400 200
try {
    # ---- S1 5마리 2종 (config), 에셋 공유, 사용자 폴더 우선 ----
    $null = Session "S1_five_pets" @("--config=$cfgA", "--no-behavior", "--spawn=1500,150", "--spawn-step=-180") {
        $null = WaitLog "\[pet4\] spawn" 5
        Start-Sleep -Seconds 2
        $spawns = All "\] spawn character="
        $o = @($spawns | Where-Object { $_ -match "character=test-orange" }).Count
        $b = @($spawns | Where-Object { $_ -match "character=test-blue" }).Count
        $wins = @(PetWindows).Count
        Check "S1a" "5마리 2종 생성 (config [[pets]])" ($o -eq 3 -and $b -eq 2 -and $wins -eq 5) "orange=$o blue=$b windows=$wins"
        $refsO = ($spawns | Where-Object { $_ -match "test-orange" } | Select-Object -Last 1) -replace '.*asset_refs=(\d+).*', '$1'
        $refsB = ($spawns | Where-Object { $_ -match "test-blue" } | Select-Object -Last 1) -replace '.*asset_refs=(\d+).*', '$1'
        Check "S1b" "같은 캐릭터는 에셋 공유 (라이브러리 1 + 펫 수)" ($refsO -eq "4" -and $refsB -eq "3") "orange refs=$refsO blue refs=$refsB"
        $user = All "\[library\] loaded id=test-orange .*version=9\.9\.9"
        $dup = All "id test-orange 중복"
        Check "S1c" "사용자 캐릭터 폴더가 exe 폴더보다 우선 (P2-R8)" ($user.Count -eq 1 -and $dup.Count -eq 1) "$(@($user)[0])"
        $landed = @(All "event=Landed" | ForEach-Object { if ($_ -match "\[pet(\d+)\]") { $Matches[1] } } | Select-Object -Unique).Count
        Check "S1d" "5마리 모두 착지" ($landed -eq 5) "landed=$landed"
    }

    # ---- S2 코드 수정 없이 캐릭터 교체 (config / 명령행) ----
    $null = Session "S2_swap_config" @("--config=$cfgB", "--no-behavior", "--spawn=1500,150") {
        $s = WaitLog "\[pet0\] spawn" 5
        Check "S2a" "config 만 바꿔 캐릭터 교체" ($s -match "character=test-blue lua=on") "$s"
    }
    $null = Session "S2_swap_arg" @("--config=$cfgB", "--character=test-orange", "--pets=2", "--no-behavior", "--spawn=1500,150") {
        $null = WaitLog "\[pet1\] spawn" 5
        $sp = All "\] spawn character="
        Check "S2b" "--character 로 교체 (config 무시)" ($sp.Count -eq 2 -and @($sp | Where-Object { $_ -match "test-orange lua=off" }).Count -eq 2) "spawns=$($sp.Count)"
    }

    # ---- S3 Lua 행동 반영: 파랑은 클릭 → sit, 주황은 look ----
    $null = Session "S3_lua_click" @("--config=$cfgB", "--character=test-blue", "--pets=1", "--no-behavior", "--spawn=1500,150") {
        $null = WaitLog "\[pet0\] event=Landed" 5
        Start-Sleep -Milliseconds 500
        Mark
        ClickPet (HwndOf 0)
        $c = WaitLog "\[pet0\] click → act=" 3
        $act = WaitLog "\[pet0\] act=sit .*hooks=[1-9]" 2
        $back = WaitLog "\[pet0\] act=idle" 4
        Check "S3a" "파랑 클릭 → Lua on_event 가 sit 2초" ($c -match "act=sit" -and $act -and $back) "$c / $back"
    }
    $null = Session "S3_default_click" @("--config=$cfgB", "--character=test-orange", "--pets=1", "--no-behavior", "--spawn=1500,150") {
        $null = WaitLog "\[pet0\] event=Landed" 5
        Start-Sleep -Milliseconds 500
        Mark
        ClickPet (HwndOf 0)
        $c = WaitLog "\[pet0\] click → act=" 3
        Check "S3b" "주황 클릭 → 기본 FSM look" ($c -match "act=look") "$c"
    }

    # ---- S4 잘못된 패키지·악성 Lua ----
    $null = Session "S4_bad_packages" @("--config=$cfgC", "--characters-dir=$bad", "--no-behavior", "--spawn=1500,150", "--spawn-step=-200") {
        $null = WaitLog "\[pet1\] spawn" 5
        foreach ($id in "bad-escape", "bad-license", "big-lua") {
            $rej = All ("\[library\] rejected dir=.*\\" + $id + " ")
            $why = (PetLines | Select-String -Pattern ("rejected dir=.*\\" + $id + " ") -Context 0, 1 | ForEach-Object { $_.Context.PostContext -join " | " }) -join ""
            Check "S4-$id" "잘못된 패키지 거부: $id" ($rej.Count -eq 1 -and (All "loaded id=$id ").Count -eq 0) $why
        }
        $blueRej = All "rejected dir=.*\\bad\\test-blue "
        $blueOk = All "loaded id=test-blue .*version=1\.0\.0 .*root=.*build"
        Check "S4-override" "깨진 재정의 패키지 → 기존 같은 id 유지" ($blueRej.Count -eq 1 -and $blueOk.Count -eq 1 -and (All "id test-blue 중복").Count -eq 0) "$(@($blueRej)[0])"
        $unk = All "알 수 없는 캐릭터 'bad-license'"
        Check "S4-skip" "거부된 캐릭터 요청은 건너뛰고 나머지 실행" ($unk.Count -eq 1 -and @(PetWindows).Count -eq 2) "windows=$(@(PetWindows).Count)"
        $ll = All "Lua 로드 실패"
        $s0 = All "\[pet0\] spawn character=loop-load lua=off"
        Check "S4-loop-load" "최상위 무한 루프 Lua → 로드 실패, 기본 행동으로 실행" ($ll.Count -eq 1 -and $s0.Count -eq 1) "$(@($ll)[0])"
        $null = WaitLog "\[pet1\] event=Landed" 5
        Start-Sleep -Milliseconds 500
        $h1 = HwndOf 1
        for ($k = 0; $k -lt 3 -and -not (All "\[pet1\] lua disabled"); $k++) { ClickPet $h1; Start-Sleep -Milliseconds 600 }
        $dis = WaitLog "\[pet1\] lua disabled" 3
        $err = All "\[pet1\] lua error" | Select-Object -Last 1
        Check "S4-loop-event" "훅 안 무한 루프 → 명령 수 제한, 3회 연속 오류 후 비활성화" ($dis -and $err -match "instruction|명령") "$err"
        Start-Sleep -Milliseconds 500
        Mark
        ClickPet $h1
        $c = WaitLog "\[pet1\] click → act=look" 2
        Check "S4-fallback" "비활성화 후 기본 FSM 으로 계속 동작 (앱 생존)" ($c -and -not $p.HasExited) "$c"
    }

    # ---- S5 트레이 메뉴: 추가·교체·제거·다시 읽기·최대 마릿수 ----
    $null = Session "S5_tray" @("--config=$cfgB", "--character=test-orange", "--pets=7", "--no-behavior", "--spawn=2300,150", "--spawn-step=-150") {
        $null = WaitLog "\[pet6\] spawn" 5
        $order = LibraryOrder
        $iBlue = [array]::IndexOf($order, "test-blue")
        Mark
        AppCommand (1000 + $iBlue)
        $add = WaitLog "\[pet7\] spawn character=test-blue" 3
        Check "S5a" "트레이: 펫 추가 (파랑)" ($add -and @(PetWindows).Count -eq 8) "$add"
        Mark
        AppCommand (1000 + $iBlue)
        $max = WaitLog "최대 8마리" 3
        Check "S5b" "8마리 상한 (P2-R6)" ($max -and @(PetWindows).Count -eq 8) "$max"
        Mark
        AppCommand (2000 + [array]::IndexOf($order, "test-orange"))
        $sw = WaitLog "\[pet7\] swap test-blue → test-orange" 3
        Check "S5c" "트레이: 마지막 펫 캐릭터 교체" $sw "$sw"
        Mark
        AppCommand 107
        $rm = WaitLog "\[pet7\] remove" 3
        Start-Sleep -Milliseconds 300
        Check "S5d" "트레이: 마지막 펫 제거" ($rm -and @(PetWindows).Count -eq 7) "windows=$(@(PetWindows).Count)"
        Mark
        AppCommand 108
        $null = WaitLog "\[library\] 캐릭터 \d+개 로드" 5
        $null = WaitLog "\[pet\d+\] spawn" 3
        Start-Sleep -Milliseconds 800
        $sp = SinceMark "\] spawn character=test-orange"
        $last = ($sp | Select-Object -Last 1) -replace '.*asset_refs=(\d+).*', '$1'
        Check "S5e" "트레이: 패키지 다시 읽기 → 같은 구성 재생성" ($sp.Count -eq 7 -and $last -eq "8" -and @(PetWindows).Count -eq 7) "respawn=$($sp.Count) refs=$last windows=$(@(PetWindows).Count)"
    }

    # ---- S7 크기가 다른 캐릭터 혼합 ----
    $T = Start-Target "mixed_target" 600 180 900 500
    try {
        $null = Session "S7_mixed_sizes" @("--config=$cfgD", "--characters-dir=$big", "--no-behavior", "--spawn=800,170", "--spawn-step=500") {
            $o = WaitLog ("\[pet0\] mode=Attached host=" + $T.Hex) 6
            $b = WaitLog "\[pet1\] mode=Grounded" 6
            $sz = All "\[pet1\] spawn character=test-big"
            Check "S7a" "128px 펫은 위 공간 180px 창에 착지 (256px 펫과 함께 실행)" $o "$o"
            Check "S7b" "256px 펫은 그 창에 서지 못하고 바닥으로" ($b -and $sz.Count -ge 1) "$b"
        }
    } finally { if (-not $T.Proc.HasExited) { Stop-Process -Id $T.Proc.Id -Force } }

    # ---- S6 펫 0마리 → 트레이 상주 (D13) ----
    $p6 = Session "S6_zero_pets" @("--config=$cfgB", "--character=no-such-pet", "--no-behavior") {
        Start-Sleep -Seconds 3
        $w = All "실행할 펫이 없음"
        Check "S6" "알 수 없는 캐릭터만 요청 → 펫 0마리, 앱 상주" ($w.Count -eq 1 -and @(PetWindows).Count -eq 0 -and -not $p.HasExited) "windows=$(@(PetWindows).Count) alive=$(-not $p.HasExited)"
    }
    $p6 = @($p6)[-1]
    Check "S6b" "펫 0마리 상태에서 트레이 종료(106) 정상" ($p6.HasExited -and $p6.ExitCode -eq 0) "exit=$($p6.ExitCode)"
}
finally {
    CurMove 2400 1300
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    $results | Export-Csv (Join-Path $outDir "results.csv") -NoTypeInformation -Encoding UTF8
    $pass = @($results | Where-Object Pass).Count
    Write-Host ("결과: {0}/{1} 통과 → {2}" -f $pass, $results.Count, $outDir)
}
