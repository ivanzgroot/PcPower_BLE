# HTTP API

The web page is only a client of this API — anything the interface can do, `curl` can do.

- Base URL: `http://pcpower.local/` on your LAN, or `http://192.168.4.1/` on the board's hotspot.
- Responses are JSON unless noted. Request bodies are `application/x-www-form-urlencoded`, which
  is why the firmware carries no JSON parser: it emits JSON, and never reads it.
- Errors return a non-200 status with `{"ok":false,"error":"..."}`.
- There is **no authentication**, by design. Anyone who can reach the board can update its
  firmware — keep it on a network you trust.
- Two guards make up for that where a browser is involved. Requests must carry a **`Host`** header
  naming this board (its IP, `pcpower.local`, or `192.168.4.1`), which stops DNS rebinding; and a
  request carrying an **`Origin`** header must match that host, which stops a web page you happen
  to be visiting from driving the API. Requests with no `Origin` — `curl`, scripts — are allowed
  through, so automating the API is as easy as it looks below. A refused request gets 421 or 403.
- Durations in milliseconds. The value **4294967295** (`UINT32_MAX`) means *never* — no pulse yet,
  never seen, never been on.

The field lists below are taken from the emitters in `PcPower_BLE/src/web_server.cpp`,
`core/settings_model.cpp`, `core/device_list.cpp` and `core/learner.cpp`.

---

## Status

### `GET /api/status`

Everything the Status tab shows, in one call.

```bash
curl -s http://pcpower.local/api/status
```

```json
{
  "pc": {"state":"off","raw":"off","duty":0,"spread":0,"ready":true,"lit":false,
         "ms_since_off":428113,"mode":"auto"},
  "net": {"mode":"station","ssid":"home","ip":"192.168.1.42","rssi":-58,
          "ap_active":false,"ap_ssid":"PcPower-3F2A","hostname":"pcpower",
          "has_credentials":true},
  "scan": {"scanning":true,"paused":false,"inhibited":false,"adverts":18422,
           "last_advert_ms":210,"last_reason":"unknown_device",
           "last_reason_text":"device is not in the known list","last_reason_ms":210},
  "pulse": {"count":3,"ms_since_last":428601,"active":false},
  "learn": {"active":false,"remaining_ms":0},
  "ota": {"running":false,"percent":0,"capable":true,"partition":"app0","slot_bytes":1966080},
  "uptime_ms":431204, "heap":198432, "version":"1.0.0",
  "devices":[ ... see GET /api/devices ... ]
}
```

`ota.capable` is false when the board's partition table has no second app slot, in which case
`POST /update` will refuse the upload immediately rather than failing partway through.
`ota.partition` names the slot currently running.

`pc.state` is the debounced state the guards use; `pc.raw` is the latest window classification
before hysteresis. `duty` and `spread` are what the classifier measured — the numbers to tune
against if your power LED behaves unusually.

### `GET /api/logs` · `POST /api/logs/clear`

The last 64 lines with millisecond timestamps.

```json
{"total":312,"lines":[{"t":428601,"m":"WAKE: 8BitDo Pro 2 pressed the power button"}]}
```

---

## Settings

### `GET /api/config`

Flat object of every setting: numbers for booleans, integers and enums, strings for text.

### `GET /api/config/schema`

What the Settings form is built from — key, type, range, label, unit, group, options and help for
every setting.

```json
[{"key":"pulse_ms","type":"int","min":50,"max":2000,"label":"Pulse length","unit":"ms",
  "group":"Power","options":null,
  "help":"How long the power button is held for a normal press."}]
```

### `POST /api/config`

Any number of `key=value` pairs. Values are **clamped into range rather than rejected**; the
response says which were adjusted. Changes take effect immediately — no reboot — and are saved.

```bash
curl -s -X POST http://pcpower.local/api/config -d 'pulse_ms=350&require_absence=1'
```

```json
{"ok":true,"applied":2,"clamped":[]}
```

Booleans accept `1/0`, `true/false`, `on/off`, `yes/no`. Enums accept the option name
(`force_off`) or its index. An unknown key or an unparseable value returns 400 and changes
nothing.

