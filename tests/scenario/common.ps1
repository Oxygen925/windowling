# 시나리오 공용 도우미 (P1~P5·UI 가 dot-source 한다). 부르기 전에 $Phase(로그 폴더 이름: p1·ui …)와 $Preset 을 정한다.
#   . (Join-Path $PSScriptRoot "common.ps1")
# 정하는 것: $root·$bin·$pet·$mockExe·$outDir·$logDir, 로그 대기(PetLines·Mark·SinceMark·All·WaitLog·Find-NewLog),
#            결과 기록(Check·Save-Results), 모의 SIWC 서버(Start-Mock·Mock·Stats·StatN)
$ErrorActionPreference = "Stop"
$env:DP_TEST_LANGUAGE = "ko"; $env:DP_TEST_NO_WELCOME = "1"  # 화면 문구 한국어 고정, 환영 창 끔 (UI 시험은 언어를 다시 지운다)
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bin = Join-Path $root "build\$Preset"
$pet = Join-Path $bin "desktop-pet.exe"
$mockExe = Join-Path $bin "mock-siwc.exe"
$outDir = Join-Path $root ("logs\$Phase\" + (Get-Date -Format "yyyyMMdd_HHmmss") + "_scenarios")
New-Item -ItemType Directory -Force $outDir | Out-Null

# ---- 로그 (개발 채널 데이터 폴더, 아직 없을 수 있음) ----
$logDir = Join-Path $env:LOCALAPPDATA "DesktopPet-Dev\logs"
New-Item -ItemType Directory -Force $logDir | Out-Null
$script:petLog = $null; $script:mark = 0; $script:allLogs = @()
function PetLines { @(Get-Content $script:petLog -Encoding UTF8) }
function Mark { $script:mark = (PetLines).Count }
function SinceMark($pattern) { @(PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match $pattern }) }
function All($pattern) { @(PetLines | Where-Object { $_ -match $pattern }) }
function WaitLog($pattern, $sec = 6) {
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        $hit = PetLines | Select-Object -Skip $script:mark | Where-Object { $_ -match $pattern } | Select-Object -First 1
        if ($hit) { return $hit }
        Start-Sleep -Milliseconds 100
    }
    return $null
}
function LogNames { @(Get-ChildItem $logDir -Filter *.log | Select-Object -ExpandProperty Name) }
# 앱을 띄운 뒤 새로 생긴 로그 파일을 찾아 $script:petLog 로 (before = 띄우기 전 LogNames). 최대 10초
function Find-NewLog($before) {
    for ($i = 0; $i -lt 50 -and -not $script:petLog; $i++) {
        Start-Sleep -Milliseconds 200
        $new = Get-ChildItem $logDir -Filter *.log | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($new) { $script:petLog = $new.FullName }
    }
    return $script:petLog
}

# ---- 결과 ----
$results = New-Object System.Collections.ArrayList
function Check($id, $name, $ok, $detail) {
    [void]$results.Add([pscustomobject]@{ Id = $id; Scenario = $name; Pass = [bool]$ok; Detail = "$detail" })
    Write-Host ("[{0}] {1} {2} — {3}" -f $id, $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail)
}
function Save-Results {
    $results | Export-Csv (Join-Path $outDir "results.csv") -NoTypeInformation -Encoding UTF8
    $pass = @($results | Where-Object Pass).Count
    Write-Host ("결과: {0}/{1} 통과 → {2}" -f $pass, $results.Count, $outDir)
}

# ---- 모의 SIWC 서버 (127.0.0.1) ----
function Start-Mock($tmpDir) {
    $portFile = Join-Path $tmpDir "port.txt"
    $script:mock = Start-Process $mockExe -ArgumentList @("--port-file=$portFile") -PassThru -WindowStyle Hidden
    for ($i = 0; $i -lt 100 -and -not (Test-Path $portFile); $i++) { Start-Sleep -Milliseconds 100 }
    $script:base = "http://127.0.0.1:" + (Get-Content $portFile).Trim()
}
function Mock($q) { $null = Invoke-RestMethod -UseBasicParsing "$base/control?$q" }
function Stats { Invoke-RestMethod -UseBasicParsing "$base/stats" }
function StatN($k) { $s = Stats; if ($s.PSObject.Properties[$k]) { [int]$s.$k } else { 0 } }
