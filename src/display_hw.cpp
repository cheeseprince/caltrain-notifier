#ifdef ARDUINO
#include "display_hw.h"

#include <Arduino.h>
#ifdef BOARD_ES3C28P
#include <Wire.h>
#endif

namespace {

TFT_eSPI g_tft;

// LEDC channel 0 at 5 kHz, 8-bit. These values were used on the CrowPanel in
// the obd-gauge-cluster bring-up; 5 kHz is above audible and well within what
// the backlight driver follows cleanly. The ES3C28P's own board support package
// drives its backlight at the same 5 kHz.
constexpr int BL_CHANNEL = 0;
constexpr int BL_FREQ_HZ = 5000;
constexpr int BL_RESOLUTION = 8;

#ifdef BOARD_ES3C28P
// --- FT6336G capacitive touch (QDtech ES3C28P) --------------------------------
// Read directly over I2C, not through TFT_eSPI. The address and registers are
// the vendor driver's (FT6336-arduino/FT6336.h in QDtech's demo package):
// register 0x02, TD_STATUS, holds the number of touch points in its low
// nibble, and 0xA8 reads back FocalTech's vendor ID, 0x11.
//
// A capacitive controller reports a clean count rather than a pressure that
// dips as a finger settles, so unlike the XPT2046 below there is no hysteresis
// here. pollWake() in main.cpp already edge-detects the result.
constexpr uint8_t FT6336_ADDR = 0x38;
constexpr uint8_t FT6336_REG_TD_STATUS = 0x02;
constexpr uint8_t FT6336_REG_VENDOR_ID = 0xA8;

// Read one register. False on any I2C failure, so a missing or wedged
// controller reads as "not touched" rather than as a press that never ends.
bool ft6336Read(uint8_t reg, uint8_t* out) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(FT6336_ADDR, (uint8_t)1) != 1) return false;
  *out = (uint8_t)Wire.read();
  return true;
}

// Reset timing is the vendor driver's: high, low for 20 ms, high, then 500 ms
// for the controller to come up before it answers on the bus. The backlight is
// still dark at this point, so the wait is invisible.
void touchBegin() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, HIGH);
  delay(20);
  digitalWrite(TOUCH_RST, LOW);
  delay(20);
  digitalWrite(TOUCH_RST, HIGH);
  delay(500);

  // Diagnostic only. Boot carries on either way: the sign is still useful
  // without tap-to-wake, and the BOOT button still lights it.
  uint8_t id = 0;
  if (ft6336Read(FT6336_REG_VENDOR_ID, &id)) {
    Serial.printf("[TOUCH] FT6336 id=0x%02X\n", id);
  } else {
    Serial.println("[TOUCH] no response");
  }
}
#else
// --- XPT2046 resistive touch (Elecrow CrowPanel), through TFT_eSPI -------------
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
#endif

}  // namespace

namespace display {

void begin() {
  // Backlight first, and dark, so the panel's power-on noise is never shown.
  // It is raised once the first frame has been drawn.
  //
  // Attached to BACKLIGHT_PIN, not TFT_BL: if TFT_eSPI ever saw a TFT_BL
  // definition, its init() below would call pinMode() on that pin and detach
  // this LEDC channel right back off it, silently turning it back into a
  // plain GPIO and making every ledcWrite() below a no-op (confirmed on the
  // ES3C28P by register read, 2026-09-12 — see platformio.ini).
  ledcSetup(BL_CHANNEL, BL_FREQ_HZ, BL_RESOLUTION);
  ledcAttachPin(BACKLIGHT_PIN, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, 0);

  g_tft.init();
  g_tft.setRotation(layout::PANEL_ROTATION);  // landscape; per board in layout.h
  g_tft.fillScreen(TFT_BLACK);

#ifdef BOARD_ES3C28P
  touchBegin();
#endif
}

void setBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(BL_CHANNEL, (pct * 255) / 100);
}

bool touched() {
#ifdef BOARD_ES3C28P
  uint8_t status = 0;
  if (!ft6336Read(FT6336_REG_TD_STATUS, &status)) return false;
  // The controller tracks at most two points; any other count is not a press.
  const uint8_t points = status & 0x0F;
  return points >= 1 && points <= 2;
#else
  const uint16_t z = g_tft.getTouchRawZ();
  if (g_pressed) {
    if (z < TOUCH_RELEASE) g_pressed = false;
  } else if (z > TOUCH_PRESS) {
    g_pressed = true;
  }
  return g_pressed;
#endif
}

TFT_eSPI& tft() { return g_tft; }

}  // namespace display
#endif  // ARDUINO
