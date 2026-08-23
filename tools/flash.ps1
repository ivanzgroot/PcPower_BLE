# Flashes PcPower_BLE over USB.
# Usage: powershell -ExecutionPolicy Bypass -File tools\flash.ps1 [-Port COM5]
param([string]$Port)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$Fqbn = 'esp32:esp32:esp32c3:PartitionScheme=no_fs,CDCOnBoot=cdc,FlashSize=4M,CPUFreq=160,FlashMode=qio,DebugLevel=none'
$env:ARDUINO_DIRECTORIES_DATA      = Join-Path $Root 'tools\.arduino-data'
$env:ARDUINO_DIRECTORIES_DOWNLOADS = Join-Path $Root 'tools\.arduino-data\staging'
$env:ARDUINO_DIRECTORIES_USER      = Join-Path $Root 'tools\.arduino-user'

$Acli = Join-Path $Root 'tools\bin\arduino-cli.exe'
if (-not (Test-Path $Acli)) {
    $onPath = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if (-not $onPath) { throw 'arduino-cli not found - run tools\build.ps1 first' }
    $Acli = $onPath.Source
}

if (-not (Test-Path 'build\PcPower_BLE.ino.bin')) { throw 'nothing built - run tools\build.ps1 first' }

if (-not $Port) {
    $boards = & $Acli board list --format json | ConvertFrom-Json
    $ports = @($boards.detected_ports | Where-Object { $_.port.address -like 'COM*' } |
               ForEach-Object { $_.port.address } | Sort-Object)
    if ($ports.Count -eq 0) { throw 'no serial port found - plug the board in, or pass -Port COM5' }
    $Port = $ports[-1]
}

Write-Host "==> flashing $Port"
Write-Host '    (if this fails, hold BOOT, tap RESET, release BOOT, and retry)'
& $Acli upload --fqbn $Fqbn -p $Port --input-dir build PcPower_BLE
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "==> done. Serial monitor: tools\bin\arduino-cli.exe monitor -p $Port -c baudrate=115200"
