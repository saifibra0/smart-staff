<#
  Sets the gate ESP32's Wi-Fi and passwords through the USB cable (no need to edit secrets.h).

  Run it in PowerShell with the ESP32 plugged in:
    powershell -ExecutionPolicy Bypass -File .\setup-wifi.ps1

  It lists the Wi-Fi networks your laptop can see, asks which one the gate should use, and asks
  for the passwords. Passwords are typed hidden and go straight to the ESP32 over the cable; they
  are not shown or stored on the laptop. The ESP32 needs firmware 1.3.0 or newer.
#>
param([string]$Port = '')
$ErrorActionPreference = 'Stop'

function Read-Secret([string]$Prompt) {
  $secure = Read-Host -Prompt $Prompt -AsSecureString
  $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
  try { [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
}
function To-B64([string]$Text) {
  if (-not $Text) { return '-' }
  [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Text))
}

# 1. Find the ESP32 on USB (CP210x, CH340, FTDI or native USB).
if (-not $Port) {
  $Port = Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.PNPDeviceID -match 'VID_10C4|VID_1A86|VID_303A|VID_0403' -and $_.Name -match '\((COM\d+)\)' } |
    ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } } | Select-Object -First 1
}
if (-not $Port) { throw 'No ESP32 found on USB. Plug it in with a data cable and try again.' }
Write-Host ''
Write-Host "ESP32 found on $Port" -ForegroundColor Green

# 2. Choose the Wi-Fi network. The ESP32 can only use 2.4 GHz.
$nets = [ordered]@{}
$current = $null
# Read netsh as UTF-8 so names with symbols (like a curly apostrophe in a phone hotspot name) reach the ESP32 exactly.
$prevEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = [Text.Encoding]::UTF8
try {
  netsh wlan show networks mode=bssid | ForEach-Object {
    if ($_ -match '^SSID \d+ : (.*)$') { $current = $Matches[1]; if ($current -and -not $nets.Contains($current)) { $nets[$current] = @() } }
    elseif ($current -and $_ -match '^\s*Band\s*:\s*(.*)$') { $nets[$current] += $Matches[1].Trim() }
  }
} finally { [Console]::OutputEncoding = $prevEncoding }
$names = @($nets.Keys | Where-Object { $_ -and $_ -ne 'SmartGate-Setup' })
Write-Host ''
Write-Host 'Wi-Fi networks this laptop can see:'
for ($i = 0; $i -lt $names.Count; $i++) {
  $bands = ($nets[$names[$i]] | Sort-Object -Unique) -join ', '
  $ok = if ($bands -match '2\.4') { '' } else { '   (5 GHz only: the ESP32 cannot use it)' }
  Write-Host ('  {0}) {1}   [{2}]{3}' -f ($i + 1), $names[$i], $bands, $ok)
}
$ssid = $null
while (-not $ssid) {
  $choice = (Read-Host 'Type the NUMBER of the network for the gate (for example 1)').Trim()
  if ($choice -match '^\d+$' -and [int]$choice -ge 1 -and [int]$choice -le $names.Count) { $ssid = $names[[int]$choice - 1] }
  elseif ($choice -and $names -contains $choice) { $ssid = $choice }
  elseif ($choice) {
    $sure = Read-Host "'$choice' is not in the list above. Use it anyway? (y/n)"
    if ($sure -match '^(y|yes)$') { $ssid = $choice } else { Write-Host 'OK, choose again.' }
  }
}
Write-Host "Network: $ssid" -ForegroundColor Cyan

# 3. Passwords (typed hidden).
$wifiPass = Read-Secret "Password of the Wi-Fi '$ssid'"
$devicePass = Read-Secret 'Password of the ESP32 account (gate-esp32@smart-staff.iot), or press Enter to keep the one already saved'
$otaPass = Read-Secret 'Update password for Wi-Fi uploads (8+ characters), or press Enter to keep/skip'
if ($wifiPass -and $wifiPass.Length -lt 8) { throw 'Wi-Fi passwords have at least 8 characters. Run the script again.' }
if ($otaPass -and $otaPass.Length -lt 8) { throw 'The update password needs at least 8 characters. Run the script again.' }

# 4. Send it to the ESP32 and show what happens.
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.NewLine = "`n"
try { $sp.Open() } catch { throw "Could not open $Port. Close Arduino IDE's Serial Monitor (or any other program using it) and try again." }
try {
  $sp.DiscardInBuffer()
  $sp.WriteLine(('GATESET {0} {1} {2} {3}' -f (To-B64 $ssid), (To-B64 $wifiPass), (To-B64 $devicePass), (To-B64 $otaPass)))
  $wifiPass = $null; $devicePass = $null; $otaPass = $null
  Write-Host ''
  Write-Host 'Sent. Waiting for the ESP32...'
  $end = (Get-Date).AddSeconds(60)
  $pending = ''
  $answered = $false
  while ((Get-Date) -lt $end) {
    Start-Sleep -Milliseconds 100
    $pending += $sp.ReadExisting()
    $parts = $pending -split "`n"
    $pending = $parts[-1]
    foreach ($line in $parts[0..($parts.Length - 2)]) {
      $line = $line.TrimEnd("`r")
      if ($line -match '^\[setup\] (OK|ERROR) ') {
        $answered = $true
        if ($Matches[1] -eq 'OK') { Write-Host $line -ForegroundColor Green } else { Write-Host $line -ForegroundColor Red; return }
      } elseif ($line -match '^\[(setup|wifi|firebase|ota|web)\]|^Smart Parking') {
        Write-Host $line
        if ($line -match 'signed in as') { Write-Host ''; Write-Host 'Done: the gate is online. Open the website''s Entrance Gate page.' -ForegroundColor Green; return }
      }
    }
  }
  if (-not $answered) { Write-Host 'No answer from the ESP32. Is firmware 1.3.0 or newer on it?' -ForegroundColor Yellow }
  else { Write-Host 'The ESP32 has not signed in to Firebase yet. If it keeps "connecting", check the Wi-Fi password.' -ForegroundColor Yellow }
} finally {
  $sp.Close()
}
