// display_hw.h — the panel, and nothing else.
//
// Wraps TFT_eSPI so the rest of the firmware never touches the driver directly.
// Derived from the bring-up of the CrowPanel for the obd-gauge-cluster project,
// with the LVGL and touch layers removed: this design draws directly and uses
// touch only as a wake button.
//
// Two board families (layout.h has the list). The Elecrow CrowPanel 3.5" reads
// its XPT2046 resistive touch through TFT_eSPI; the QDtech ES3C28P 2.8"
// (BOARD_ES3C28P) has an FT6336G capacitive controller on its own I2C bus. Pins
// arrive as build flags from platformio.ini.
#pragma once
#include <stdint.h>

// SCREEN_W / SCREEN_H and the rest of the per-board geometry.
#include "layout.h"

#ifdef ARDUINO
#include <TFT_eSPI.h>

namespace display {

// Initialise the backlight PWM, the panel and, on the ES3C28P, the touch
// controller. Call once, early, after Serial.begin(): it logs.
void begin();

// 0..100 percent. Backed by LEDC on BACKLIGHT_PIN (GPIO27 on the CrowPanel,
// GPIO45 on the ES3C28P). Both pins are dimmable, which is what makes night
// mode possible.
void setBacklight(uint8_t pct);

// Is the panel being pressed right now?
//
// No coordinates, so no calibration is involved. The screen is a wake button,
// not a pointer.
//
// CrowPanel: pressure only, and deliberately not tft.getTouch(): that
// validates two consecutive position samples and rejects a finger that moves
// between them, which makes it unreliable for a casual tap.
// ES3C28P: the FT6336G's touch-point count. Any I2C failure reads as false.
bool touched();

// The underlying driver, for the renderer.
TFT_eSPI& tft();

}  // namespace display
#endif  // ARDUINO
