// display_hw.h — the panel, and nothing else.
//
// Wraps TFT_eSPI so the rest of the firmware never touches the driver directly.
// Derived from the bring-up of this exact board for the obd-gauge-cluster
// project, with the LVGL and touch layers removed: this design draws directly
// and has no input device.
#pragma once
#include <stdint.h>

#ifdef ARDUINO
#include <TFT_eSPI.h>

// Landscape, USB on the right. Declared as plain constants because TFT_eSPI's
// ILI9488_Defines.h defines TFT_WIDTH/TFT_HEIGHT as portrait 320x480, and using
// those would silently transpose the whole layout.
inline constexpr int SCREEN_W = 480;
inline constexpr int SCREEN_H = 320;

namespace display {

// Initialise the backlight PWM and the panel. Call once, early.
void begin();

// 0..100 percent. Backed by LEDC on GPIO27 — the pin is dimmable even though
// Elecrow only documents it as on/off, which is what makes night mode possible.
void setBacklight(uint8_t pct);

// Is the panel being pressed right now?
//
// Pressure only — no coordinates, so no calibration is involved. The screen is
// a wake button, not a pointer.
//
// Deliberately not tft.getTouch(): that validates two consecutive position
// samples and rejects a finger that moves between them, which makes it
// unreliable for a casual tap.
bool touched();

// The underlying driver, for the renderer.
TFT_eSPI& tft();

}  // namespace display
#endif  // ARDUINO
