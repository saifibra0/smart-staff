<#
  Window version of setup-wifi.ps1: sets the gate ESP32's Wi-Fi and its Firebase account password through
  the USB cable, using a small window with text boxes instead of terminal questions.

  Run it with the ESP32 plugged in:
    powershell -ExecutionPolicy Bypass -File .\setup-wifi-window.ps1

  Passwords go straight to the ESP32 over the cable. They are hidden while typed (unless "Show" is
  ticked) and are not saved or logged on the laptop. The ESP32 needs firmware 1.3.0 or newer.
#>
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

function To-B64([string]$Text) {
  if (-not $Text) { return '-' }
  [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Text))
}

# 1. Find the ESP32 on USB (CP210x, CH340, FTDI or native USB).
$port = Get-CimInstance Win32_PnPEntity |
  Where-Object { $_.PNPDeviceID -match 'VID_10C4|VID_1A86|VID_303A|VID_0403' -and $_.Name -match '\((COM\d+)\)' } |
  ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } } | Select-Object -First 1
if (-not $port) {
  [void][System.Windows.Forms.MessageBox]::Show('No ESP32 found on USB. Plug it in with a data cable and try again.', 'Smart Gate setup')
  return
}

# 2. Wi-Fi networks this laptop can see (read as UTF-8 so names like "Saif's iphone" stay exact).
$nets = [ordered]@{}
$current = $null
$prevEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = [Text.Encoding]::UTF8
try {
  netsh wlan show networks mode=bssid | ForEach-Object {
    if ($_ -match '^SSID \d+ : (.*)$') { $current = $Matches[1]; if ($current -and -not $nets.Contains($current)) { $nets[$current] = @() } }
    elseif ($current -and $_ -match '^\s*Band\s*:\s*(.*)$') { $nets[$current] += $Matches[1].Trim() }
  }
} finally { [Console]::OutputEncoding = $prevEncoding }
$names = @($nets.Keys | Where-Object { $_ -and $_ -ne 'SmartGate-Setup' })
$first24 = @($names | Where-Object { (($nets[$_] | Sort-Object -Unique) -join ',') -match '2\.4' }) | Select-Object -First 1

# 3. The window.
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Smart Gate - Wi-Fi setup'
$form.ClientSize = New-Object System.Drawing.Size(460, 470)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.TopMost = $true
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)

function New-Label([string]$Text, [int]$Y, [bool]$Bold = $false) {
  $l = New-Object System.Windows.Forms.Label
  $l.Text = $Text
  $l.Location = New-Object System.Drawing.Point(16, $Y)
  $l.Size = New-Object System.Drawing.Size(428, 22)
  if ($Bold) { $l.Font = New-Object System.Drawing.Font('Segoe UI', 10, [System.Drawing.FontStyle]::Bold) }
  $form.Controls.Add($l)
  $l
}
function New-Box([int]$Y, [bool]$Secret) {
  $b = New-Object System.Windows.Forms.TextBox
  $b.Location = New-Object System.Drawing.Point(16, $Y)
  $b.Width = 428
  $b.UseSystemPasswordChar = $Secret
  $form.Controls.Add($b)
  $b
}

[void](New-Label "ESP32 found on $port. Fill in the boxes, then press Save." 12 $true)
[void](New-Label '1. Wi-Fi network for the gate (2.4 GHz)' 46)
$net = New-Object System.Windows.Forms.ComboBox
$net.Location = New-Object System.Drawing.Point(16, 70)
$net.Width = 428
if ($names.Count) { $net.Items.AddRange([object[]]$names) }
$net.Text = if ($first24) { $first24 } elseif ($names.Count) { $names[0] } else { '' }
$form.Controls.Add($net)

[void](New-Label '2. Password of that Wi-Fi (your hotspot password)' 106)
$wifiBox = New-Box 130 $true
[void](New-Label '3. ESP32 account password (gate-esp32-2@smart-staff.iot in Firebase)' 166)
$devBox = New-Box 190 $true

$show = New-Object System.Windows.Forms.CheckBox
$show.Text = 'Show what I type'
$show.Location = New-Object System.Drawing.Point(16, 222)
$show.AutoSize = $true
$show.Add_CheckedChanged({ $wifiBox.UseSystemPasswordChar = -not $show.Checked; $devBox.UseSystemPasswordChar = -not $show.Checked })
$form.Controls.Add($show)

$save = New-Object System.Windows.Forms.Button
$save.Text = 'Save to ESP32'
$save.Location = New-Object System.Drawing.Point(16, 254)
$save.Size = New-Object System.Drawing.Size(428, 38)
$form.Controls.Add($save)
$form.AcceptButton = $save

