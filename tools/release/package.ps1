# 배포 패키지 만들기 (P6-02). x64-dist 빌드 결과로 Portable ZIP·SHA-256·예제 캐릭터 zip·latest.json 을 dist\ 에 만든다.
# 사용: powershell -ExecutionPolicy Bypass -File tools\release\package.ps1 [-Repo 소유자/저장소] [-Draft]
#  - 먼저 tools\build.ps1 -Preset x64-dist 로 빌드한다.
#  - -Repo 가 있으면 latest.json(업데이트 확인용)을 만든다. 없으면 건너뛴다.
#  - -Draft: README 가 아직 없어도 만든다 (시험용). 릴리스에는 쓰지 않는다.
# zip 항목 이름은 '/' 로 쓴다 (Windows PowerShell 5.1 Compress-Archive 는 '\' 를 써서 다른 도구에서 깨질 수 있다).
param(
    [string]$Preset = "x64-dist",
    [string]$Repo = "",
    [switch]$Draft
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bin = Join-Path $root "build\$Preset"
$out = Join-Path $root "dist"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

# ---- 버전 (생성 헤더) ----
$verHeader = Join-Path $bin "generated\dp_version.h"
if (-not (Test-Path $verHeader)) { throw "빌드 결과가 없음: $verHeader (tools\build.ps1 -Preset $Preset 먼저)" }
$m = Select-String -Path $verHeader -Pattern '#define DP_VERSION_STRING "([0-9]+\.[0-9]+\.[0-9]+)"'
if (-not $m) { throw "버전을 읽지 못함" }
$version = $m.Matches[0].Groups[1].Value
$name = "DesktopPet-$version-win-x64"

# ---- 넣을 파일 ----
$files = [ordered]@{
    "DesktopPet/desktop-pet.exe"        = Join-Path $bin "desktop-pet.exe"
    "DesktopPet/pack-check.exe"         = Join-Path $bin "pack-check.exe"
    "DesktopPet/LICENSE"                = Join-Path $root "LICENSE"
    "DesktopPet/THIRD_PARTY_NOTICES.md" = Join-Path $root "THIRD_PARTY_NOTICES.md"
    "DesktopPet/README.md"              = Join-Path $root "README.md"
    "DesktopPet/README.en.md"           = Join-Path $root "README.en.md"
}
foreach ($f in Get-ChildItem (Join-Path $root "lang") -Filter *.toml) { $files["DesktopPet/lang/$($f.Name)"] = $f.FullName }
# 정적 링크한 라이브러리의 라이선스 원문 (vcpkg). 하나라도 없으면 실패
$share = Join-Path $bin "vcpkg_installed\x64-windows-static\share"
foreach ($pkg in @("tomlplusplus", "wil", "lua", "nlohmann-json", "cgltf")) {
    $c = Join-Path $share "$pkg\copyright"
    if (-not (Test-Path $c)) { throw "라이선스 원문 없음: $c" }
    $files["DesktopPet/licenses/$pkg.txt"] = $c
}
$missing = @($files.GetEnumerator() | Where-Object { -not (Test-Path $_.Value) } | ForEach-Object { $_.Key })
if ($missing.Count) {
    if ($Draft -and @($missing | Where-Object { $_ -notlike "DesktopPet/README*" }).Count -eq 0) {
        Write-Warning "README 없음 (-Draft): $($missing -join ', ')"
        foreach ($k in $missing) { $files.Remove($k) }
    } else {
        throw "없는 파일: $($missing -join ', ')"
    }
}

function New-Zip($zipPath, $entries) {
    if (Test-Path $zipPath) { Remove-Item $zipPath }
    $fs = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
    try {
        $za = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create, $false, [Text.Encoding]::UTF8)
        try {
            foreach ($e in $entries.GetEnumerator()) {
                $entry = $za.CreateEntry($e.Key, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = (Get-Item $e.Value).LastWriteTime
                $w = $entry.Open()
                try { $r = [IO.File]::OpenRead($e.Value); try { $r.CopyTo($w) } finally { $r.Dispose() } } finally { $w.Dispose() }
            }
        } finally { $za.Dispose() }
    } finally { $fs.Dispose() }
}
function Write-Sha($path) {
    $h = (Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$path.sha256", "$h  $(Split-Path -Leaf $path)`n", (New-Object Text.UTF8Encoding $false))
    return $h
}

New-Item -ItemType Directory -Force $out | Out-Null
$zip = Join-Path $out "$name.zip"
New-Zip $zip $files
$hash = Write-Sha $zip

# ---- 예제 캐릭터 (배포판에는 넣지 않고 따로, 캐릭터 관리 창으로 설치) ----
$samples = @()
foreach ($id in @("windowling")) {
    $dir = Join-Path $root "characters\$id"
    $entries = [ordered]@{}
    foreach ($f in Get-ChildItem $dir -Recurse -File) {
        $rel = $f.FullName.Substring($dir.Length + 1).Replace('\', '/')
        $entries["$id/$rel"] = $f.FullName
    }
    $sz = Join-Path $out "DesktopPet-character-$id-$version.zip"
    New-Zip $sz $entries
    $null = Write-Sha $sz
    $samples += (Split-Path -Leaf $sz)
}

# ---- latest.json (업데이트 확인) ----
if ($Repo) {
    if ($Repo -notmatch '^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$') { throw "-Repo 형식: 소유자/저장소" }
    $latest = [ordered]@{
        schema    = 1
        version   = $version
        published = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
        page      = "https://github.com/$Repo/releases/tag/v$version"
        zip       = "$name.zip"
        sha256    = $hash
    }
    [IO.File]::WriteAllText((Join-Path $out "latest.json"), ($latest | ConvertTo-Json) + "`n", (New-Object Text.UTF8Encoding $false))
}

# ---- 확인: zip 항목 목록 = 넣으려던 목록 ----
$za = [IO.Compression.ZipFile]::OpenRead($zip)
try { $names = @($za.Entries | ForEach-Object { $_.FullName }) } finally { $za.Dispose() }
$expected = @($files.Keys)
$diff = Compare-Object ($expected | Sort-Object) ($names | Sort-Object)
if ($diff) { throw "zip 항목이 다름: $($diff | Out-String)" }
if (@($names | Where-Object { $_ -match '\\' }).Count) { throw "zip 항목 이름에 '\\' 가 있음" }

Write-Host "버전 $version"
Write-Host "$zip ($([math]::Round((Get-Item $zip).Length / 1MB, 2)) MB, 항목 $($names.Count)개)"
Write-Host "SHA-256 $hash"
foreach ($s in $samples) { Write-Host "예제 캐릭터 $s" }
if ($Repo) { Write-Host "latest.json → https://github.com/$Repo/releases/tag/v$version" } else { Write-Host "latest.json 건너뜀 (-Repo 없음)" }
