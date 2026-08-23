#!/usr/bin/env bash
# Builds PcPower_BLE for the ESP32-C3 SuperMini.
#   build/PcPower_BLE.bin         -> upload through the web UI (OTA)
#   build/PcPower_BLE.merged.bin  -> flash over USB at offset 0 (first install)
# Usage: tools/build.sh [--clean]
set -euo pipefail
cd "$(dirname "$0")/.."

ACLI_VERSION=1.3.1
CORE_VERSION=3.3.11
NIMBLE_VERSION=2.5.1
ESP32_INDEX=https://espressif.github.io/arduino-esp32/package_esp32_index.json
FQBN="esp32:esp32:esp32c3:PartitionScheme=no_fs,CDCOnBoot=cdc,FlashSize=4M,CPUFreq=160,FlashMode=qio,DebugLevel=none"

export ARDUINO_DIRECTORIES_DATA="$PWD/tools/.arduino-data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$PWD/tools/.arduino-data/staging"
export ARDUINO_DIRECTORIES_USER="$PWD/tools/.arduino-user"

ACLI="$PWD/tools/bin/arduino-cli"
if [ ! -x "$ACLI" ]; then
  if command -v arduino-cli >/dev/null 2>&1; then
    ACLI=$(command -v arduino-cli)
  else
    echo "==> downloading arduino-cli $ACLI_VERSION"
    mkdir -p tools/bin
    case "$(uname -m)" in
      x86_64)        ARCH=64bit ;;
      aarch64|arm64) ARCH=ARM64 ;;
      *) echo "unsupported architecture $(uname -m)" >&2; exit 1 ;;
    esac
    case "$(uname -s)" in
      Linux)  OS=Linux ;;
      Darwin) OS=macOS ;;
      *) echo "unsupported OS $(uname -s) - use tools/build.ps1 on Windows" >&2; exit 1 ;;
    esac
    curl -fsSL "https://github.com/arduino/arduino-cli/releases/download/v${ACLI_VERSION}/arduino-cli_${ACLI_VERSION}_${OS}_${ARCH}.tar.gz" \
      | tar xz -C tools/bin arduino-cli
    ACLI="$PWD/tools/bin/arduino-cli"
  fi
fi

if [ "${1:-}" = "--clean" ]; then rm -rf build; fi

echo "==> ensuring toolchain (esp32 $CORE_VERSION, NimBLE $NIMBLE_VERSION)"
"$ACLI" core update-index --additional-urls "$ESP32_INDEX" >/dev/null
"$ACLI" core install "esp32:esp32@${CORE_VERSION}" --additional-urls "$ESP32_INDEX" >/dev/null
"$ACLI" lib install "NimBLE-Arduino@${NIMBLE_VERSION}" >/dev/null

if command -v python3 >/dev/null 2>&1 && [ -f web/index.html ] && [ -f tools/embed_web.py ]; then
  echo "==> embedding web UI"
  python3 tools/embed_web.py
fi

echo "==> compiling"
mkdir -p build
"$ACLI" compile --fqbn "$FQBN" --output-dir build --warnings default PcPower_BLE

cp -f build/PcPower_BLE.ino.bin build/PcPower_BLE.bin
if [ -f build/PcPower_BLE.ino.merged.bin ]; then
  cp -f build/PcPower_BLE.ino.merged.bin build/PcPower_BLE.merged.bin
fi

echo
echo "OTA image : build/PcPower_BLE.bin        (upload in the web UI)"
echo "USB image : build/PcPower_BLE.merged.bin (tools/flash.sh)"
