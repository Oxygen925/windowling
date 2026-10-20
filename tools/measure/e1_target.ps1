# E1 클릭 통과 검증용 대상 창 (별도 프로세스). 받은 마우스 이벤트를 파일에 기록한다.
param([Parameter(Mandatory)][string]$EventLog)
Add-Type -AssemblyName System.Windows.Forms
# 콘솔 창만 숨긴다 (Start-Process -WindowStyle Hidden 은 폼의 첫 ShowWindow까지 숨겨버림)
Add-Type -Namespace E1T -Name W -MemberDefinition '[DllImport("kernel32.dll")] public static extern System.IntPtr GetConsoleWindow(); [DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);'
[void][E1T.W]::ShowWindow([E1T.W]::GetConsoleWindow(), 0)
$form = New-Object System.Windows.Forms.Form
$form.Text = "E1 target"
$form.FormBorderStyle = "None"   # 제목 표시줄 없이 전체가 클라이언트 영역
$form.StartPosition = "Manual"
$form.Location = New-Object System.Drawing.Point(0, 0)
$form.Size = New-Object System.Drawing.Size(900, 500)
$form.BackColor = [System.Drawing.Color]::FromArgb(40, 60, 90)
$log = { param($t) Add-Content -Path $EventLog -Value ("{0:HH:mm:ss.fff} {1}" -f (Get-Date), $t) }
$form.Add_MouseDown({ & $log ("down {0} {1}" -f $_.X, $_.Y) })
$form.Add_MouseWheel({ & $log ("wheel {0} {1} {2}" -f $_.X, $_.Y, $_.Delta) })
$form.Add_Activated({ & $log "activated" })
$form.Add_Deactivate({ & $log "deactivated" })
$form.Add_Shown({ & $log ("hwnd {0}" -f $form.Handle) })
[System.Windows.Forms.Application]::Run($form)
