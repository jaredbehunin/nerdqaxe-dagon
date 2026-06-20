# Troubleshooting & build notes

How this fork came to exist, what broke along the way, and how to recover if you
break yours. This is the technical companion to the design write-up in
[`blog/index.html`](blog/index.html).

---

## 1. Which panel do you have?

The NerdQAxe++ ships with two different displays depending on the seller:

| Panel | Size / res | Driver | Stock firmware |
| --- | --- | --- | --- |
| **Small** (stock) | 1.9" 170×320 | ST7789 | Looks correct |
| **Big** (this fork) | 3.5" 480×320 | ST7796, i80 8-bit parallel | Looks small / shifted / wrong color |

The "big screen" units are sold on AliExpress (e.g. by *yysluping*). If your
display shows a small, off-center, or color-garbled image on stock firmware, you
have the big ST7796 panel and this firmware is for you.

> The big-screen sellers ship **PyInstaller-wrapped factory binaries only** and
> never published source — their "fork" is just a README. That's why this fork
> rebuilds the fix from upstream source instead of patching their binary.

---

## 2. The brick: don't OTA the stock web `www.bin` onto a big-screen unit

**What happened to me:** the first thing I tried was uploading firmware through
the device's web UI (the OTA update for `esp-miner.bin` + `www.bin`). On the
big-screen unit this crashed the device into a boot loop — the display init for
the wrong panel wedges early in boot, and a half-applied OTA leaves you with no
working web UI to retry from.

**Lesson:** for a panel/display change, don't rely on OTA. Flash over **USB**
with a known-good **factory image** so the whole flash (bootloader + partitions +
app) is consistent. Keep a stock factory binary on hand as your recovery image
*before* you start experimenting.

---

## 3. Recovery — getting an unresponsive device back

The ESP32-S3 can always be re-flashed over USB regardless of firmware state.

1. Connect the device over USB. On Linux it enumerates as `/dev/ttyACM0` (or
   `ttyACM1`). If your user isn't in the `dialout` group:
   ```bash
   sudo chmod 666 /dev/ttyACM0
   ```
2. If the device won't enter download mode on its own, hold the **BOOT** button
   while resetting (press RESET, or replug USB) to force the serial bootloader.
3. Re-flash a **factory** image at offset `0x0` (this restores bootloader +
   partition table + app together):
   ```bash
   esptool --chip esp32s3 --port /dev/ttyACM0 -b 460800 \
     --before default_reset --after hard_reset write-flash \
     --flash_mode dio --flash_size 16MB --flash_freq 80m \
     0x0 esp-miner-factory-NerdQAxe++-<version>.bin
   ```
   (Or use `bitaxetool`/`./docker/bitaxetool.sh` as in the build docs.)

### After a full `erase_flash`: AP mode

A full chip erase wipes NVS, which includes your Wi-Fi credentials. The device
comes back up as its own access point:

- SSID: **`NerdAxe_XXXX`** (last hex digits of the MAC)
- IP: **`192.168.4.1`**

Reconnect by joining that AP and re-entering Wi-Fi in the web UI, or push config
over the API:

```bash
# replace <miner-ip> with the device address (192.168.4.1 in AP mode)
curl -X PATCH http://<miner-ip>/api/system \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"YOUR_WIFI","wifiPass":"YOUR_PASS"}'
curl -X POST http://<miner-ip>/api/system/restart
```

### Incremental dev flashing (don't wipe Wi-Fi every time)

Once you have a working device, flash **only the app partition** so NVS (Wi-Fi,
pool, lifetime counters) survives:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 -b 460800 \
  --before default_reset --after hard_reset write-flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x10000 build/esp-miner.bin
```

---

## 4. The display fix (480×320 ST7796)

Getting the big panel to render correctly took three layers of fixes, all in
`main/displays/`:

### 4a. Panel geometry & driver

In `displayDriver.h` / `displayDriver.cpp`:

- `TDISPLAYS3_LCD_H_RES = 480`, `TDISPLAYS3_LCD_V_RES = 320`
- Use the **ST7796** panel driver, not ST7789:
  - add `espressif/esp_lcd_st7796` to `main/idf_component.yml`
  - `#include "esp_lcd_st7796.h"` and `esp_lcd_new_panel_st7796(...)`
- `esp_lcd_panel_set_gap(panel, 0, 0)` — the stock `(0, 35)` gap is the
  centering offset for the small 170-in-240 panel; the big panel needs `0`.
- Mirror: `(false, false)` normal, `(true, true)` for a flipped install.
- `rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR` — this panel is **BGR**.

### 4b. Color byte order — the "everything is blue" bug

**Symptom:** the whole UI comes up with a heavy blue cast, or smooth gradients
show rainbow banding.

The big panel uses an 8-bit parallel (i80) interface, which needs the 16-bit
color bytes swapped. In `lv_conf.h`:

```c
#define LV_COLOR_16_SWAP 1
```

With `SWAP=1`, your **theme image assets must be encoded big-endian** (high byte
first) to match. The repo's stock `convert_single.py` writes the **wrong** byte
order — use [`convert_le.py`](convert_le.py) in this repo, which emits big-endian
`LV_IMG_CF_TRUE_COLOR` RGB565.

### 4c. How to diagnose color bugs: use a grey ramp, not a flat color

This is the single most useful debugging trick here. **Flat color blocks hide
byte-order bugs** — a solid red can look fine even when the encoding is wrong.
Test with:

- a **grey ramp** (black→white gradient): if it shows rainbow banding, your
  byte order is wrong (`LV_COLOR_16_SWAP` / asset encoding mismatch).
- **R / G / B flat swatches**: if red and blue are swapped, your channel order
  is wrong (RGB vs BGR — fix `rgb_ele_order`).

(`theme_build/testcard.png` is generated for exactly this.)

### 4d. Panel quirk: don't use true black

This panel renders pure/near-black regions splotchy and uneven. Keep a dark
floor (around `#100a0c`, minimum channel ~16) instead of true black for smooth
dark areas.

---

## 5. "Time in Oblivion" — persistent uptime

The lifetime uptime counter is stored in NVS so it survives reboots. Because a
device can reconnect / reset its session counter, the value is **delta-
accumulated**: on each update the firmware adds the elapsed delta to the stored
total rather than overwriting it, so reconnects don't reset your lifetime number.
The same delta-accumulation pattern backs the lifetime accepted-shares and the
blocks-found counter.

If you flash a factory image at `0x0` or run a full `erase_flash`, these NVS
counters reset — use the incremental app-only flash (section 3) to preserve them.

---

## 6. Flash budget

The app partition is 4 MB and the themed build runs it tight (~7% free). If you
add assets and the build overflows, the lava animation frame count is the easiest
lever (it was reduced from 12 to 10 frames to fit the block-climax image). Prefer
property animations over additional image frames.
