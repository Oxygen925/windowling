# 저장소 텍스트 표준 검사 (CI hygiene, 로컬에서도 실행 가능)
#  - 추적되는 텍스트 파일: LF (BAT/CMD 는 CRLF), UTF-8
#  - PowerShell 스크립트: UTF-8 BOM
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location $root
try {
    $bad = @()
    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)  # 잘못된 바이트에서 예외
    foreach ($line in (git ls-files --eol)) {
        $parts = $line -split "\s+", 4
        if ($parts[0] -eq "i/-text") { continue }  # 바이너리
        $path = ($line -split "`t", 2)[1]
        $bytes = [IO.File]::ReadAllBytes((Join-Path $root $path))
        $crlf = ([Text.Encoding]::ASCII.GetString($bytes)).Contains("`r`n")
        $wantCrlf = $path -match '\.(bat|cmd)$'
        if ($crlf -ne $wantCrlf -and -not ($wantCrlf -and $bytes.Length -eq 0)) {
            $bad += "$path : 줄 끝 $(if ($wantCrlf) { 'CRLF 필요' } else { 'LF 필요 (CRLF 발견)' })"
        }
        $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
        if ($path -match '\.ps1$' -and -not $hasBom) { $bad += "$path : PowerShell 스크립트는 UTF-8 BOM 필요" }
        try { $null = $utf8.GetString($bytes) } catch { $bad += "$path : UTF-8 아님" }
    }
    if ($bad.Count) {
        $bad | ForEach-Object { Write-Host $_ }
        throw "텍스트 표준 위반 $($bad.Count)건"
    }
    Write-Host "텍스트 표준 검사 통과"
}
finally { Pop-Location }
