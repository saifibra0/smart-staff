<#
  Uploads the gate firmware to the ESP32 over Wi-Fi.

  Works after the first upload by USB, with the laptop and the ESP32 on the same Wi-Fi
  (or the ESP32 joined to the laptop's Mobile hotspot). The gate must be closed with
  nothing in front of the sensor, otherwise the ESP32 refuses the update.

  Usage (PowerShell, in this folder):
    .\upload-wifi.ps1                  finds the board as smart-gate.local
    .\upload-wifi.ps1 192.168.137.50   or give its IP address (shown on its status page)

  The update password is read from smart_gate\secrets.h (OTA_PASSWORD).
#>
param([string]$Address = "smart-gate.local")
$ErrorActionPreference = "Stop"

$sketch = Join-Path $PSScriptRoot "smart_gate"
$secretsFile = Join-Path $sketch "secrets.h"
if (-not (Test-Path $secretsFile)) { throw "secrets.h not found in $sketch" }

$match = Select-String -Path $secretsFile -Pattern '^\s*#define\s+OTA_PASSWORD\s+"([^"]*)"' | Select-Object -First 1
$password = if ($match) { $match.Matches[0].Groups[1].Value } else { "" }
if ($password.Length -lt 8 -or $password -eq "choose-an-update-password") {
  throw "Set OTA_PASSWORD in secrets.h (8+ characters), then upload once by USB so the ESP32 knows it."
}

$cli = Join-Path $env:LOCALAPPDATA "Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe"
if (-not (Test-Path $cli)) { throw "Arduino IDE 2 was not found." }
$espota = Get-ChildItem (Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\hardware\esp32\*\tools\espota.exe") -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending | Select-Object -First 1
if (-not $espota) { throw "The ESP32 board package was not found. Install it in Arduino IDE (Boards Manager)." }

$build = Join-Path $env:TEMP "smart_gate_build"
Write-Host "Compiling..."
& $cli compile --fqbn esp32:esp32:esp32 --output-dir $build $sketch
if ($LASTEXITCODE -ne 0) { throw "Compile failed." }

Write-Host "Uploading to $Address over Wi-Fi..."
& $espota.FullName -i $Address -p 3232 "--auth=$password" -f (Join-Path $build "smart_gate.ino.bin") -r
if ($LASTEXITCODE -ne 0) {
  throw "Upload failed. Check that the gate is closed, that the ESP32 is on the same Wi-Fi, and allow espota through Windows Firewall if it asks."
}
Write-Host "Done. The ESP32 restarts with the new firmware."
