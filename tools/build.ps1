# Builds PcPower_BLE for the ESP32-C3 SuperMini.
#   build\PcPower_BLE.bin         -> upload through the web UI (OTA)
#   build\PcPower_BLE.merged.bin  -> flash over USB at offset 0 (first install)
# Usage: powershell -ExecutionPolicy Bypass -File tools\build.ps1 [-Clean]
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$AcliVersion   = '1.3.1'
$CoreVersion   = '3.3.11'
$NimbleVersion = '2.5.1'
$Esp32Index    = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
$Fqbn = 'esp32:esp32:esp32c3:PartitionScheme=no_fs,CDCOnBoot=cdc,FlashSize=4M,CPUFreq=160,FlashMode=qio,DebugLevel=none'

$env:ARDUINO_DIRECTORIES_DATA      = Join-Path $Root 'tools\.arduino-data'
$env:ARDUINO_DIRECTORIES_DOWNLOADS = Join-Path $Root 'tools\.arduino-data\staging'
$env:ARDUINO_DIRECTORIES_USER      = Join-Path $Root 'tools\.arduino-user'

function Invoke-Checked {
    param([string]$Exe, [string[]]$Args)
    & $Exe @Args
    if ($LASTEXITCODE -ne 0) { throw "$Exe $($Args -join ' ') failed with exit code $LASTEXITCODE" }
}

$Acli = Join-Path $Root 'tools\bin\arduino-cli.exe'
if (-not (Test-Path $Acli)) {
    $onPath = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($onPath) {
        $Acli = $onPath.Source
    } else {
        Write-Host "==> downloading arduino-cli $AcliVersion"
        New-Item -ItemType Directory -Force -Path (Join-Path $Root 'tools\bin') | Out-Null
        $arch = if ([Environment]::Is64BitOperatingSystem) { '64bit' } else { '32bit' }
        $url  = "https://github.com/arduino/arduino-cli/releases/download/v$AcliVersion/arduino-cli_${AcliVersion}_Windows_$arch.zip"
        $zip  = Join-Path $env:TEMP "arduino-cli-$AcliVersion.zip"
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath (Join-Path $Root 'tools\bin') -Force
        Remove-Item $zip -Force
        $Acli = Join-Path $Root 'tools\bin\arduino-cli.exe'
    }
}

if ($Clean -and (Test-Path 'build')) { Remove-Item -Recurse -Force 'build' }

Write-Host "==> ensuring toolchain (esp32 $CoreVersion, NimBLE $NimbleVersion)"
Invoke-Checked $Acli @('core','update-index','--additional-urls',$Esp32Index)
Invoke-Checked $Acli @('core','install',"esp32:esp32@$CoreVersion",'--additional-urls',$Esp32Index)
Invoke-Checked $Acli @('lib','install',"NimBLE-Arduino@$NimbleVersion")

$python = Get-Command python -ErrorAction SilentlyContinue
if ($python -and (Test-Path 'web\index.html') -and (Test-Path 'tools\embed_web.py')) {
    Write-Host '==> embedding web UI'
    Invoke-Checked $python.Source @('tools/embed_web.py')
}

Write-Host '==> compiling'
New-Item -ItemType Directory -Force -Path 'build' | Out-Null
Invoke-Checked $Acli @('compile','--fqbn',$Fqbn,'--output-dir','build','--warnings','default','PcPower_BLE')

Copy-Item 'build\PcPower_BLE.ino.bin' 'build\PcPower_BLE.bin' -Force
if (Test-Path 'build\PcPower_BLE.ino.merged.bin') {
    Copy-Item 'build\PcPower_BLE.ino.merged.bin' 'build\PcPower_BLE.merged.bin' -Force
}

Write-Host ''
Write-Host 'OTA image : build\PcPower_BLE.bin        (upload in the web UI)'
Write-Host 'USB image : build\PcPower_BLE.merged.bin (tools\flash.ps1)'
