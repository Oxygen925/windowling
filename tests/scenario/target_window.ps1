# 시나리오 테스트용 대상 창 (별도 프로세스). 테두리 없는 창이라 화면 사각형 = 창 사각형.
param(
    [Parameter(Mandatory)][string]$EventLog,
    [int]$X = 600, [int]$Y = 500, [int]$W = 900, [int]$H = 400,
    [string]$Title = "P1 target",
    [int]$Argb = -13421705
)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace TW -Name W -MemberDefinition '[DllImport("kernel32.dll")] public static extern System.IntPtr GetConsoleWindow(); [DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);'
[void][TW.W]::ShowWindow([TW.W]::GetConsoleWindow(), 0)  # 콘솔만 숨김
$form = New-Object System.Windows.Forms.Form
$form.Text = $Title
$form.FormBorderStyle = "None"
$form.StartPosition = "Manual"
$form.Location = New-Object System.Drawing.Point($X, $Y)
$form.Size = New-Object System.Drawing.Size($W, $H)
$form.BackColor = [System.Drawing.Color]::FromArgb($Argb)
$form.ShowInTaskbar = $true
$log = { param($t) Add-Content -Path $EventLog -Value ("{0:HH:mm:ss.fff} {1}" -f (Get-Date), $t) }
$form.Add_MouseDown({ & $log ("down {0} {1}" -f $_.X, $_.Y) })
$form.Add_Shown({ & $log ("hwnd {0}" -f $form.Handle) })
[System.Windows.Forms.Application]::Run($form)
