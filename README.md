# PcPower_BLE

Switch on a game controller, and the PC turns on.

An ESP32-C3 SuperMini listens for Bluetooth LE advertisements from controllers you have
registered with it, checks that the PC is not already running, and closes a relay across the power
button for a few hundred milliseconds. Both directions go through their own opto-isolator, so the
board shares no ground with the PC and can run off any USB supply.

It was written for a SteamOS machine that sleeps rather than shutting down, living under a TV where
reaching for the case is a nuisance. Waking the TV is HDMI-CEC's job and happens on the PC side;
this firmware only deals with the power button.

Everything is configurable from a phone browser, including the pin assignments, and firmware
updates go over the air. You should not need a serial cable after the first flash.

## Hardware

| Signal | Default pin | Polarity | Connects to |
| --- | --- | --- | --- |
| Power button | GPIO5 | active high | Input LED of a G3VM-61A1, output across the PC's power switch |
| PWR-LED sense | GPIO3 | low = LED lit | Output of a second G3VM-61A1, input driven by the PC's power LED |
| Status LED | GPIO8 | active low | On-board LED of the SuperMini |

You will need an ESP32-C3 SuperMini, two Omron G3VM-61A1 MOSFET relays, a 10 kΩ resistor, two
current-limiting resistors for the opto inputs, and leads to the motherboard's PWR_SW and PWR_LED
headers. Full details, including how to size the resistors, are in [docs/wiring.md](docs/wiring.md).

```
                 ESP32-C3 SuperMini
                 ┌───────────────┐
   PC PWR_SW ────┤ GPIO5 ──[R]──►│──┐  G3VM-61A1 #1
   (both pins)   │               │  └──► output across the power button
                 │          10k ─┴─ GND
                 │               │
   PC PWR_LED ──►│──[R]──► G3VM-61A1 #2 ──► GPIO3 (INPUT_PULLUP)
   (+ and -)     │               │
                 │ GPIO8 ── on-board status LED
                 └───────────────┘
        USB-C ── power and flashing
```

Fit the 10 kΩ pulldown on GPIO5 before you connect anything to the PC. ESP32-C3 pins float from
reset until firmware configures them, and a floating opto input can close the relay, which presses
the power button. The firmware idles that pin as the first statement in `setup()`, but it cannot do
anything about the microseconds before its own code runs. Without the resistor the PC may twitch
every time the board reboots, including partway through a firmware update.

The other thing that catches people out is the sense side. Motherboards often drive the PWR_LED
header with only 3-5 mA, which can be too little for the G3VM-61A1's input LED. If the Status page
reports the PC as off while it is plainly running, that is the first thing to check. The header is
also polarised, so try swapping the two wires before suspecting anything else.

## Getting started

Build and flash over USB:

```bash
tools/build.sh && tools/flash.sh          # Linux, macOS
```

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1
powershell -ExecutionPolicy Bypass -File tools\flash.ps1
```

Nothing needs installing first. The scripts fetch a pinned `arduino-cli`, install the ESP32 core
and NimBLE into a project-local directory, and leave your global Arduino setup alone. If the first
upload fails, hold BOOT, tap RESET, release BOOT and try again.

The board then comes up as its own access point, `PcPower-XXXX`, password `123454321`. Most phones
offer the configuration page automatically; if yours does not, open `http://192.168.4.1/`. Give it
your WiFi on the System tab. The hotspot deliberately stays up until the new connection succeeds,
so a mistyped password cannot lock you out. After that the board answers at `http://pcpower.local/`.

Finally, go to the Devices tab, press Learn, and switch your controller on while it is close to the
board. The strongest signal during the five-second window wins. Name it, save, and you are done.

### Flashing from the Arduino IDE

If you would rather use the IDE for the first flash, add the Espressif board manager URL
(`https://espressif.github.io/arduino-esp32/package_esp32_index.json`), install **esp32 3.3.11**
and **NimBLE-Arduino 2.5.1**, then open `PcPower_BLE/PcPower_BLE.ino` and select the *ESP32C3 Dev
Module* board.

Two settings under Tools are not optional:

- **Partition Scheme: No FS 4MB (2MB APP x2)**. The firmware is around 1.37 MB and the default
  partition only allows 1.2 MB, so the IDE will reject it otherwise. This also provides the second
  slot that OTA updates need.
- **USB CDC On Boot: Enabled**. The SuperMini has no USB-serial chip. With this disabled, `Serial`
  is routed to GPIO20/21 and you get nothing over USB.

Set Flash Size to 4 MB and leave the rest at their defaults. These match what the build scripts
use, so IDE and script builds are interchangeable.

## How it decides

Four conditions must hold before the button is pressed, and whichever one blocks is written to the
log, so the Status tab can always tell you why nothing happened.

The address has to be known and enabled. Addresses that rotate (RPA and NRPA) are refused during
learning rather than accepted and quietly forgotten later; the interface explains which kind it saw
and why it will not work.

The PC has to be off, or asleep, since sleep counts as off by default.

The post-shutdown block, 30 seconds, has to have expired. A controller that has just lost its host
looks identical to one being switched on, and without this the PC would come straight back up after
every shutdown.

The cooldown, 10 seconds, has to have expired, so a single burst of advertisements cannot press the
button twice.

Two further guards are available and off by default: a minimum signal strength, useful if a
controller in the next room keeps waking the machine, and absence re-arm, which requires a device to
have gone unseen for a while before it may trigger again. Turn the latter on if your controller
keeps advertising after the PC shuts down. Most switch themselves off, which is why it ships
disabled.

### Reading the power LED

