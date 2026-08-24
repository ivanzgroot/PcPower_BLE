#!/usr/bin/env bash
# Flashes PcPower_BLE over USB.
# Usage: tools/flash.sh [/dev/ttyACM0]
set -euo pipefail
cd "$(dirname "$0")/.."

FQBN="esp32:esp32:esp32c3:PartitionScheme=no_fs,CDCOnBoot=cdc,FlashSize=4M,CPUFreq=160,FlashMode=qio,DebugLevel=none"
export ARDUINO_DIRECTORIES_DATA="$PWD/tools/.arduino-data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$PWD/tools/.arduino-data/staging"
export ARDUINO_DIRECTORIES_USER="$PWD/tools/.arduino-user"

ACLI="$PWD/tools/bin/arduino-cli"
[ -x "$ACLI" ] || ACLI=$(command -v arduino-cli)

PORT="${1:-}"
if [ -z "$PORT" ]; then
  PORT=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n1 || true)
fi
if [ -z "$PORT" ]; then
  echo "no serial port found - plug the board in, or pass one: tools/flash.sh /dev/ttyACM0" >&2
  exit 1
fi

[ -f build/PcPower_BLE.ino.bin ] || { echo "nothing built - run tools/build.sh first" >&2; exit 1; }

echo "==> flashing $PORT"
echo "    (if this fails, hold BOOT, tap RESET, release BOOT, and retry)"
"$ACLI" upload --fqbn "$FQBN" -p "$PORT" --input-dir build PcPower_BLE
echo "==> done. Serial monitor: tools/bin/arduino-cli monitor -p $PORT -c baudrate=115200"
