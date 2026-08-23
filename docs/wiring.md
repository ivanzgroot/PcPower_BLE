# Wiring

Two Omron G3VM-61A1 MOSFET relays, one in each direction. Neither shares a ground with the PC, so
the ESP32 can be powered from any USB supply without tying itself to the PC's 0 V.

The G3VM-61A1 is a 4-pin DIP: pins 1 and 2 are the input LED (1 = anode, 2 = cathode), pins 4 and
3 are the output MOSFETs. The output is bidirectional and unpolarised, which is why neither the
power-button side nor the LED side cares which way round the header wires go — except for the
*input* of the sense relay, which is an LED and does care.

```
  Pin 1 ─┬─ input LED anode        Pin 4 ─┬─ output
  Pin 2 ─┴─ input LED cathode      Pin 3 ─┴─ output
```

---

## 1. Power button output — ESP32 drives the PC

```
   GPIO5 ──[ 220R ]──► pin 1 ┃ G3VM-61A1 #1 ┃ pin 4 ──► PWR_SW pin A
                      pin 2 ─┫              ┣─ pin 3 ──► PWR_SW pin B
                        │    ┗━━━━━━━━━━━━━━┛
                       GND

   GPIO5 ──[ 10k ]── GND        <-- REQUIRED, see below
```

- **220 Ω** gives roughly 7 mA from the ESP32's 3.3 V output into the input LED (forward drop
  around 1.2 V). The G3VM-61A1 needs about 5 mA to switch reliably; the ESP32-C3 can source
  considerably more than that, so there is no need to run it hard.
- The output pins go to the motherboard's **PWR_SW** header, in parallel with the case's existing
  power button. Both continue to work. Polarity does not matter.
- **The 10 kΩ pulldown on GPIO5 is not optional.** ESP32-C3 GPIOs float from reset until firmware
  configures them. A floating gate on the opto's input can turn it on, which presses the PC's
  power button — every time the ESP reboots, including partway through an OTA update. The
  firmware idles the pin as the first statement in `setup()`, but the pulldown covers the window
  before that.

## 2. PWR-LED sense — the PC drives the ESP32

```
   PWR_LED + ──[ R_sense ]──► pin 1 ┃ G3VM-61A1 #2 ┃ pin 4 ──► GPIO3
   PWR_LED - ───────────────── pin 2 ┫              ┣─ pin 3 ──► GND
                                     ┗━━━━━━━━━━━━━━┛

   GPIO3 is configured INPUT_PULLUP, so: LED lit -> pin pulled LOW -> "PC is on"
```

- **The PWR_LED header is polarised.** If the Status tab never shows the PC as on, swap the two
  wires before suspecting anything else.
- **Sizing `R_sense` is the fiddly part.** The header usually sits at 3.3 V or 5 V behind a
  resistor the motherboard already contains, and often sources only 3–5 mA. The G3VM-61A1's input
  LED wants around 5 mA and drops about 1.2 V.
  - Start with the case LED still connected in parallel and **no** added resistor beyond what the
    board provides; measure the voltage across the opto's input.
  - If the opto does not switch, remove the case LED from the header (the opto replaces it) and
    try a **100 Ω** series resistor.
  - If the header is 5 V and switching is reliable, **330–470 Ω** keeps the LED comfortably inside
    its 50 mA absolute maximum.
- Check it from the Status tab rather than with a meter: it shows the live lit-duty percentage, so
  you can watch the number move as the PC boots and shuts down.

## 3. Status LED

Nothing to wire. The SuperMini's on-board LED is on GPIO8 and is **active low** — the firmware
already accounts for that. If you fit an external LED on another pin, set `pin_led` and
`led_active_low` in Settings → Pins.

---

## Pins to leave alone

| Pin | Why |
|---|---|
| GPIO8 | Strapping pin, and the on-board LED. Fine as an output, but it must not be pulled low externally at reset. |
| GPIO9 | BOOT button. Pulling it low at reset enters the bootloader. |
| GPIO18, GPIO19 | Native USB (D− / D+). Used for flashing and the serial console. |
| GPIO20, GPIO21 | UART0. Usable, but you lose the hardware serial port. |

Usable for the three signals here: **GPIO0–GPIO7** and **GPIO10**. The defaults (5, 3, 8) are
chosen to keep the analog-capable pins and the strapping pins out of trouble.

## Powering the board

Any USB supply. If you power it from the PC's own USB, the board reboots when the PC loses power —
which is exactly when it needs to be listening. Use a separate charger, or a USB port that stays
powered in standby (often labelled "always on" in the BIOS).
