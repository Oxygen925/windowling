# MSVC 환경(vcvars64)을 잡은 뒤 CMake Preset으로 구성·빌드(·테스트)한다.
# 사용: powershell -ExecutionPolicy Bypass -File tools\build.ps1 [-Preset x64-debug] [-Clean] [-Configure] [-Test]
# VCPKG_ROOT 가 없으면 Visual Studio 동봉 vcpkg 를 사용한다 (manifest 모드, vcpkg.json 기준선으로 버전 고정).
#
# 빠른 경로 (조사 B1): 빌드할 때마다 vcvars64(약 6초)·configure(약 7초)를 다시 하지 않는다.
#  - vcvars 가 바꾸는 환경 변수를 build\.vcvars64.env 에 캐시한다 (VS 경로·vcvars64.bat 이 바뀌거나 7일이 지나면 갱신)
#  - build\<preset>\build.ninja 가 있으면 configure 를 건너뛴다. CMakeLists·vcpkg.json 이 바뀌면 ninja 가 스스로 다시 구성한다
#  - -Configure 로 강제 구성, -Clean 은 빌드 폴더를 지우고 처음부터
param(
    [string]$Preset = "x64-debug",
    [switch]$Clean,
    [switch]$Configure,
    [switch]$Test
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "MSVC x64 toolset not found" }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

# ---- vcvars 환경 (캐시) ----
$envCache = Join-Path $root "build\.vcvars64.env"
$fresh = (Test-Path $envCache) -and
         ((Get-Content $envCache -TotalCount 1) -eq "# $vsPath") -and
         ((Get-Item $envCache).LastWriteTime -gt (Get-Item $vcvars).LastWriteTime) -and
         ((Get-Item $envCache).LastWriteTime -gt (Get-Date).AddDays(-7))
if (-not $fresh) {
    function Read-Env($lines) {
        $d = @{}
        foreach ($l in $lines) { if ($l -match '^([^=]+)=(.*)$') { $d[$Matches[1]] = $Matches[2] } }
        $d
    }
    $before = Read-Env (cmd /c set)
    $after = Read-Env (cmd /c "`"$vcvars`" >nul && set")
    $out = New-Object System.Collections.Generic.List[string]
    $out.Add("# $vsPath")
    foreach ($k in $after.Keys) {
        if ($k -match '^VCPKG_') { continue }  # vcpkg 경로는 아래에서 따로 정한다
        if ($before.ContainsKey($k) -and $before[$k] -eq $after[$k]) { continue }
        $out.Add("$k=$($after[$k])")
    }
    New-Item -ItemType Directory -Force (Split-Path $envCache) | Out-Null
    [IO.File]::WriteAllLines($envCache, $out)
}
foreach ($l in (Get-Content $envCache | Select-Object -Skip 1)) {
    $i = $l.IndexOf('=')
    if ($i -gt 0) { [Environment]::SetEnvironmentVariable($l.Substring(0, $i), $l.Substring($i + 1), 'Process') }
}

# 콘솔 출력 코드 페이지를 UTF-8 로: cl 은 /showIncludes 줄("참고: 포함 파일:")을 콘솔 코드 페이지로 내보낸다.
# 949(예: Git Bash 에서 실행)이면 ninja 가 UTF-8 로 기록한 접두어와 맞지 않아 그 빌드에서 컴파일한 파일의
# 헤더 의존성이 0개가 된다 → 이후 헤더만 바꾸면 다시 컴파일되지 않음 (2026-10-20 발견)
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
& "$env:SystemRoot\System32\chcp.com" 65001 | Out-Null

if (-not $env:VCPKG_ROOT) {
    $bundled = Join-Path $vsPath "VC\vcpkg"
    if (-not (Test-Path (Join-Path $bundled "vcpkg.exe"))) { throw "VCPKG_ROOT 미설정, VS 동봉 vcpkg 도 없음" }
    $env:VCPKG_ROOT = $bundled
}

$bin = Join-Path $root "build\$Preset"
if ($Clean -and (Test-Path $bin)) { Remove-Item -Recurse -Force $bin }

Push-Location $root
try {
    if ($Clean -or $Configure -or -not (Test-Path (Join-Path $bin "build.ninja"))) {
        cmake --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }
    }
    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
    if ($Test) {
        ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "tests failed ($LASTEXITCODE)" }
    }
}
finally { Pop-Location }
