# PcPower_BLE

Wakes a PC when a game controller is switched on.

An ESP32-C3 listens for a known Bluetooth LE device advertising, checks the PC is actually off by
reading its power LED, and pulses an opto-isolated relay across the power button. Nothing is
connected electrically to the PC — both directions go through their own opto-isolator, so there is
no shared ground.

Built for a SteamOS machine that sleeps rather than shuts down, where the controller should be the
thing that turns everything on. HDMI-CEC, which brings up the TV, is the PC's business and is not
part of this firmware.

- **Fast.** The guards run inside the Bluetooth advertisement callback, so the button is pressed
  within milliseconds of the radio hearing the controller.
- **Careful.** Four guards run before every press, and the PC-state logic is deliberately
  asymmetric: a false *on* costs a delayed wake, a false *off* would press the button on a running
  machine.
- **Wakes from sleep**, including on motherboards where sleep makes the power LED pulse.
- **Configurable from a phone.** Every setting, including pin numbers and polarities. Firmware
  updates over the air. A serial cable is never required.

---

## Hardware

| Signal | Default pin | Polarity | Goes to |
|---|---|---|---|
| Power-button output | GPIO5 | active **HIGH** | Input LED of a G3VM-61A1; its output sits across the PC's power button |
| PWR-LED sense | GPIO3 | **LOW = LED lit** | Output of a second G3VM-61A1, whose input LED is driven by the PC's power LED |
| Status LED | GPIO8 | active **LOW** | The SuperMini's on-board LED |

Every pin and polarity is a runtime setting, so different wiring is a settings change, not a
recompile.

### Bill of materials

- 1 × ESP32-C3 SuperMini
- 2 × Omron **G3VM-61A1** MOSFET relay (2.5 kVrms isolation, 400 mA / 60 V)
- 1 × 10 kΩ resistor — **pulldown on GPIO5, required, see below**
- 2 × current-limiting resistors for the opto inputs, sized for your wiring (see `docs/wiring.md`)
- A 2-pin lead to the motherboard's PWR_SW header, and one for PWR_LED

```
                 ESP32-C3 SuperMini
                 ┌───────────────┐
                 │               │
   PC PWR_SW ────┤ GPIO5 ──[R]──►│──┐  G3VM-61A1 #1
   (both pins)   │               │  └──► output across the power button
                 │          10k ─┴─ GND      (polarity does not matter)
                 │               │
   PC PWR_LED ──►│──[R]──► G3VM-61A1 #2 ──► GPIO3 ──┐
   (+ and -)     │                                   └─ INPUT_PULLUP
                 │               │
                 │ GPIO8 ── on-board status LED
                 └───────────────┘
        USB-C ── power (a phone charger is fine) and flashing
```

### Two things that will bite you

**GPIO5 needs a 10 kΩ pulldown to GND.** The ESP32-C3's pins float from reset until firmware runs.
Without the pulldown the PC can twitch every time the ESP reboots — including in the middle of an
OTA update. The firmware drives the output to idle as the very first statement in `setup()`, but it
cannot do anything about the microseconds before that.

**The PWR-LED header may not drive the opto-isolator.** Motherboards often source only 3–5 mA
there, which can be below what the G3VM-61A1's input LED needs to switch reliably. If the Status
tab always shows `off` while the PC is on, that is the first thing to check — see
`docs/wiring.md`. The PWR_LED header is also polarised; if it never reads lit, swap the two wires.

---

## Quick start

1. **Flash it.** `tools/build.sh && tools/flash.sh` on Linux or macOS, or
   `powershell -ExecutionPolicy Bypass -File tools\build.ps1` then `...\flash.ps1` on Windows.
   Nothing needs to be installed first — the script fetches its own toolchain.
2. **Join the hotspot.** The board comes up as **`PcPower-XXXX`**, password **`123454321`**. Your
   phone should offer the page automatically; if not, open `http://192.168.4.1/`.
3. **Give it your WiFi.** System tab → scan → pick your network → save. The hotspot stays up until
   the new connection is confirmed, so a typo cannot lock you out.
4. **Learn your controller.** Devices tab → Learn → switch the controller on while it is near the
   board. The strongest signal in the window wins. Give it a name and save.
5. **Done.** From then on it answers at `http://pcpower.local/`.

---

## How it decides

Four guards run before every press. When one blocks, the reason is written to the log, so
"why didn't it wake" is always answerable from the Status tab.

1. **The address is known and enabled.** Rotating addresses (RPA/NRPA) are refused at learning
   time with an explanation — they would work for a few minutes and then silently stop.
2. **The PC is off.** Or asleep, which counts as off by default so a sleeping machine gets woken.
3. **The post-shutdown block has expired** (30 s). A controller that just lost its host looks
   exactly like one being switched on.
4. **The cooldown has expired** (10 s), so one advertising burst cannot press twice.