The sense pin is sampled every 2-4 ms into a rolling two-second window split into eight blocks. Each
window is judged on how much of it was lit, and on how much that varied from block to block.

| Measurement | Verdict |
| --- | --- |
| Lit for 95% of the window or more | on |
| Lit for 2% or less | off |
| In between, steady across the window | on, a dimmed LED |
| In between, varying across the window | sleep, a blinking or breathing LED |

The sampling interval is jittered on purpose. A fixed period can alias a PWM-dimmed LED into a
constant reading, and reading a running PC as dark would press its power button.

The two directions are deliberately not symmetric. Entering the on state is immediate, because
being slow to notice the PC came up costs nothing. Leaving it takes about three seconds of
sustained disagreement, because a momentary flicker lingers in the window for a further two seconds
and must never be mistaken for a shutdown.

If your power LED behaves unusually, the Status tab shows the live duty and spread figures the
classifier is working from, and every threshold is adjustable.

## Status LED

| Pattern | Meaning |
| --- | --- |
| Solid for half a second at power-up | Boot self-test |
| Two short blinks every 2 s | Hotspot up, waiting for configuration |
| Five blinks per second | Joining WiFi, or writing a firmware update |
| One brief wink every 3 s | Armed; PC is off and the radio is listening |
| One brief wink every 8 s | PC is running, scanning paused |
| Rapid flicker | Learning a device |
| Solid | Power button held right now |

## Development

```bash
tools/run_tests.sh          # 96 tests, about a second, no hardware
tools/build.sh --clean
```

The decision logic lives in `PcPower_BLE/src/core/` and has no Arduino dependencies, so it compiles
with plain `g++` and is covered by unit tests: LED classification, address rules, the guards,
settings validation and clamping, the device list, the learning window and the log buffer. Nothing
under `src/core/` may include `Arduino.h`, and that restriction is the only reason any of it is
testable. Everything else is a thin adapter over pins, the radio or HTTP.

Two build outputs are produced. `build/PcPower_BLE.bin` is what you upload through the web
interface; `build/PcPower_BLE.merged.bin` includes the bootloader and partition table and is only
for flashing over USB. Set `BUILD_JOBS` if you want to change compile parallelism, which defaults to
2 because the embedded web page makes one translation unit memory-hungry.

The interface itself is `web/index.html`, a single file with no external requests, since it has to
work when your phone is joined to the board's own hotspot with no route to the internet.
`tools/embed_web.py` turns it into `PcPower_BLE/src/web_ui.h`, which is committed so that the build
does not require Python. Settings forms are generated from `/api/config/schema`, which the firmware
derives from its own settings table, so adding a setting means adding one row in
`src/core/settings_model.cpp` and nothing else. The HTTP API is documented in
[docs/api.md](docs/api.md).

## Troubleshooting

**Nothing happens when the controller comes on.** Check the last decision on the Status tab. If it
says the device is unknown, the controller is advertising a different address from the one you
learned, which usually means it uses a rotating address. If it says the PC is already running, the
sense wiring is misreading; see below.

**PC state is stuck on or off.** Watch the duty figure on the Status tab while the PC changes state.
If it never moves, the sense side is not switching at all, which is a wiring or resistor problem.
As a stopgap, setting `sense_mode` to `force_off` makes the firmware assume the PC is always off.
The other guards still apply, but it will happily press the button on a running machine.

**A sleeping PC is reported as on.** Lower `spread_pct` until the pulsing LED registers as sleep.

**The PC wakes again right after you shut it down.** Your controller keeps advertising once the host
is gone. Enable `require_absence` under Guards.

**The web interface is sluggish while the PC is off.** That is the BLE scan competing with WiFi for
a single antenna. Raise `scan_intvl_ms` or lower `scan_window_ms`. The firmware already caps the
window at 60% of the interval whenever WiFi is connected.

**A firmware update fails with "Partition Could Not be Found".** The board is running a partition
table with only one app slot, so there is nowhere to write a new image. This happens if it was
flashed from the Arduino IDE with a scheme such as "Huge APP (3MB No OTA)", which is a tempting
choice once the default one rejects the sketch for being too big. Reflash it once over USB using
No FS 4MB (2MB APP x2), or the `-usb.bin` from a release, and updates will work from then on. The
System tab says so up front, and `status` on the serial console reports which slot it is running
from and whether a spare exists.

**Locked out after changing a pin or the hotspot password.** Connect over USB at 115200 baud and run
`defaults`. That restores every setting while keeping your learned devices and WiFi credentials.

## A note on security

There is no password on the web interface. This is a deliberate choice: the board lives on a home
network, and being locked out of the thing that turns your PC on is worse than the alternative. The
consequence is that anyone who can reach it can also flash firmware onto it, so keep it off guest
and public networks.

Requests are checked for a matching `Host` header, and for a matching `Origin` when a browser
supplies one. That prevents a web page you happen to be visiting from driving the API through your
browser, which is the realistic attack. It is not a substitute for a network you trust.

The hotspot password is only used during setup and defaults to `123454321`. Change it under
Settings → Network if that matters where the board is installed.

## Layout

```
PcPower_BLE/
  PcPower_BLE.ino      setup and loop, wiring only
  src/core/            decision logic, no Arduino, covered by the tests
  src/*.cpp            adapters for pins, radio, WiFi, HTTP and the console
  src/web_ui.h         generated from web/index.html
web/index.html         the interface
tools/                 build, flash, test and embed scripts
tests/                 native unit tests
docs/                  wiring guide and API reference
```

## Licence

MIT. See [LICENSE](LICENSE).