### `POST /api/config/defaults`

Restores every setting. **Learned devices and WiFi credentials are kept.**

### `GET /api/config/export` · `POST /api/config/import`

Export returns `text/plain` `key=value` lines with `#` comments — human-readable and
hand-editable. Import takes the same text in a `conf` field.

```bash
curl -s http://pcpower.local/api/config/export > pcpower.conf
curl -s -X POST http://pcpower.local/api/config/import --data-urlencode "conf@pcpower.conf"
```

---

## Devices

### `GET /api/devices`

```json
{"devices":[{"index":0,"addr":"C5:1A:7D:DA:71:13","type":1,"label":"8BitDo Pro 2",
             "enabled":true,"seen":true,"last_seen_ms":210,"rssi":-52,"triggers":3,
             "kind":"random-static"}]}
```

`last_seen_ms` is how long ago, not a timestamp. `seen:false` means it has not advertised since
this boot.

### `POST /api/devices`

`addr` (required, `AA:BB:CC:DD:EE:FF`), `type` (0 public, 1 random — default 1), `label`.
Rotating addresses are refused with the reason as the error text.

### `POST /api/devices/update` · `POST /api/devices/delete`

`index` plus `label` and/or `enabled` for update; `index` alone for delete. Deleting compacts the
list, so indices after it shift down.

---

## Learning

### `POST /api/learn/start`

`seconds` (2–30, default 5). Triggering is inhibited while learning, so a controller held next to
the board cannot press the button.

### `GET /api/learn/status`

```json
{"active":true,"remaining_ms":3200,"best_stable":1,
 "candidates":[
   {"index":0,"addr":"45:1A:7D:DA:71:13","type":1,"name":"","rssi":-38,"hits":22,
    "kind":"resolvable-private","learnable":false,
    "reason":"this device rotates its address every few minutes, so it would stop working within the hour"},
   {"index":1,"addr":"C5:1A:7D:DA:71:13","type":1,"name":"8BitDo Pro 2","rssi":-52,"hits":17,
    "kind":"random-static","learnable":true,"reason":""}]}
```

Candidates are sorted strongest-first. Ones that cannot be learned are still listed, with the
reason — the point is that the user understands *why* their controller was refused.

### `POST /api/learn/accept` · `POST /api/learn/cancel`

Accept takes `index` and an optional `label` (defaults to the advertised name).

---

## Actions

### `POST /api/action/press`

`mode=short` (default, `pulse_ms`) or `mode=long` (`long_press_ms` — the force-off). Returns 409
if a press is already running. Manual presses bypass the guards.

```bash
curl -s -X POST http://pcpower.local/api/action/press -d 'mode=short'
```

### `POST /api/reboot`

Replies first, restarts about 500 ms later.

---

## Network

### `GET /api/wifi/scan`

Starts an asynchronous scan and returns what it has. Poll until `scanning` is false.

```json
{"scanning":false,"networks":[{"ssid":"home","rssi":-58,"secure":true}]}
```

### `POST /api/wifi` · `POST /api/wifi/forget`

`ssid` and `pass`. The hotspot deliberately stays up until the new connection is confirmed, so a
wrong password cannot lock you out.

---

## Firmware update

### `POST /update`

Multipart upload of `build/PcPower_BLE.bin`. Scanning is paused and triggering inhibited for the
duration. On success the board replies `{"ok":true}` and reboots about 500 ms later; on failure it
returns the error from the update library and stays on the old firmware.

```bash
curl -s -F 'firmware=@build/PcPower_BLE.bin' http://pcpower.local/update
```

If the board has no spare app slot the upload is refused at the start, with an error explaining
that it needs reflashing over USB with a two-slot partition table; check `ota.capable` in
`GET /api/status` before offering an update. Any failure — refused, interrupted, or a bad write —
resumes scanning and clears the trigger inhibit before replying, so a failed update never leaves
the board awake but deaf.

Progress is also visible in `GET /api/status` under `ota`. Upload the plain
`PcPower_BLE.bin`, **not** `PcPower_BLE.merged.bin` — the merged image includes the bootloader and
partition table and is only for flashing over USB.