Two guards are off by default and available if you need them: a **minimum signal strength**, to
ignore a controller in the next room, and **absence re-arm**, which requires a device to have gone
unseen for a while before it may wake the PC again. Turn that on if your controller keeps
advertising after the PC shuts down — most switch themselves off, which is why it ships disabled.

### Reading the power LED

The sense pin is sampled every 2–4 ms into a rolling 2 s window split into eight blocks. Per
window the firmware looks at the overall lit-duty and the spread between blocks:

| What it sees | Verdict |
|---|---|
| Lit ≥ 95 % of the window | **on** |
| Lit ≤ 2 % | **off** |
| In between, but steady across the window | **on** — a PWM-dimmed LED |
| In between and varying across the window | **sleep** — blinking or breathing |

The sampling interval is jittered on purpose. A fixed period can alias a PWM-dimmed LED into a
constant reading, and a constant *dark* reading on a running PC would press its power button.

Entering **on** is immediate. Leaving it takes about three seconds of sustained disagreement,
because a momentary flicker lingers in the window for a further two seconds and must not be able
to look like a shutdown. The live duty and spread are shown on the Status tab, so if you have an
unusual power LED you can tune the thresholds against what the board actually sees.

---

## Status LED

| Pattern | Meaning |
|---|---|
| Solid for half a second at power-up | Boot self-test |
| Two short blinks every 2 s | Hotspot is up, waiting to be configured |
| Fast blink, 5 per second | Joining WiFi, or an update is being written |
| One wink every 3 s | Armed — PC is off, listening for a controller |
| One wink every 8 s | PC is running, scanning paused |
| Rapid flicker | Learning a device |
| Solid | The power button is being held right now |

---

## Building

Both scripts do the same thing and need nothing preinstalled: they download a pinned `arduino-cli`
into `tools/bin/`, install the pinned ESP32 core and NimBLE into a project-local directory (your
global Arduino setup is never touched), regenerate the embedded web page, and compile.

```bash
tools/build.sh              # Linux, macOS
tools/build.sh --clean
tools/flash.sh              # or: tools/flash.sh /dev/ttyACM0
```

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Port COM5
```

Outputs:

- `build/PcPower_BLE.bin` — upload this through the web interface (OTA)
- `build/PcPower_BLE.merged.bin` — full image for the first flash over USB

Set `BUILD_JOBS` to change compile parallelism (defaults to 2; the embedded web page makes one
translation unit memory-hungry).

If the first USB flash fails, hold **BOOT**, tap **RESET**, release **BOOT**, and try again.

### Tests

The decision logic — LED classification, address rules, the guards, settings validation — lives in
`PcPower_BLE/src/core/` with no Arduino dependencies, and is unit-tested on your machine:

```bash
tools/run_tests.sh          # 96 tests, about a second, no hardware needed
```

Anything under `src/core/` must never include `Arduino.h`; that constraint is what keeps the logic
testable.

---

## Editing the web interface

`web/index.html` is the source of truth. `tools/embed_web.py` turns it into
`PcPower_BLE/src/web_ui.h`, which is committed so the build works without Python. `tools/build.sh`
runs it automatically when Python 3 is available.

The Settings form is generated from `/api/config/schema`, which the firmware builds from its own
settings table — so adding a setting means adding one row in
`PcPower_BLE/src/core/settings_model.cpp` and nothing else.

---

## Troubleshooting

**The PC never wakes.** Check the Status tab: does the last decision say `unknown_device`? The
controller may be advertising a different address than the one you learned. Does it say `pc_on`?
The sense wiring is reading the PC as running — see the next entry.

**PC state is always `on`, or always `off`.** Watch the duty and spread numbers on the Status tab
while the PC is on and off. If duty never moves, the sense side is not switching: check the
PWR_LED polarity and the input resistor (`docs/wiring.md`). As a stopgap you can set
`sense_mode = force_off`, which makes the firmware assume the PC is always off — the other guards
still apply, but it will press the button on a running machine.

**It wakes the PC again right after a shutdown.** Your controller keeps advertising after the PC
goes down. Turn on `require_absence` in Settings → Guards.

**It presses twice.** Raise `cooldown_ms`.

**The web page is slow while the PC is off.** That is the scan duty cycle competing with WiFi for
one antenna. Lower `scan_window_ms` or raise `scan_intvl_ms`; the firmware already caps the window
at 60 % of the interval while WiFi is connected.

**Locked out after changing a pin or the hotspot password.** Connect over USB at 115200 and run
`defaults`. There is no reset button for this — the serial console is the way back. `defaults`
restores every setting while keeping your learned devices and WiFi credentials.

---

## Layout

```
PcPower_BLE/
  PcPower_BLE.ino        setup/loop wiring only
  src/core/              decision logic, no Arduino - this is what the tests cover
  src/*.cpp              thin hardware and network adapters
  src/web_ui.h           generated from web/index.html
web/index.html           the interface
tools/                   build, flash, test and embed scripts
tests/                   native unit tests
docs/                    wiring, API reference, design spec and implementation plan
```

## Licence

MIT — see `LICENSE`.
