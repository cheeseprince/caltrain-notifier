#ifdef ARDUINO
#include "display_hw.h"

#include <Arduino.h>

namespace {

TFT_eSPI g_tft;

// LEDC channel 0 at 5 kHz, 8-bit. These values were used on this panel in the
// obd-gauge-cluster bring-up; 5 kHz is above audible and well within what the
// backlight driver follows cleanly.
constexpr int BL_CHANNEL = 0;
constexpr int BL_FREQ_HZ = 5000;
constexpr int BL_RESOLUTION = 8;

// Touch pressure thresholds, taken from this panel's bring-up log rather than
// guessed: idle floated at z = 5..20, and real presses read z = 320..1582.
// One spurious z = 141 sample appeared with a nonsense coordinate, so the entry
// threshold sits above that and still well below the lightest real tap.
//
// Hysteresis: a press has to exceed TOUCH_PRESS to register, then has to fall
// below TOUCH_RELEASE to clear. Contact pressure dips as a finger settles, and
// without the gap a single tap would read as several.
constexpr uint16_t TOUCH_PRESS = 250;
constexpr uint16_t TOUCH_RELEASE = 80;

bool g_pressed = false;

}  // namespace

namespace display {

void begin() {
  // Backlight first, and dark, so the panel's power-on noise is never shown.
  // It is raised once the first frame has been drawn.
  ledcSetup(BL_CHANNEL, BL_FREQ_HZ, BL_RESOLUTION);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, 0);

  g_tft.init();
  g_tft.setRotation(1);  // landscape; 3 would be upside-down
  g_tft.fillScreen(TFT_BLACK);
}

void setBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(BL_CHANNEL, (pct * 255) / 100);
}

bool touched() {
  const uint16_t z = g_tft.getTouchRawZ();
  if (g_pressed) {
    if (z < TOUCH_RELEASE) g_pressed = false;
  } else if (z > TOUCH_PRESS) {
    g_pressed = true;
  }
  return g_pressed;
}

TFT_eSPI& tft() { return g_tft; }

}  // namespace display
#endif  // ARDUINO
