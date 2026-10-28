# UI 다듬기 시나리오 (UI 계획 §3). 임시 폴더의 --config 로 실행하며 사용자 설정·자격 증명은 건드리지 않는다.
# 모의 SIWC 서버(127.0.0.1)만 사용. 시작 프로그램 등록은 --test-hooks 로 시험 키(HKCU\Software\DesktopPet\TestRun)만 쓴다.
# 실행 중 설정 창·대화창이 열린다. PC를 만지지 않는다.
param([string]$Preset = "x64-release")
$Phase = "ui"
. (Join-Path $PSScriptRoot "common.ps1")
Remove-Item Env:DP_TEST_LANGUAGE -ErrorAction SilentlyContinue  # 이 시험은 설정 파일의 언어를 쓴다
$tmp = Join-Path $env:TEMP ("dp_ui_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force $tmp | Out-Null

Add-Type -Namespace UIS -Name N -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, System.IntPtr title);
[DllImport("user32.dll")] public static extern System.IntPtr GetDlgItem(System.IntPtr h, int id);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr SendMessageW(System.IntPtr h, uint m, System.IntPtr w, string l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr SendMessageW(System.IntPtr h, uint m, System.IntPtr w, System.Text.StringBuilder l);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool IsWindowEnabled(System.IntPtr h);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowW")] public static extern System.IntPtr FindWindowT(string cls, string title);
'@
$WM_COMMAND = 0x111; $WM_CLOSE = 0x10; $WM_SETTEXT = 0x0C; $BM_CLICK = 0xF5; $BM_GETCHECK = 0xF0
$CB_SETCURSEL = 0x14E; $LB_GETCURSEL = 0x188
$ID_EXIT = 106; $ID_CHAT = 110; $ID_OBSERVE_SETTINGS = 120; $ID_SETTINGS = 122
function App { [UIS.N]::FindWindowW("DesktopPetDev.App", [IntPtr]::Zero) }
function Settings { [UIS.N]::FindWindowW("DesktopPetDev.Settings", [IntPtr]::Zero) }
function Chat { [UIS.N]::FindWindowW("DesktopPetDev.Chat", [IntPtr]::Zero) }
function AppCommand($id) { $null = [UIS.N]::PostMessageW((App), $WM_COMMAND, [IntPtr]$id, [IntPtr]::Zero) }
function WaitWindow($fn, $sec = 5) {
    for ($i = 0; $i -lt $sec * 10; $i++) { $h = & $fn; if ($h -ne [IntPtr]::Zero) { return $h }; Start-Sleep -Milliseconds 100 }
    return [IntPtr]::Zero
}
function Text($h) {
    $n = [int][UIS.N]::SendMessageW($h, 0x0E, [IntPtr]::Zero, [string]$null)
    $sb = New-Object System.Text.StringBuilder ($n + 2); $null = [UIS.N]::SendMessageW($h, 0x0D, [IntPtr]($n + 1), $sb); $sb.ToString()
}
function Ctl($id) { [UIS.N]::GetDlgItem((Settings), $id) }
function SetText($id, $s) { $null = [UIS.N]::SendMessageW((Ctl $id), $WM_SETTEXT, [IntPtr]::Zero, $s) }
function Pick($id, $index) { $null = [UIS.N]::SendMessageW((Ctl $id), $CB_SETCURSEL, [IntPtr]$index, [string]$null) }
function Click($id) { $null = [UIS.N]::SendMessageW((Ctl $id), $BM_CLICK, [IntPtr]::Zero, [string]$null) }
function Checked($id) { [int][UIS.N]::SendMessageW((Ctl $id), $BM_GETCHECK, [IntPtr]::Zero, [string]$null) -eq 1 }
function IsWin($h) { [UIS.N]::IsWindowEnabled($h) }
function SetCText($id, $s) { $null = [UIS.N]::SendMessageW([UIS.N]::GetDlgItem([UIS.N]::FindWindowW("DesktopPetDev.Characters", [IntPtr]::Zero), $id), $WM_SETTEXT, [IntPtr]::Zero, $s) }
function Apply { $s = Settings; $null = [UIS.N]::PostMessageW($s, $WM_COMMAND, [IntPtr]502, [UIS.N]::GetDlgItem($s, 502)) }

# ---- 모의 서버 ----
Start-Mock $tmp

# ---- 설정 (임시): 사용자가 쓴 주석과 펫 목록이 있는 파일 ----
$cfgDir = Join-Path $tmp "cfg"; New-Item -ItemType Directory -Force $cfgDir | Out-Null
$cfg = Join-Path $cfgDir "config.toml"
[IO.File]::WriteAllText($cfg, "# my own comment`n[display]`nlanguage = `"ko`"`ntheme = `"light`"`nproximity_reaction = false`n" +
    "[physics]`nhz = 50`n[observe]`nmax_per_hour = 2`n[[pets]]`ncharacter = `"test-blue`"`ncount = 1`n")
$bak = "$cfg.bak"

# ---- 시작 프로그램: 시험 키만 쓴다. 실제 Run 키는 읽기만 해서 바뀌지 않았는지 본다 ----
$testKey = "HKCU:\Software\DesktopPet\TestRun"
$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$runName = "DesktopPet-Dev"  # 개발 빌드의 시작 프로그램 값 이름 (배포판은 "DesktopPet")
function RunValue($key) { try { (Get-ItemProperty -Path $key -Name $runName -ErrorAction Stop).$runName } catch { $null } }
Remove-Item $testKey -Recurse -ErrorAction SilentlyContinue
$realBefore = RunValue $runKey

# ---- 로그 ----

$script:allLogs = @()
function StartApp($config) {
    $before = LogNames
    $script:petLog = $null; $script:mark = 0
    $proc = Start-Process $pet -ArgumentList @("--config=$config", "--ai", "--ai-endpoint=$base", "--ai-open-url-file=$(Join-Path $tmp 'url.txt')",
        "--no-behavior", "--pets=1", "--character=test-blue", "--spawn=1500,150", "--duration=180", "--test-hooks") -PassThru
    $null = Find-NewLog $before
    $script:allLogs += $script:petLog
    return $proc
}
$p = StartApp $cfg
try {
    $null = WaitLog "\[ai\] enabled endpoint=loopback-test" 8

    # ---- S1: 설정 창 열기 (트레이 "설정…") ----
    AppCommand $ID_SETTINGS
    $s = WaitWindow { Settings }
    $title = if ($s -ne [IntPtr]::Zero) { Text $s } else { "" }
    Check "S1" "트레이 '설정…' → 설정 창 (일반·표시 영역)" ($s -ne [IntPtr]::Zero -and $title -match "설정" -and [int][UIS.N]::SendMessageW((Ctl 501), $LB_GETCURSEL, [IntPtr]::Zero, [string]$null) -eq 0) $title

    # ---- S2: 잘못된 값 → 저장 안 함, 그 영역으로 이동 ----
    Mark
    SetText 807 "500"   # 화면 관찰 · 시간당 최대 (1~60)
    Apply
    Start-Sleep -Milliseconds 800
    $status = Text (Ctl 505)
    $page = [int][UIS.N]::SendMessageW((Ctl 501), $LB_GETCURSEL, [IntPtr]::Zero, [string]$null)
    Check "S2" "범위 밖 값 → '값을 확인하세요', 화면 관찰 영역으로, 파일 그대로" ($status -match "값을 확인하세요" -and $page -eq 2 -and -not (Test-Path $bak) -and (SinceMark "\[settings\] applied").Count -eq 0) "$status page=$page"

    # ---- S3: 적용 → 파일 다시 쓰기 + .bak, 언어·테마 즉시 반영, 시작 프로그램(시험 키) ----
    Mark
    SetText 807 "4"
    Pick 602 2          # 언어: English
    Pick 604 2          # 테마: 어둡게
    Click 610           # Windows 시작할 때 실행
    Apply
    $applied = WaitLog "\[settings\] applied" 5
    Start-Sleep -Milliseconds 500
    $text = Get-Content $cfg -Raw -Encoding UTF8
    $bakText = if (Test-Path $bak) { Get-Content $bak -Raw -Encoding UTF8 } else { "" }
    Check "S3a" "적용 → 바뀐 항목만 로그 (값 없음)" ($applied -match "changed=display\.language,display\.theme,observe\.max_per_hour restart=0 startup=true") "$applied"
    Check "S3b" "설정 파일: 새 값 저장, 창이 다루지 않는 값([[pets]]·physics) 유지" ($text -match "language = ['`"]en['`"]" -and $text -match "theme = ['`"]dark['`"]" -and $text -match 'max_per_hour = 4' -and $text -match "character = ['`"]test-blue['`"]" -and $text -match 'hz = 50') ""
    Check "S3c" "이전 파일은 config.toml.bak (사용자 주석 포함), 새 파일엔 주석 없음" ($bakText -match "# my own comment" -and $text -notmatch "# my own comment") ""
    $s = Settings
    $title = Text $s; $status = Text (Ctl 505)
    Check "S3d" "언어 즉시 반영: 설정 창 제목·상태 문구가 영어" ($title -eq "Settings — Desktop Pet" -and $status -eq "Applied.") "$title | $status"
    AppCommand $ID_CHAT
    $c = WaitWindow { Chat }
    Start-Sleep -Milliseconds 300
    $chatTitle = if ($c -ne [IntPtr]::Zero) { Text $c } else { "" }
    Check "S3e" "언어 즉시 반영: 대화창 문구가 영어" ($chatTitle -match "Chat$") $chatTitle
    $testVal = RunValue $testKey
    Check "S3f" "시작 프로그램 켜기 → 시험 키에 실행 파일 경로, 실제 Run 키는 그대로" ($testVal -and $testVal -match "desktop-pet\.exe" -and (RunValue $runKey) -eq $realBefore) "$testVal"

    # ---- S4: 시작 프로그램 끄기 → 시험 키 값 삭제 ----
    Mark
    Click 610
    Apply
    $applied = WaitLog "\[settings\] applied" 5
    Check "S4" "시작 프로그램 끄기 → 시험 키 값 삭제 (다른 변경 없음)" ($applied -match "changed=- restart=0 startup=false" -and -not (RunValue $testKey)) "$applied"

    # ---- S5: 다시 시작이 필요한 항목 표시 (펫 크기) ----
    Mark
    $s = Settings
    $null = [UIS.N]::SendMessageW((Ctl 501), 0x186, [IntPtr]0, [string]$null)
    $null = [UIS.N]::SendMessageW($s, $WM_COMMAND, [IntPtr](501 + 65536), [string]$null)
    Pick 606 4          # 펫 크기: 1.5 (0.5, 0.75, 1, 1.25, 1.5 ...)
    Apply
    $applied = WaitLog "\[settings\] applied" 5
    $status = Text (Ctl 505)
    Check "S5" "펫 크기 변경 → 저장, '다시 시작해야' 안내" ($applied -match "changed=display\.scale restart=1" -and $status -match "after restart: Pet size$" -and (Get-Content $cfg -Raw) -match "scale = 1.5") "$status"

    # ---- S6: 관찰 설정 메뉴 → 설정 창의 화면 관찰 영역 ----
    $null = [UIS.N]::PostMessageW((Settings), $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    $closed = (Settings) -eq [IntPtr]::Zero
    AppCommand $ID_OBSERVE_SETTINGS
    $s = WaitWindow { Settings }
    $page = [int][UIS.N]::SendMessageW((Ctl 501), $LB_GETCURSEL, [IntPtr]::Zero, [string]$null)
    Check "S6" "닫기 뒤 트레이 '관찰 설정' → 설정 창이 화면 관찰 영역으로 열림" ($closed -and $s -ne [IntPtr]::Zero -and $page -eq 2 -and (Text (Ctl 807)) -eq "4") "page=$page"

    # ---- X: 종료 ----
    $t0 = Get-Date
    AppCommand $ID_EXIT
    $exited = $p.WaitForExit(8000)
    Check "X0" "정상 종료 (설정 창이 열린 채)" ($exited -and $p.ExitCode -eq 0) ("{0:N0}ms" -f ((Get-Date) - $t0).TotalMilliseconds)

    # ======== 캐릭터 관리 (UI-08): 새로 시작, 한국어 설정 ========
    $cfg2Dir = Join-Path $tmp "cfg2"; New-Item -ItemType Directory -Force $cfg2Dir | Out-Null
    $cfg2 = Join-Path $cfg2Dir "config.toml"
    [IO.File]::WriteAllText($cfg2, "[display]`nlanguage = `"ko`"`nproximity_reaction = false`n[[pets]]`ncharacter = `"test-orange`"`ncount = 1`n")
    $userChars = Join-Path $cfg2Dir "characters"
    $hookFile = Join-Path $cfg2Dir "test_install.txt"
    function Chars { [UIS.N]::FindWindowW("DesktopPetDev.Characters", [IntPtr]::Zero) }
    function CCtl($id) { [UIS.N]::GetDlgItem((Chars), $id) }
    function CClick($id) { $c = Chars; $null = [UIS.N]::PostMessageW($c, $WM_COMMAND, [IntPtr]$id, [UIS.N]::GetDlgItem($c, $id)) }
    function ListCount { [int][UIS.N]::SendMessageW((CCtl 1001), 0x18B, [IntPtr]::Zero, [string]$null) }
    function SelectChar($name) {  # 목록을 차례로 골라 이름 칸(1003)이 맞는 항목에서 멈춘다
        $n = ListCount
        for ($i = 0; $i -lt $n; $i++) {
            $null = [UIS.N]::SendMessageW((CCtl 1001), 0x186, [IntPtr]$i, [string]$null)
            $null = [UIS.N]::SendMessageW((Chars), $WM_COMMAND, [IntPtr](1001 + 65536), [string]$null)
            if ((Text (CCtl 1003)) -eq $name) { return $true }
        }
        return $false
    }
    function InstallHook($path) { [IO.File]::WriteAllText($hookFile, $path); AppCommand 9001 }
    # 확인 상자(MessageBox, 앱 안에서 모달) 에 예/아니요
    function AnswerBox($button, $sec = 5) {
        for ($i = 0; $i -lt $sec * 10; $i++) {
            $d = [UIS.N]::FindWindowT("#32770", "캐릭터 관리 — Desktop Pet")
            if ($d -ne [IntPtr]::Zero) { $null = [UIS.N]::PostMessageW($d, $WM_COMMAND, [IntPtr]$button, [IntPtr]::Zero); return $true }
            Start-Sleep -Milliseconds 100
        }
        return $false
    }
    $IDYES = 6; $IDNO = 7

    # 시험 패키지: 기본 제공 test-blue 를 복사해 id 만 바꾼다
    function MakePkg($dir, $id, $name) {
        Copy-Item (Join-Path $bin "characters\test-blue") $dir -Recurse
        $m = Join-Path $dir "manifest.toml"
        $t = [IO.File]::ReadAllText($m) -replace 'id = "test-blue"', "id = `"$id`"" -replace 'name = "[^"]*"', "name = `"$name`""
        [IO.File]::WriteAllText($m, $t)
    }
    $src = Join-Path $tmp "src"; New-Item -ItemType Directory -Force $src | Out-Null
    MakePkg (Join-Path $src "folder-pkg") "ui-test-pkg" "UI Test Pkg"
    MakePkg (Join-Path $src "zip-pkg") "ui-test-zip" "UI Test Zip"
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $goodZip = Join-Path $src "good.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $src "zip-pkg"), $goodZip, [IO.Compression.CompressionLevel]::Optimal, $true)
    $evilZip = Join-Path $src "evil.zip"
    $fs = [IO.File]::Open($evilZip, [IO.FileMode]::Create)
    $za = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
    foreach ($n in @("pkg/manifest.toml", "../evil.txt")) { $w = New-Object IO.StreamWriter($za.CreateEntry($n).Open()); $w.Write("x"); $w.Dispose() }
    $za.Dispose(); $fs.Dispose()
    $fakeZip = Join-Path $src "fake.zip"; [IO.File]::WriteAllText($fakeZip, "not a zip")
    $badPkg = Join-Path $src "bad-pkg"; MakePkg $badPkg "Bad_ID" "Bad"
    $linkPkg = Join-Path $src "link-pkg"; MakePkg $linkPkg "ui-test-link" "Link"
    $linkTarget = Join-Path $tmp "link-target"; New-Item -ItemType Directory -Force $linkTarget | Out-Null
    $null = cmd /c mklink /J "$linkPkg\linked" "$linkTarget"

    $p2 = StartApp $cfg2
    $null = WaitLog "\[library\] 캐릭터" 8
    AppCommand 121
    $cw = WaitWindow { Chars }
    Start-Sleep -Milliseconds 500
    $n0 = if ($cw -ne [IntPtr]::Zero) { ListCount } else { 0 }
    Check "C1" "트레이 '캐릭터 관리…' → 창, 기본 제공 4개" ($cw -ne [IntPtr]::Zero -and $n0 -eq 4 -and -not (IsWin (CCtl 1009))) "items=$n0"

    Mark; InstallHook (Join-Path $src "folder-pkg")
    $inst = WaitLog "\[characters\] installed id=ui-test-pkg from=folder overwrite=False" 8
    Start-Sleep -Milliseconds 300
    Check "C2" "폴더 설치 → 사용자 폴더에 id 이름으로, 목록 5개" ($inst -and (Test-Path (Join-Path $userChars "ui-test-pkg\manifest.toml")) -and (ListCount) -eq 5) "$inst"

    Mark; InstallHook $goodZip
    $inst = WaitLog "\[characters\] installed id=ui-test-zip from=zip overwrite=False" 10
    Check "C3" ".zip 설치 (zip 검사 → tar.exe → 트리 검사 → 패키지 검사)" ($inst -and (Test-Path (Join-Path $userChars "ui-test-zip\manifest.toml"))) "$inst"

    Mark; InstallHook (Join-Path $src "folder-pkg")
    $asked = AnswerBox $IDNO
    $cancel = WaitLog "\[characters\] install cancelled id=ui-test-pkg" 5
    Mark; InstallHook (Join-Path $src "folder-pkg")
    $asked2 = AnswerBox $IDYES
    $inst = WaitLog "\[characters\] installed id=ui-test-pkg from=folder overwrite=True" 8
    Check "C4" "같은 id 다시 설치 → 확인: 아니요는 취소, 예는 바꿈(기존은 시험이라 바로 삭제)" ($asked -and $cancel -and $asked2 -and $inst) "$inst"

    Mark; InstallHook $evilZip
    $rej = WaitLog "\[characters\] install rejected from=zip" 5
    Check "C5" "'..' 항목이 든 zip → 풀기 전에 거부, 밖에 파일 없음" ($rej -match "상위 폴더" -and -not (Test-Path (Join-Path $cfg2Dir "evil.txt")) -and -not (Test-Path (Join-Path $tmp "evil.txt"))) "$rej"

    Mark; InstallHook $fakeZip
    $rej = WaitLog "\[characters\] install rejected from=zip" 5
    Check "C6" "zip 이 아닌 파일 → 거부 (tar 로 넘기지 않음)" ($rej -match "서명") "$rej"

    Mark; InstallHook $badPkg
    $rej = WaitLog "\[characters\] install rejected from=folder" 5
    Check "C7" "검사에 실패하는 패키지(잘못된 id) → 거부, 설치 안 됨" ($rej -match "id" -and -not (Test-Path (Join-Path $userChars "Bad_ID"))) "$rej"

    Mark; InstallHook $linkPkg
    $rej = WaitLog "\[characters\] install rejected from=folder" 5
    Check "C8" "정션(재분석 지점)이 든 폴더 → 거부" ($rej -match "재분석" -and -not (Test-Path (Join-Path $userChars "ui-test-link"))) "$rej"

    # 사용자 폴더에 깨진 패키지를 직접 두고 '다시 읽기' → 거부 목록과 이유
    Copy-Item $badPkg (Join-Path $userChars "broken-pkg") -Recurse
    Mark; CClick 1012
    $null = WaitLog "\[library\] 캐릭터 \d+개 로드, 1개 거부" 8
    Start-Sleep -Milliseconds 500
    $found = SelectChar "broken-pkg"
    $info = Text (CCtl 1004)
    Check "C9" "거부된 패키지가 목록에 이유와 함께 (삭제 가능)" ($found -and $info -match "검사를 통과하지 못해" -and $info -match "id" -and (IsWin (CCtl 1009))) ""

    # 마릿수: ui-test-pkg 2마리 → 저장·다시 띄우기
    $null = SelectChar "UI Test Pkg"
    SetCText 1006 "2"
    Mark; CClick 1008
    $saved = WaitLog "\[characters\] counts saved total=3 kinds=2" 8
    Start-Sleep -Milliseconds 500
    $spawned = (SinceMark "spawn character=ui-test-pkg").Count
    $cfgText = Get-Content $cfg2 -Raw -Encoding UTF8
    Check "C10" "띄울 수 저장 → [[pets]] 에 기록, 그 목록으로 다시 띄움" ($saved -and $spawned -eq 2 -and $cfgText -match "ui-test-pkg" -and $cfgText -match "test-orange") "spawned=$spawned"

    # 삭제: 펫 제거 + [[pets]] 에서 빼기 + 폴더 삭제(시험이라 휴지통 대신 바로)
    $null = SelectChar "UI Test Pkg"
    Mark; CClick 1009
    $asked = AnswerBox $IDYES
    $del = WaitLog "\[characters\] deleted id=ui-test-pkg recycle=false" 8
    Start-Sleep -Milliseconds 500
    $cfgText = Get-Content $cfg2 -Raw -Encoding UTF8
    Check "C11" "삭제 → 확인 뒤 폴더·펫·시작 목록에서 사라짐" ($asked -and $del -and -not (Test-Path (Join-Path $userChars "ui-test-pkg")) -and $cfgText -notmatch "ui-test-pkg" -and (SinceMark "remove character=ui-test-pkg").Count -eq 2) "$del"

    $null = SelectChar "Test Blue"
    Check "C12" "기본 제공 캐릭터는 삭제 버튼이 꺼짐" (-not (IsWin (CCtl 1009))) ""

    $t0 = Get-Date
    AppCommand $ID_EXIT
    $exited = $p2.WaitForExit(8000)
    Check "X1" "정상 종료 (캐릭터 관리 창이 열린 채)" ($exited -and $p2.ExitCode -eq 0 -and -not (Test-Path (Join-Path $cfg2Dir "install_tmp\*"))) ("{0:N0}ms" -f ((Get-Date) - $t0).TotalMilliseconds)

    # ======== 환영 창 (UI-09): 첫 실행에만, 포커스를 빼앗지 않음 ========
    $cfg3Dir = Join-Path $tmp "cfg3"; New-Item -ItemType Directory -Force $cfg3Dir | Out-Null
    $cfg3 = Join-Path $cfg3Dir "config.toml"
    [IO.File]::WriteAllText($cfg3, "[display]`nlanguage = `"ko`"`nproximity_reaction = false`n")
    function Welcome { [UIS.N]::FindWindowW("DesktopPetDev.Welcome", [IntPtr]::Zero) }
    function WClick($id) { $w = Welcome; $null = [UIS.N]::PostMessageW($w, $WM_COMMAND, [IntPtr]$id, [UIS.N]::GetDlgItem($w, $id)) }
    Remove-Item Env:DP_TEST_NO_WELCOME
    try {
        $p3 = StartApp $cfg3
        $first = WaitLog "\[ui\] welcome first_run" 8
        $w = WaitWindow { Welcome }
        Start-Sleep -Milliseconds 300
        $state = if (Test-Path (Join-Path $cfg3Dir "ui_state.json")) { Get-Content (Join-Path $cfg3Dir "ui_state.json") -Raw | ConvertFrom-Json } else { $null }
        Check "W1" "첫 실행 → 환영 창 (전경을 빼앗지 않음), ui_state.json 기록" ($first -and $w -ne [IntPtr]::Zero -and [UIS.N]::GetForegroundWindow() -ne $w -and $state.welcome_shown -eq $true) ""
        WClick 1107
        $s = WaitWindow { Settings }
        Check "W2" "환영 창 '설정…' → 설정 창" ($s -ne [IntPtr]::Zero) ""
        $null = [UIS.N]::PostMessageW($s, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
        WClick 1108
        Start-Sleep -Milliseconds 500
        $closed = (Welcome) -eq [IntPtr]::Zero
        AppCommand 123
        $again = WaitWindow { Welcome }
        Check "W3" "'시작하기' 로 닫힘, 트레이 '도움말' 로 다시 열림" ($closed -and $again -ne [IntPtr]::Zero) ""
        AppCommand $ID_EXIT
        $null = $p3.WaitForExit(8000)
        $p3 = StartApp $cfg3
        $null = WaitLog "\[library\] 캐릭터" 8
        Start-Sleep -Seconds 2
        Check "W4" "다시 실행하면 환영 창 없음" ((Welcome) -eq [IntPtr]::Zero -and (SinceMark "\[ui\] welcome first_run").Count -eq 0) ""
        AppCommand $ID_EXIT
        $exited = $p3.WaitForExit(8000)
        Check "X2" "정상 종료" ($exited -and $p3.ExitCode -eq 0) ""
    } finally {
        $env:DP_TEST_NO_WELCOME = "1"
        if ($p3 -and -not $p3.HasExited) { Stop-Process -Id $p3.Id -Force }
    }

    # ======== 업데이트 확인 (P6-03): 모의 서버의 latest.json, 알림만 ========
    function UpdateRun($name, $json, $path = "latest.json", $cfgExtra = "", $cfgDir = $null) {
        if (-not $cfgDir) { $cfgDir = Join-Path $tmp "upd_$name"; New-Item -ItemType Directory -Force $cfgDir | Out-Null }
        $c = Join-Path $cfgDir "config.toml"
        [IO.File]::WriteAllText($c, "[display]`nlanguage = `"ko`"`nproximity_reaction = false`n$cfgExtra")
        if ($json) { $null = Invoke-RestMethod -UseBasicParsing ("$base/control?latest=" + [uri]::EscapeDataString($json)) }
        $before = LogNames
        $script:petLog = $null; $script:mark = 0
        $pp = Start-Process $pet -ArgumentList @("--config=$c", "--no-behavior", "--pets=1", "--character=test-blue", "--spawn=1500,150",
            "--duration=60", "--test-hooks", "--update-url=$base/$path", "--update-repo=example/desktop-pet", "--update-delay-sec=1") -PassThru
        $null = Find-NewLog $before
        $script:allLogs += $script:petLog
        return [pscustomobject]@{ P = $pp; Dir = $cfgDir }
    }
    function StopRun($r) { $null = [UIS.N]::PostMessageW((App), $WM_COMMAND, [IntPtr]$ID_EXIT, [IntPtr]::Zero); $null = $r.P.WaitForExit(8000) }
    function LatestCount { [int](Invoke-RestMethod -UseBasicParsing "$base/stats").latest }
    $hash = "a" * 64
    function LatestJson($ver, $page = "https://github.com/example/desktop-pet/releases/tag/v$ver") {
        '{"schema":1,"version":"' + $ver + '","published":"2026-10-20T00:00:00Z","page":"' + $page + '","zip":"DesktopPet-' + $ver + '-win-x64.zip","sha256":"' + $hash + '"}'
    }

    # 현재 버전 (CMakeLists.txt project VERSION)
    $cur = [regex]::Match((Get-Content -Raw (Join-Path $PSScriptRoot "..\..\CMakeLists.txt")), "project\(desktop_pet VERSION ([0-9.]+)").Groups[1].Value
    $curRe = [regex]::Escape($cur)
    $r = UpdateRun "newer" (LatestJson "0.9.0")
    $ok = WaitLog "\[update\] check ok latest=0\.9\.0 current=$curRe newer=true" 8
    $nt = WaitLog "\[update\] notified version=0\.9\.0" 3
    AppCommand 124
    $op = WaitLog "\[update\] open page \(test: not opened\)" 3
    Check "UP1" "새 버전 → 트레이 알림 1번·메뉴로 릴리스 페이지 (시험은 열지 않음)" ($ok -and $nt -and $op) "$ok"
    StopRun $r
    $n0 = LatestCount
    $r = UpdateRun "newer2" $null "latest.json" "" $r.Dir
    Start-Sleep -Seconds 3
    Check "UP2" "하루 1번: 다시 시작해도 24시간 안에는 확인하지 않음" ((LatestCount) -eq $n0 -and (SinceMark "\[update\] check start").Count -eq 0) "requests=$((LatestCount) - $n0)"
    StopRun $r

    $r = UpdateRun "same" (LatestJson $cur)
    $ok = WaitLog "\[update\] check ok latest=$curRe current=$curRe newer=false" 8
    Check "UP3" "같은 버전 → 알림 없음" ($ok -and (SinceMark "\[update\] notified").Count -eq 0) "$ok"
    StopRun $r

    $r = UpdateRun "evil" (LatestJson "0.9.0" "https://evil.example/download")
    $f = WaitLog "\[update\] check failed code=invalid" 8
    Check "UP4" "릴리스 페이지가 아닌 주소 → 무시 (알림 없음)" ($f -and (SinceMark "\[update\] notified").Count -eq 0) "$f"
    StopRun $r

    $r = UpdateRun "redir" (LatestJson "0.9.1") "redirect/latest.json"
    $ok = WaitLog "\[update\] check ok latest=0\.9\.1" 8
    Check "UP5" "허용된 리디렉션(같은 origin) 은 따라감" ($ok) "$ok"
    StopRun $r

    $r = UpdateRun "redirevil" (LatestJson "0.9.1") "redirect-evil/latest.json"
    $f = WaitLog "\[update\] check failed code=redirect_denied" 8
    Check "UP6" "다른 곳으로 리디렉션 → 따라가지 않음" ($f) "$f"
    StopRun $r

    $n0 = LatestCount
    $r = UpdateRun "off" (LatestJson "0.9.0") "latest.json" "[update]`ncheck = false`n"
    Start-Sleep -Seconds 3
    Check "UP7" "설정에서 끄면 요청 없음" ((LatestCount) -eq $n0 -and (SinceMark "\[update\] check start").Count -eq 0) "requests=$((LatestCount) - $n0)"
    StopRun $r

    # ======== 모든 데이터 지우기 (P6-04): 로그인 상태에서 → 로그아웃(토큰 폐기)·시작 프로그램 해제·앱 파일 삭제·종료 ========
    $wDir = Join-Path $tmp "wipe"; New-Item -ItemType Directory -Force $wDir | Out-Null
    $wCfg = Join-Path $wDir "config.toml"
    [IO.File]::WriteAllText($wCfg, "[display]`nlanguage = `"ko`"`nproximity_reaction = false`n")
    $keep = Join-Path $wDir "my-notes.txt"; [IO.File]::WriteAllText($keep, "user file")  # 앱이 만들지 않은 파일은 남아야 한다
    $wUrl = Join-Path $tmp "wipe_url.txt"
    New-Item -Path "HKCU:\Software\DesktopPet\TestRun" -Force | Out-Null
    New-ItemProperty -Path "HKCU:\Software\DesktopPet\TestRun" -Name $runName -Value '"x"' -Force | Out-Null
    $before = LogNames
    $script:petLog = $null; $script:mark = 0
    $wp = Start-Process $pet -ArgumentList @("--config=$wCfg", "--ai", "--ai-endpoint=$base", "--ai-open-url-file=$wUrl",
        "--no-behavior", "--pets=1", "--character=test-blue", "--spawn=1500,150", "--duration=90", "--test-hooks") -PassThru
    $null = Find-NewLog $before
    $script:allLogs += $script:petLog
    $null = Invoke-RestMethod -UseBasicParsing "$base/control?reset=1"
    $null = WaitLog "\[ai\] enabled endpoint=loopback-test" 8
    AppCommand $ID_CHAT
    $c = WaitWindow { Chat }
    $null = [UIS.N]::PostMessageW($c, $WM_COMMAND, [IntPtr]211, [UIS.N]::GetDlgItem($c, 211))
    for ($i = 0; $i -lt 50 -and -not (Test-Path $wUrl); $i++) { Start-Sleep -Milliseconds 100 }
    try { $null = Invoke-WebRequest -UseBasicParsing ((Get-Content $wUrl -Raw).Trim()) -TimeoutSec 10 } catch { }
    $si = WaitLog "\[ai\] ui signin ok=true" 8
    $authed = Test-Path (Join-Path $wDir "auth")
    $rev0 = [int](Invoke-RestMethod -UseBasicParsing "$base/stats").revoke
    AppCommand $ID_SETTINGS
    $s = WaitWindow { Settings }
    $null = [UIS.N]::PostMessageW($s, $WM_COMMAND, [IntPtr]612, [UIS.N]::GetDlgItem($s, 612))
    $asked = $false
    for ($i = 0; $i -lt 50; $i++) { $d = [UIS.N]::FindWindowT("#32770", "설정 — Desktop Pet"); if ($d -ne [IntPtr]::Zero) { $null = [UIS.N]::PostMessageW($d, $WM_COMMAND, [IntPtr]6, [IntPtr]::Zero); $asked = $true; break }; Start-Sleep -Milliseconds 100 }
    $exited = $wp.WaitForExit(15000)
    $rev1 = [int](Invoke-RestMethod -UseBasicParsing "$base/stats").revoke
    $left = @(Get-ChildItem $wDir -Force -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
    $runVal = try { (Get-ItemProperty -Path "HKCU:\Software\DesktopPet\TestRun" -Name $runName -ErrorAction Stop).$runName } catch { $null }
    $wLog = @(Get-Content $script:petLog -Encoding UTF8)
    Check "WP1" "모든 데이터 지우기: 확인 → 로그아웃(토큰 폐기 1회) → 시작 프로그램 해제 → 앱 파일 삭제 → 종료" ($si -and $authed -and $asked -and $exited -and $wp.ExitCode -eq 0 -and $rev1 -eq $rev0 + 1 -and -not $runVal -and ($wLog -match "\[wipe\] remove app data default_dir=False").Count -eq 1) "revoke=$rev0→$rev1 exit=$($wp.ExitCode)"
    Check "WP2" "앱이 만든 것만 지움 (사용자 파일은 남김)" (($left -join ",") -eq "my-notes.txt") ("left=" + ($left -join ","))
    Check "WP3" "기본 데이터 폴더가 아니면 로그 폴더는 건드리지 않음" ((Test-Path $logDir) -and (Test-Path $script:petLog)) ""
} finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    if ($p2 -and -not $p2.HasExited) { Stop-Process -Id $p2.Id -Force }
    if ($linkPkg -and (Test-Path "$linkPkg\linked")) { cmd /c rmdir "$linkPkg\linked" | Out-Null }  # 정션만 지운다 (대상은 따라가지 않음)
    Stop-Process -Id $mock.Id -Force -ErrorAction SilentlyContinue
    Remove-Item "HKCU:\Software\DesktopPet\TestRun" -Recurse -ErrorAction SilentlyContinue
    $parent = Get-Item "HKCU:\Software\DesktopPet" -ErrorAction SilentlyContinue  # 시험이 만든 빈 키면 지운다
    if ($parent -and $parent.SubKeyCount -eq 0 -and $parent.ValueCount -eq 0) { Remove-Item "HKCU:\Software\DesktopPet" -ErrorAction SilentlyContinue }
    $i = 0; foreach ($l in $script:allLogs) { if ($l) { Copy-Item $l (Join-Path $outDir ("desktop-pet_{0}.log" -f $i++)) } }
    Copy-Item $cfg, $bak (Join-Path $outDir ".") -ErrorAction SilentlyContinue
}
Save-Results
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
