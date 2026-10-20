# 성능 비회귀 R4용: 한 프로세스에서 작은 창 N개를 띄운다 (창이 많은 데스크탑 재현)
param([int]$Count = 50, [int]$Seconds = 120)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace MW -Name W -MemberDefinition '[DllImport("kernel32.dll")] public static extern System.IntPtr GetConsoleWindow(); [DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);'
[void][MW.W]::ShowWindow([MW.W]::GetConsoleWindow(), 0)
$forms = @()
for ($i = 0; $i -lt $Count; $i++) {
    $f = New-Object System.Windows.Forms.Form
    $f.Text = "many $i"
    $f.StartPosition = "Manual"
    $f.Location = New-Object System.Drawing.Point((100 + ($i % 10) * 220), (100 + [math]::Floor($i / 10) * 220))
    $f.Size = New-Object System.Drawing.Size(200, 180)
    $f.Show()
    $forms += $f
}
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = $Seconds * 1000
$timer.Add_Tick({ [System.Windows.Forms.Application]::Exit() })
$timer.Start()
[System.Windows.Forms.Application]::Run()
