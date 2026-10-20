# 예제 캐릭터 Windowling 스프라이트 시트 생성 (MIT). 앱 아이콘(tools/assets/app_icon.svg)의 창 + 세로 눈에 다리를 단 모양.
# 기본 출력: characters/windowling/render/sprite/atlas.png — 128x128 프레임 11개 가로 배열 (오른쪽 보기, 왼쪽은 앱이 뒤집는다)
#   0 idle, 1 깜빡임, 2 걷기 기본, 3 걷기 왼발 듦, 4 걷기 오른발 듦, 5 sit, 6 fall, 7 drag, 8 look, 9 talk(입 벌림), 10 talk(입 작게)
# 좌표는 100 단위 설계를 프레임 크기에 비례해 그린다. 몸통 기본 x 24~76, y 20~70.
# 사용: powershell -File tools\assets\gen_windowling_sprite.ps1 [-OutDir ...] [-Preview 미리보기.png]
param(
    [string]$OutDir = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "characters\windowling\render\sprite"),
    [string]$Preview = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$F = 128; $k = $F / 100.0
function C($hex) { [Drawing.ColorTranslator]::FromHtml($hex) }
function U($v) { [float]($v * $k) }
function Pt($x, $y) { New-Object Drawing.PointF (U $x), (U $y) }
function RoundRect($x, $y, $w, $h, $r) {
    $x = U $x; $y = U $y; $w = U $w; $h = U $h; $d = [Math]::Max(0.5, 2 * (U $r))
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90); $p.CloseFigure(); $p
}
# 아이콘과 같은 색. 외곽선은 밝은 배경에서도 보이게
$screenC = C "#262C36"; $lineC = C "#151920"; $whiteC = C "#E9EEF2"; $tealC = C "#86DACA"

function Line($g, $pts, $w, $col) {
    $p = New-Object Drawing.Pen $col, (U $w); $p.StartCap = 'Round'; $p.EndCap = 'Round'; $p.LineJoin = 'Round'
    $g.DrawLines($p, [Drawing.PointF[]]$pts); $p.Dispose()
}
function OLine($g, $pts, $w, $col) { Line $g $pts ($w + 3) $lineC; Line $g $pts $w $col }

# 자세: dy = 몸통 위아래, rot = 기울기(도), L/R = 발 위치(다리 길이 90% 적용 전), toe = 발끝, eyes/mouth = 표정
$LegScale = 0.9; $BASE = 3   # 다리 길이 비율, 전체를 아래로 (발이 프레임 바닥에 닿게)
$frames = @(
    @{ dy = 0;  L = @(38, 93); R = @(62, 93); eyes = "pill" },                 # 0 idle
    @{ dy = 0;  L = @(38, 93); R = @(62, 93); eyes = "closed" },               # 1 깜빡임
    @{ dy = 0;  L = @(36, 93); R = @(64, 93); eyes = "pill" },                 # 2 걷기 기본
    @{ dy = -3; L = @(46, 85); R = @(59, 93); eyes = "pill" },                 # 3 왼발 듦
    @{ dy = -3; L = @(37, 93); R = @(68, 85); eyes = "pill" },                 # 4 오른발 듦
    @{ dy = 10; L = @(27, 91); R = @(73, 91); eyes = "happy" },                # 5 sit
    @{ dy = -4; L = @(22, 80); R = @(78, 80); eyes = "round"; toe = $false },  # 6 fall
    @{ dy = -8; L = @(38, 92); R = @(61, 90); eyes = "squint"; rot = -10; toe = $false },  # 7 drag
    @{ dy = 0;  L = @(38, 93); R = @(62, 93); eyes = "up" },                   # 8 look
    @{ dy = 0;  L = @(38, 93); R = @(62, 93); eyes = "pill"; mouth = 5 },      # 9 talk
    @{ dy = 0;  L = @(38, 93); R = @(62, 93); eyes = "pill"; mouth = 2 }       # 10 talk
)

function Eyes($g, $kind, $cy) {
    $wb = New-Object Drawing.SolidBrush $whiteC
    foreach ($sgn in -1, 1) {
        $cx = 50 + $sgn * 13
        switch ($kind) {
            "pill"   { $g.FillPath($wb, (RoundRect ($cx - 4) ($cy - 8.5) 8 17 4)) }
            "up"     { $g.FillPath($wb, (RoundRect ($cx - 2) ($cy - 12) 8 17 4)) }   # 위·앞쪽을 봄
            "closed" { Line $g @((Pt ($cx - 4) ($cy + 2)), (Pt ($cx + 4) ($cy + 2))) 4.5 $whiteC }
            "happy"  { Line $g @((Pt ($cx - 5) ($cy + 2)), (Pt $cx ($cy - 4)), (Pt ($cx + 5) ($cy + 2))) 4.5 $whiteC }
            "squint" { $s = -$sgn; Line $g @((Pt ($cx - 4 * $s) ($cy - 5)), (Pt ($cx + 3 * $s) $cy), (Pt ($cx - 4 * $s) ($cy + 5))) 4.5 $whiteC }
            "round"  { $g.FillEllipse($wb, (U ($cx - 5.5)), (U ($cy - 5.5)), (U 11), (U 11)) }
        }
    }
    $wb.Dispose()
}

