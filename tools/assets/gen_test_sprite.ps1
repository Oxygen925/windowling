# 테스트 스프라이트 시트 생성 (CC0, D11). 색을 바꿔 여러 테스트 캐릭터를 만든다 (P2)
# 기본 출력: assets/test/pet_sheet.png — 64x64 프레임 8개 가로 배열
#   0~3: walk 사이클, 4: look(커서 근접), 5: drag, 6: sit, 7: fall
# 가장자리에 안티앨리어싱(부분 알파)을 넣어 premultiplied 합성·hit-test 임계값을 검증한다.
param(
    [string]$OutDir = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "assets\test"),
    [string]$FileName = "pet_sheet.png",
    # 색 (R,G,B). PowerShell 변수는 대소문자를 구분하지 않아 아래 브러시 변수와 이름이 겹치지 않게 한다
    [int[]]$BodyRgb = @(255, 170, 60),
    [int[]]$BellyRgb = @(255, 225, 170),
    [int[]]$LineRgb = @(90, 45, 20),
    [int[]]$FootRgb = @(200, 110, 40)
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$F = 64; $N = 8
$bmp = New-Object System.Drawing.Bitmap ($F * $N), $F, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)

$body    = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, $BodyRgb[0], $BodyRgb[1], $BodyRgb[2]))
$belly   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, $BellyRgb[0], $BellyRgb[1], $BellyRgb[2]))
$outline = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, $LineRgb[0], $LineRgb[1], $LineRgb[2])), 2.5
$eye     = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 30, 20, 20))
$shine   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
$foot    = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, $FootRgb[0], $FootRgb[1], $FootRgb[2]))
$glow    = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(110, 255, 200, 80))  # 반투명 영역(알파 경계 테스트)

for ($i = 0; $i -lt $N; $i++) {
    $ox = $i * $F
    $squash = if ($i -eq 5) { 6 } elseif ($i -eq 6) { 10 } else { 0 }
    $bob = if ($i -lt 4) { @(0, -2, 0, -2)[$i] } else { 0 }

    # 발 (walk 사이클에서 교차, sit 은 앞으로 뻗음, fall 은 벌림)
    $lf = if ($i -lt 4) { @(-3, 0, 3, 0)[$i] } elseif ($i -eq 7) { -6 } else { 0 }
    if ($i -eq 6) {
        $g.FillEllipse($foot, $ox + 10, 54, 16, 8)
        $g.FillEllipse($foot, $ox + 38, 54, 16, 8)
    } else {
        $g.FillEllipse($foot, $ox + 16 + $lf, 52, 14, 9)
        $g.FillEllipse($foot, $ox + 34 - $lf, 52, 14, 9)
    }

    # 반투명 후광 (look 프레임)
    if ($i -eq 4) { $g.FillEllipse($glow, $ox + 2, 2, 60, 58) }

    # 몸통
    $rect = New-Object System.Drawing.RectangleF ($ox + 8 - $squash / 2), (10 + $bob + $squash), (48 + $squash), (46 - $squash)
    $g.FillEllipse($body, $rect)
    $g.FillEllipse($belly, $ox + 20, 30 + $bob, 24, 20)
    $g.DrawEllipse($outline, $rect)

    # 귀
    $g.FillPolygon($body, [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF ($ox + 14), (18 + $bob)),
        (New-Object System.Drawing.PointF ($ox + 18), (2 + $bob)),
        (New-Object System.Drawing.PointF ($ox + 26), (13 + $bob))))
    $g.FillPolygon($body, [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF ($ox + 38), (13 + $bob)),
        (New-Object System.Drawing.PointF ($ox + 46), (2 + $bob)),
        (New-Object System.Drawing.PointF ($ox + 50), (18 + $bob))))

    # 눈 (look 프레임은 시선 이동, drag 프레임은 감은 눈)
    $ex = if ($i -eq 4) { 3 } else { 0 }
    if ($i -eq 5 -or $i -eq 6) {
        $g.DrawLine($outline, $ox + 20, 26 + $squash, $ox + 27, 26 + $squash)
        $g.DrawLine($outline, $ox + 37, 26 + $squash, $ox + 44, 26 + $squash)
    } elseif ($i -eq 7) {
        # 놀란 눈 (크게)
        $g.FillEllipse($shine, $ox + 18, 18, 11, 12); $g.DrawEllipse($outline, $ox + 18, 18, 11, 12)
        $g.FillEllipse($shine, $ox + 35, 18, 11, 12); $g.DrawEllipse($outline, $ox + 35, 18, 11, 12)
        $g.FillEllipse($eye, $ox + 22, 22, 4, 4); $g.FillEllipse($eye, $ox + 39, 22, 4, 4)
    } else {
        $g.FillEllipse($eye, $ox + 20 + $ex, 20 + $bob, 7, 9)
        $g.FillEllipse($eye, $ox + 37 + $ex, 20 + $bob, 7, 9)
        $g.FillEllipse($shine, $ox + 22 + $ex, 21 + $bob, 2.5, 2.5)
        $g.FillEllipse($shine, $ox + 39 + $ex, 21 + $bob, 2.5, 2.5)
    }
}
$g.Dispose()

New-Item -ItemType Directory -Force $OutDir | Out-Null
$out = Join-Path $OutDir $FileName
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "wrote $out"
