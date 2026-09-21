# Hardware

Board details for the [Caltrain Notifier](../README.md). The two supported
boards are compared in the README's [Hardware](../README.md#hardware) table;
this page has what you need when building for, flashing or debugging one of
them. Values were read off real units, not taken from vendor listings.

- [Elecrow CrowPanel 3.5"](#elecrow-crowpanel-35)
  - [Board revisions matter — build the one that matches your unit](#board-revisions-matter--build-the-one-that-matches-your-unit)
  - [Pins](#pins)
- [QDtech ES3C28P 2.8"](#qdtech-es3c28p-28)

## Elecrow CrowPanel 3.5"

**Elecrow CrowPanel 3.5" ESP32 HMI display** — ILI9488 480x320 SPI, XPT2046
resistive touch. Roughly $30.

- [Amazon — B0FXLB5CFL](https://www.amazon.com/dp/B0FXLB5CFL)

Also sold directly by Elecrow and through the usual electronics distributors;
any listing for the 3.5" CrowPanel with an ILI9488 should be the same board.

The vendor listing for this board is wrong in two ways that matter. Values below
were read off the unit itself, not the listing — the same `[CHIP]` line
`src/main.cpp` prints on every boot, so any owner of this board can reproduce it.

| | Listing says | Actually is |
| :--- | :--- | :--- |
| Module | ESP32-WROVER-B | **ESP32-D0WD** rev 1.01, 2 cores, 240 MHz |
| Flash | 4 MB | **8 MB** |
| PSRAM | 8 MB | **Unconfirmed** — see below |

PSRAM was never verified. The bring-up build did not enable it, so its report of
zero proves nothing either way. Nothing in this firmware needs it — the display
is drawn directly rather than through LVGL, and the largest API response
measured is about 3 KB — so `BOARD_HAS_PSRAM` is deliberately left undefined
rather than enabled on a guess.

### Board revisions matter — build the one that matches your unit

Elecrow shipped v2.0 and v2.2, which swap two pins:

| | v2.0 | v2.2 |
| :--- | :--- | :--- |
| `TFT_MISO` | 12 | 33 |
| `TOUCH_CS` | 33 | 12 |

Both pins are used. Touch wakes the screen — `display::touched()` reads the
XPT2046's raw pressure, never coordinates, so there is no calibration — and
reading the touch controller needs both `TOUCH_CS` and a real `TFT_MISO`.

So there are **two environments, and you must build the one matching your
board**:

```bash
pio run -e caltrain        # revision 2.2
pio run -e caltrain_v20    # revision 2.0
```

Flashing the wrong one leaves a working display whose screen never wakes on a
tap, with nothing on the console to say why. If tapping does nothing, you are
almost certainly on the other revision — build the other environment.

The unit this was developed on is v2.2. `bringup/` proves the display and touch
independently of the firmware, and is the fastest way to tell the revisions
apart if the silkscreen is ambiguous.

Because `TOUCH_CS` is defined, TFT_eSPI compiles its touch support in and emits
no warning about it. A `TOUCH_CS pin not defined` warning would mean the pin has
gone missing from the build flags and tap-to-wake is silently disabled.

### Pins

| Signal | GPIO |
| :--- | :--- |
| `TFT_MOSI` | 13 |
| `TFT_SCLK` | 14 |
| `TFT_CS` | 15 |
| `TFT_DC` | 2 |
| `TFT_RST` | −1 (tied to the ESP32 reset line) |
| `TFT_MISO` | **33** on v2.2, **12** on v2.0 — read back from the touch controller |
| `TOUCH_CS` | **12** on v2.2, **33** on v2.0 |
| `BACKLIGHT_PIN` | 27 — PWM-capable, which is what makes night dimming possible |
| BOOT button | 0 |

## QDtech ES3C28P 2.8"

**Hosyond / QDtech ES3C28P** — ESP32-S3, 2.8" ILI9341V 320x240 IPS over SPI,
FT6336G capacitive touch. One build environment:

```bash
pio run -e caltrain_es3c28p
```

- [Amazon — B0FKG7WRWV](https://www.amazon.com/dp/B0FKG7WRWV)
- Manufacturer pin table, dimension drawing and STEP model:
  [lcdwiki — 2.8inch ESP32-S3 Display](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display_E32C28P/E32N28P)

Read off the unit with `esptool.py flash_id`: ESP32-S3 (QFN56) rev v0.2,
**16 MB quad flash, 8 MB octal PSRAM**. The S3's SDK configuration brings that
PSRAM up at boot and adds it to the heap; nothing in this firmware relies on
it, and `BOARD_HAS_PSRAM` stays undefined.

Same firmware and features, laid out for the smaller panel (`src/layout.h`):
three departures in 58 px rows with the countdown in the same large font as the
3.5" — dropping to the medium font at 100 minutes and over, where a third
digit would reach into the frame — the delay status and the header clock in the
small font, and a route header that shows "South San Francisco" and "California
Avenue" as "S. San Francisco" and "California Ave" (`src/station_label.h`) so
every station pair fits on one line. The setup portal keeps the full names.

Touch is capacitive and read over I2C, not through TFT_eSPI, so this build prints
TFT_eSPI's `TOUCH_CS pin not defined` warning. **On this board that warning is
expected**; the `[TOUCH] FT6336 id=0x11` line at boot is what confirms
tap-to-wake.

| Signal | GPIO |
| :--- | :--- |
| `TFT_MOSI` / `TFT_SCLK` / `TFT_MISO` | 11 / 12 / 13 |
| `TFT_CS` / `TFT_DC` | 10 / 46 |
| `TFT_RST` | −1 (tied to the ESP32-S3 reset line) |
| `BACKLIGHT_PIN` | 45 — active high, PWM |
| Touch `SDA` / `SCL` / `RST` | 16 / 15 / 18 (FT6336G at I2C `0x38`; `INT` on 17 is unused) |
| BOOT button | 0 |

Bring-up on a real unit (2026-09-12) showed this IPS panel draws a negative
without colour inversion, so `caltrain_es3c28p` sets `TFT_INVERSION_ON`. Rotation
is the same landscape `setRotation(1)` as the CrowPanel, with the USB-C connector
on the right.

---