$result = New-Object System.Windows.Forms.Label
$result.Location = New-Object System.Drawing.Point(16, 300)
$result.Size = New-Object System.Drawing.Size(428, 44)
$result.Font = New-Object System.Drawing.Font('Segoe UI', 10, [System.Drawing.FontStyle]::Bold)
$form.Controls.Add($result)

$log = New-Object System.Windows.Forms.TextBox
$log.Multiline = $true
$log.ReadOnly = $true
$log.ScrollBars = 'Vertical'
$log.Location = New-Object System.Drawing.Point(16, 348)
$log.Size = New-Object System.Drawing.Size(428, 108)
$log.Font = New-Object System.Drawing.Font('Consolas', 8.5)
$form.Controls.Add($log)

$script:sp = $null
$script:pending = ''
$script:until = Get-Date
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 200

function Show-Result([string]$Text, [string]$Tone) {
  $result.Text = $Text
  $result.ForeColor = switch ($Tone) { 'ok' { [System.Drawing.Color]::ForestGreen } 'bad' { [System.Drawing.Color]::Firebrick } default { [System.Drawing.Color]::DimGray } }
}
function Finish([bool]$Ok, [string]$Text) {
  $timer.Stop()
  if ($script:sp) { try { $script:sp.Close() } catch { } ; $script:sp = $null }
  $save.Enabled = $true
  Show-Result $Text $(if ($Ok) { 'ok' } else { 'bad' })
}

$save.Add_Click({
  $ssid = $net.Text.Trim()
  if (-not $ssid) { Show-Result 'Choose the Wi-Fi network in box 1.' 'bad'; return }
  if ($wifiBox.Text.Length -gt 0 -and $wifiBox.Text.Length -lt 8) { Show-Result 'Wi-Fi passwords have at least 8 characters (box 2).' 'bad'; return }
  if (-not $devBox.Text) { Show-Result 'Type the ESP32 account password in box 3.' 'bad'; return }
  if ($devBox.Text -ceq $wifiBox.Text) { Show-Result 'Box 3 is the same as box 2. Box 3 is the ESP32 account password, not the Wi-Fi one.' 'bad'; return }
  try {
    $script:sp = New-Object System.IO.Ports.SerialPort $port, 115200, 'None', 8, 'One'
    $script:sp.DtrEnable = $false
    $script:sp.RtsEnable = $false
    $script:sp.Open()
  } catch {
    $script:sp = $null
    Show-Result "Could not open $port. Close the Arduino IDE Serial Monitor or any other setup window, then try again." 'bad'
    return
  }
  $script:sp.DiscardInBuffer()
  # The update password stays as it is ("-").
  $script:sp.Write(('GATESET {0} {1} {2} -' -f (To-B64 $ssid), (To-B64 $wifiBox.Text), (To-B64 $devBox.Text)) + "`n")
  $log.Clear()
  $save.Enabled = $false
  $script:pending = ''
  $script:until = (Get-Date).AddSeconds(60)
  Show-Result 'Sent. The ESP32 restarts and signs in (up to 1 minute)...' 'wait'
  $timer.Start()
})

$timer.Add_Tick({
  if (-not $script:sp) { return }
  try { $script:pending += $script:sp.ReadExisting() } catch { }
  $parts = $script:pending -split "`n"
  $script:pending = $parts[-1]
  if ($parts.Length -gt 1) {
    foreach ($line in $parts[0..($parts.Length - 2)]) {
      $line = $line.TrimEnd("`r")
      if ($line -match '^\[(setup|wifi|firebase)\]') { $log.AppendText($line + "`r`n") }
      if ($line -match 'signed in as') { Finish $true 'ONLINE! The ESP32 is signed in. The website shows LIVE in a few seconds. You can close this window.'; return }
      # Firebase's reason comes on the lines after "sign-in failed".
      if ($line -match 'TOO_MANY_ATTEMPTS_TRY_LATER') { Finish $false 'Wi-Fi works. Firebase is blocking sign-in for a while after too many wrong passwords. Leave the ESP32 on: it tries again every 15 minutes and goes online by itself.'; return }
      if ($line -match 'INVALID_LOGIN_CREDENTIALS|INVALID_PASSWORD|EMAIL_NOT_FOUND') { Finish $false 'Wi-Fi works, but Firebase says the ESP32 account password (box 3) is wrong. Check it and press Save again.'; return }
      if ($line -match '^\[setup\] ERROR (.*)$') { Finish $false $Matches[1]; return }
    }
  }
  if ((Get-Date) -gt $script:until) { Finish $false 'No sign-in after 1 minute. Check the Wi-Fi password (box 2) and that the hotspot is on, then press Save again.' }
})

$form.Add_Shown({ $form.Activate(); $wifiBox.Focus() })
$form.Add_FormClosed({ $timer.Stop(); if ($script:sp) { try { $script:sp.Close() } catch { } } })
[void]$form.ShowDialog()
