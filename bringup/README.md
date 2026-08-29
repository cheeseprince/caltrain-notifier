# Elecrow 3.5" bring-up smoke test — flash from the Mac

Throwaway sketch to prove the board + toolchain **before** any real firmware.
Verifies: flash/boot → USB-UART serial → PSRAM → backlight + ILI9488 480×320
panel (color cycle) → XPT2046 resistive touch (raw coords). Self-contained:
only depends on **TFT_eSPI** (pins injected via `platformio.ini`, no
`User_Setup.h` edit).

## Flash it

From this `bringup/` folder, board on USB-C:

-=-=-=-=-=-=-=-
```bash
cd ~/gmc_obd/firmware/bringup
pio run -e bringup -t upload -t monitor
```
-=-=-=-=-=-=-=-

`-t monitor` opens serial at 115200 right after upload (Ctrl-C to exit).
First build downloads the ESP32 toolchain + TFT_eSPI (~couple min).

### Board revision 2.0 vs 2.2 (important)
Elecrow shipped two revs that **swap MISO ↔ TOUCH_CS** (12 ↔ 33). The default
env is **v2.0**. The display works on both revs; only touch cares.

- Screen lights + cycles colors, **but touch is dead** → you have **v2.2**:
  -=-=-=-=-=-=-=-
  ```bash
  pio run -e bringup_v22 -t upload -t monitor
  ```
  -=-=-=-=-=-=-=-
- The silkscreen usually prints the rev too.

### If upload can't find / enter the board
- Prefer the `cu.` port on macOS: `--upload-port /dev/cu.usbserial-2130`
  (the `tty.` variant can hang on carrier-detect).
- Install the USB-UART driver if macOS doesn't enumerate it (CP210x or CH340).
- Force download mode: hold **BOOT/IO0**, tap **EN/RST**, release BOOT, retry.

## What you should see

| Output | Means |
|:--|:--|
| Serial banner: chip `ESP32`, cores, flash, **PSRAM total ≠ 0** | MCU + PSRAM good |
| `[BL] backlight ON` | GPIO27 driving the backlight |
| `[DISPLAY] tft.init() done` | ILI9488 init accepted |
| Screen cycles RED→GREEN→BLUE→WHITE→BLACK→CYAN→AMBER with labels + frame | panel + backlight + full-area addressing good |
| Land on "TOUCH TEST" screen | render pipeline good |
| Press screen → `[TOUCH] z=.. raw_x=.. raw_y=..` + green dot under finger | touch controller alive + tracking |

**Report back:** the serial banner (esp. the PSRAM line), whether the color
cycle renders cleanly edge-to-edge, and whether touch prints coords. If colors
look wrong (e.g. red/blue swapped) note that — ILI9488 color order is a one-flag
fix. That tells me exactly what's solid before the real firmware.

## Notes
- Touch x/y is **raw** XPT2046 here (rough screen mapping for the dot). Per-unit
  calibration happens later in the firmware touch HAL.
- This folder is intentionally separate from `firmware/` so it pulls in **none**
  of the unfinished HAL / LVGL / OBD code.
- If the image is upside-down, change `tft.setRotation(1)` → `3` in `src/main.cpp`.