function DrawFrame($g, $ox, $p) {
    $st = $g.Save()
    $g.TranslateTransform($ox, 0)
    $g.TranslateTransform(0, (U $BASE))
    if ($p.rot) { $g.TranslateTransform((U 50), (U 20)); $g.RotateTransform($p.rot); $g.TranslateTransform(-(U 50), -(U 20)) }
    $top = 20 + $p.dy; $bot = 70 + $p.dy
    # 다리 (몸통 뒤). 엉덩이에서 발까지, 발끝은 오른쪽
    foreach ($leg in @(@(40, $p.L), @(60, $p.R))) {
        $hx = $leg[0]; $hy = $bot - 4; $f = $leg[1]
        $fx = $hx + ($f[0] - $hx) * $LegScale; $fy = $hy + ($f[1] - $hy) * $LegScale
        $pts = @((Pt $hx $hy), (Pt $fx $fy))
        if ($p.toe -ne $false) { $pts += Pt ($fx + 5 * $LegScale) $fy }
        OLine $g $pts 6 $tealC
    }
    # 몸통: 외곽선 → 어두운 화면 → 흰 테두리
    $g.FillPath((New-Object Drawing.SolidBrush $lineC), (RoundRect 19 ($top - 5) 62 60 10))
    $g.FillPath((New-Object Drawing.SolidBrush $screenC), (RoundRect 24 $top 52 50 6))
    $w = New-Object Drawing.Pen $whiteC, (U 7); $w.LineJoin = 'Round'
    $g.DrawPath($w, (RoundRect 24 $top 52 50 6)); $w.Dispose()
    # 화면 안 꺾쇠 (아이콘의 왼쪽 위·오른쪽 아래)
    Line $g @((Pt 30.5 ($top + 14)), (Pt 30.5 ($top + 7)), (Pt 38 ($top + 7))) 3.5 $tealC
    Line $g @((Pt 62 ($top + 43)), (Pt 69.5 ($top + 43)), (Pt 69.5 ($top + 36))) 3.5 $tealC
    Eyes $g $p.eyes (46 + $p.dy)
    if ($p.mouth) {
        $h = $p.mouth
        $g.FillPath((New-Object Drawing.SolidBrush $whiteC), (RoundRect 46 (59 + $p.dy - $h / 2) 8 $h ([Math]::Min(4, $h / 2))))
    }
    $g.Restore($st)
}

$N = $frames.Count
$bmp = New-Object Drawing.Bitmap ($F * $N), $F, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'
$g.Clear([Drawing.Color]::Transparent)
for ($i = 0; $i -lt $N; $i++) { DrawFrame $g ($i * $F) $frames[$i] }
$g.Dispose()

New-Item -ItemType Directory -Force $OutDir | Out-Null
$out = Join-Path $OutDir "atlas.png"
$bmp.Save($out, [Drawing.Imaging.ImageFormat]::Png)

# 미리보기: 밝은·어두운 배경에 모든 프레임
if ($Preview) {
    $pv = New-Object Drawing.Bitmap ($F * $N), ($F * 2)
    $pg = [Drawing.Graphics]::FromImage($pv)
    $pg.FillRectangle((New-Object Drawing.SolidBrush (C "#E6E6E6")), 0, 0, $F * $N, $F)
    $pg.FillRectangle((New-Object Drawing.SolidBrush (C "#202020")), 0, $F, $F * $N, $F)
    $pg.DrawImage($bmp, 0, 0, $F * $N, $F); $pg.DrawImage($bmp, 0, $F, $F * $N, $F)
    $pen = New-Object Drawing.Pen (C "#FF5A5A"), 1
    for ($i = 1; $i -lt $N; $i++) { $pg.DrawLine($pen, $i * $F, 0, $i * $F, 2 * $F) }
    $pg.Dispose(); $pv.Save($Preview); $pv.Dispose()
}
$bmp.Dispose()
Write-Output "wrote $out ($N frames, ${F}px)"
