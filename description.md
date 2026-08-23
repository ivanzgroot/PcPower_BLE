# Features

An ESP32-C3 wakes a PC when a known Bluetooth device „mostly Gaming controller“ is switched on. It hears
the device advertise, confirms the PC is actually off, and pulses an
opto-isolated relay across the power button.

### Waking

- **Continuous BLE scan** — 200 / 60 ms (30 % duty default), active, duplicates not filtered, duty cycle can be configured in settings with safeguards as to not disrupt wifi webui serve.
- **Address matching** — 8 devices by default, 32 maximum, each with a label
- **Rotating addresses refused** — RPA/NRPA are classified and rejected; they stop working within minutes
- **Learning mode** — hold the device close; the strongest signal in a 5 s window wins
- **Four guards before the pulse** — address known, PC off, post-shutdown block expired, cooldown expired

### PC state detection

- **Opto-isolated sense** — reads the PWR-LED through a second relay; no shared ground with the PC

- **Survives PWM-dimmed and pulsing LEDs** — the asymmetry is deliberate: a false *on* costs a delayed wake, a false *off* would press the button on a running PC

### Timing guards

- **Cooldown** — 10 s after each pulse, so one advertising burst cannot press twice
- **Post-shutdown block** — 30 s, because a controller that just lost its host looks exactly like one being switched on
- **Scan pause** — stops scanning while the PC runs; nothing can trigger anyway, so the antenna goes to WiFi

### Interfaces

- **Web interface** — works on a phone
- **Self-provisioning** — opens its own WPA2 hotspot when no network is stored
- **Over-the-air updates** — upload a `.bin` from the browser
- **HTTP API** — the web page is only a client of it
- **Serial console** — commands at 115200 baud
- **Log ring buffer** — last 64 lines with millisecond timestamps, readable without a cable via webui

### Configuration

- ** runtime settings** — pulse length, scan timing, radio coexistence, even pin assignment and polarity etc. Everything.

- **Restore defaults** — keeps learned devices and WiFi credentials

### Hardware

- **2 × Omron G3VM-61A1** — 2.5 kVrms isolation in each direction
- **Status LED** — six distinct states
- **Boot-glitch protection** — the output is driven to idle as the first statement in `setup()`
