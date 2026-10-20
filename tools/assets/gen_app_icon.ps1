# 앱 아이콘 생성. 디자인: 어두운 둥근 사각형 위 "창(=얼굴)" + 세로 눈 두 개, 청록 꺾쇠 두 개 (tools/assets/app_icon.svg 와 같음).
# 출처: AI(ChatGPT)로 만든 SVG 를 바탕으로 프로젝트에서 고침 (눈 추가·선 굵기·중앙 정렬·배경 대비·작은 크기 전용). MIT.
# 32px 이상은 100 단위 설계를 비례로, 24px 이하는 16칸 격자에 픽셀로 그린다 (꺾쇠 없이 창과 눈만, 번지지 않게).
# 사용: powershell -File tools\assets\gen_app_icon.ps1 [-Out apps\desktop-pet\app.ico] [-Preview 미리보기.png]
param(
    [string]$Out = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "apps\desktop-pet\app.ico"),
    [string]$Preview = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function C($hex) { [Drawing.ColorTranslator]::FromHtml($hex) }
function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath; $d = [Math]::Max(0.5, 2 * $r)
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90); $p.CloseFigure(); $p
}
function Pt($x, $y) { New-Object Drawing.PointF ([float]$x), ([float]$y) }
$bgC = C "#262C36"; $edgeC = C "#3C4554"; $whiteC = C "#E9EEF2"; $tealC = C "#86DACA"

# 24px 이하: 16칸 격자 (f = s/16)
function DrawSmall([int]$s) {
    $b = New-Object Drawing.Bitmap $s, $s
    $g = [Drawing.Graphics]::FromImage($b); $g.SmoothingMode = 'AntiAlias'
    $f = $s / 16.0
    $g.FillPath((New-Object Drawing.SolidBrush $bgC), (RoundRect 0 0 $s $s (3.5 * $f)))
    $g.SmoothingMode = 'None'
    $wb = New-Object Drawing.SolidBrush $whiteC
    function Px($x, $y, $w, $h) { $g.FillRectangle($wb, [int][Math]::Round($x * $f), [int][Math]::Round($y * $f), [int][Math]::Max(1, [Math]::Round($w * $f)), [int][Math]::Max(1, [Math]::Round($h * $f))) }
    Px 2 4 12 2; Px 2 12 12 2; Px 2 4 2 10; Px 12 4 2 10   # 창 테두리
    Px 5 7 2 3; Px 9 7 2 3                                 # 눈
    $g.Dispose(); $b
}

# 32px 이상: 100 단위 설계
function Draw([int]$s) {
    if ($s -le 24) { return DrawSmall $s }
    $b = New-Object Drawing.Bitmap $s, $s
    $g = [Drawing.Graphics]::FromImage($b); $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'
    $k = $s / 100.0
    $g.FillPath((New-Object Drawing.SolidBrush $bgC), (RoundRect 0 0 $s $s (22 * $k)))
    $g.DrawPath((New-Object Drawing.Pen $edgeC, ([Math]::Max(1.0, 2 * $k))), (RoundRect (1 * $k) (1 * $k) ($s - 2 * $k) ($s - 2 * $k) (21 * $k)))
    $sw = 7 * $k
    $t = New-Object Drawing.Pen $tealC, $sw; $t.StartCap = 'Round'; $t.EndCap = 'Round'; $t.LineJoin = 'Round'
    $g.DrawLines($t, [Drawing.PointF[]]@((Pt (16 * $k) (40 * $k)), (Pt (16 * $k) (16 * $k)), (Pt (52 * $k) (16 * $k))))
    $g.DrawLines($t, [Drawing.PointF[]]@((Pt (48 * $k) (84 * $k)), (Pt (84 * $k) (84 * $k)), (Pt (84 * $k) (60 * $k))))
    $w = New-Object Drawing.Pen $whiteC, $sw; $w.LineJoin = 'Round'
    $g.DrawPath($w, (RoundRect (28 * $k) (30 * $k) (44 * $k) (42 * $k) (5 * $k)))
    $eb = New-Object Drawing.SolidBrush $whiteC
    $eyeW = 8 * $k; $eyeH = 17 * $k
    foreach ($sgn in -1, 1) {
        $ex = 50 * $k + $sgn * (9 * $k + $eyeW / 2) - $eyeW / 2
        $g.FillPath($eb, (RoundRect $ex (52 * $k - $eyeH / 2) $eyeW $eyeH (4 * $k)))
    }
    $g.Dispose(); $b
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$pngs = @(); $bitmaps = @{}
foreach ($s in $sizes) {
    $bmp = Draw $s
    $ms = New-Object IO.MemoryStream
    $bmp.Save($ms, [Drawing.Imaging.ImageFormat]::Png)
    $pngs += , $ms.ToArray()
    $bitmaps[$s] = $bmp
}
if ($Preview) {
    $pv = New-Object Drawing.Bitmap 700, 290
    $g = [Drawing.Graphics]::FromImage($pv); $g.InterpolationMode = 'NearestNeighbor'
    $g.FillRectangle((New-Object Drawing.SolidBrush (C "#F3F3F3")), 0, 0, 350, 290)
    $g.FillRectangle((New-Object Drawing.SolidBrush (C "#202020")), 350, 0, 350, 290)
    foreach ($ox in 0, 350) {
        $g.DrawImage($bitmaps[256], $ox + 10, 10, 256, 256)
        $g.DrawImage($bitmaps[32], $ox + 280, 10, 32, 32); $g.DrawImage($bitmaps[16], $ox + 320, 18, 16, 16)
        $g.DrawImage($bitmaps[16], $ox + 276, 60, 64, 64); $g.DrawImage($bitmaps[32], $ox + 276, 140, 64, 64)
    }
    $g.Dispose(); $pv.Save($Preview); $pv.Dispose()
}
foreach ($b in $bitmaps.Values) { $b.Dispose() }

# ICO: ICONDIR + ICONDIRENTRY(×n) + PNG 데이터
$fs = [IO.File]::Create($Out)
$w = New-Object IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $w.Write([byte]($(if ($s -ge 256) { 0 } else { $s }))); $w.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $w.Write([byte]0); $w.Write([byte]0); $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
$w.Dispose()
Write-Host "아이콘 → $Out ($($sizes -join ', ')px)"
