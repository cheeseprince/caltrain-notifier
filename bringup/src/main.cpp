// ---------------------------------------------------------------------------
// Bring-up smoke test for the Elecrow CrowPanel 3.5" HMI (ESP32-WROVER-B,
// ILI9488 SPI, XPT2046 resistive touch, 480x320).
//
// Throwaway sketch — proves the board + toolchain before any real firmware:
//   1) USB-UART serial banner (chip, cores, flash, PSRAM)
//   2) Backlight on + ILI9488 color cycle drawn edge-to-edge with labels
//   3) XPT2046 resistive touch: raw x/y/z printed to serial AND a dot drawn
//      where you press, so you can eyeball that touch maps to the screen.
//
// Pins come from platformio.ini build_flags (TFT_eSPI User_Setup injected
// there), verified against Elecrow's official repo for this board.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();  // config pulled from build_flags

// 480x320 landscape
static const int SCREEN_W = 480;
static const int SCREEN_H = 320;

// Color cycle for the panel test. 16-bit RGB565 values.
struct ColorStep { uint16_t color; const char* name; uint16_t textColor; };
static const ColorStep CYCLE[] = {
    {TFT_RED,    "RED",    TFT_WHITE},
    {TFT_GREEN,  "GREEN",  TFT_BLACK},
    {TFT_BLUE,   "BLUE",   TFT_WHITE},
    {TFT_WHITE,  "WHITE",  TFT_BLACK},
    {TFT_BLACK,  "BLACK",  TFT_WHITE},
    {TFT_CYAN,   "CYAN",   TFT_BLACK},
    {TFT_ORANGE, "AMBER",  TFT_BLACK},  // night-theme color we'll use later
};
static const int N_CYCLE = sizeof(CYCLE) / sizeof(CYCLE[0]);

// Draw one full-screen color with a centered label and an edge frame. The frame
// proves the controller addresses all four edges (no off-by-one window bug).
static void drawColorScreen(const ColorStep& step) {
    tft.fillScreen(step.color);
    tft.drawRect(0, 0, SCREEN_W, SCREEN_H, step.textColor);          // outer edge
    tft.drawRect(4, 4, SCREEN_W - 8, SCREEN_H - 8, step.textColor);  // inner edge
    tft.setTextColor(step.textColor, step.color);
    tft.setTextDatum(MC_DATUM);  // middle-center
    tft.drawString(step.name, SCREEN_W / 2, SCREEN_H / 2, 4);
}

void setup() {
    Serial.begin(115200);
    delay(300);  // let USB-UART settle

    Serial.println();
    Serial.println("=== Elecrow 3.5\" CrowPanel bring-up smoke test ===");
    Serial.printf("[CHIP]  model=%s  rev=%d  cores=%d\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
    Serial.printf("[CLOCK] cpu=%u MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("[FLASH] size=%u bytes\n", ESP.getFlashChipSize());
    // WROVER-B has PSRAM; a non-zero size proves it's wired + the build enabled it.
    size_t psram = ESP.getPsramSize();
    if (psram > 0) Serial.printf("[PSRAM] OK total=%u bytes\n", (unsigned)psram);
    else           Serial.println("[PSRAM] none reported (ok for bring-up; enable BOARD_HAS_PSRAM later)");

    // Backlight: drive GPIO27 high explicitly so the panel is lit even if the
    // TFT_eSPI BL handling differs by version. (PWM dimming comes later.)
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    Serial.println("[BL]    backlight ON (GPIO27 high)");

    tft.init();
    tft.setRotation(1);  // landscape, USB on the right; flip to 3 if upside down
    Serial.println("[DISPLAY] tft.init() done");

    // One quick color cycle so you can confirm the panel renders cleanly.
    for (int i = 0; i < N_CYCLE; i++) {
        Serial.printf("[DISPLAY] color %d/%d: %s\n", i + 1, N_CYCLE, CYCLE[i].name);
        drawColorScreen(CYCLE[i]);
        delay(700);
    }

    // Land on a dark "touch test" screen.
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("TOUCH TEST", SCREEN_W / 2, 8, 4);
    tft.drawString("press anywhere - watch serial", SCREEN_W / 2, 44, 2);
    Serial.println("[TOUCH] ready - press the screen; raw x/y/z prints below");
}

void loop() {
    static unsigned long lastBeat = 0;
    uint16_t x = 0, y = 0;

    // Raw read straight from the XPT2046 (no calibration needed to prove it's
    // alive). z is pressure-ish; rises when the screen is pressed.
    uint16_t z = tft.getTouchRawZ();

    // Heartbeat: print z once a second NO MATTER WHAT, so we can tell the touch
    // controller apart from a dead one. Idle z should sit near 0 and JUMP when
    // you press FIRMLY (this is a resistive panel — press hard, use a fingernail
    // or stylus; a light capacitive-style tap reads nothing).
    if (millis() - lastBeat > 1000) {
        lastBeat = millis();
        Serial.printf("[BEAT] idle/raw z=%u  (press FIRMLY to see this jump)\n", z);
    }

    if (z > 80) {  // low threshold so even a light press registers
        tft.getTouchRaw(&x, &y);
        Serial.printf("[TOUCH] z=%4u  raw_x=%4u  raw_y=%4u\n", z, x, y);

        // Map raw XPT2046 range (~300..3800) to screen for a visual dot. Rough
        // mapping just to confirm touch tracks position; real calibration
        // happens in the firmware touch HAL.
        int sx = map((int)x, 300, 3800, 0, SCREEN_W - 1);
        int sy = map((int)y, 300, 3800, 0, SCREEN_H - 1);
        sx = constrain(sx, 0, SCREEN_W - 1);
        sy = constrain(sy, 0, SCREEN_H - 1);
        tft.fillCircle(sx, sy, 4, TFT_GREEN);
    }
    delay(20);
}
