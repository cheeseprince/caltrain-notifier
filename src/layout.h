// layout.h — where things go on each supported panel, and at what size.
//
// render.cpp decides WHAT is drawn and in which colour; this file decides
// WHERE, per board:
//
//   (default)        Elecrow CrowPanel 3.5"   480x320   caltrain, caltrain_v20
//   BOARD_ES3C28P    QDtech ES3C28P 2.8"      320x240   caltrain_es3c28p
//
// Both panels draw with TFT_eSPI's fixed-size bitmap fonts, which do not
// scale, so a proportional layout is not an option: every 2.8" position below
// was chosen for that panel and checked against TFT_eSPI's own glyph width
// tables (Fonts/Font16.c, Font32rle.c, Font64rle.c). The measurements are in
// the design spec, not the repo.
//
// Pure constants with no Arduino types, so nothing here needs a board to
// compile, and tools/gen_screenshot.py reads the splash text straight out of
// this file. That script also copies the geometry: change one, change both.
#pragma once
#include <cstdint>

namespace layout {

// Fonts. TFT_eSPI's built-ins: 2 is ~16px, 4 is ~26px, 6 is ~48px and covers
// digits, colon and the a/p of an am/pm clock — which is exactly the header
// clock and the big countdown, and nothing else.
inline constexpr uint8_t FONT_SMALL = 2;
inline constexpr uint8_t FONT_MED   = 4;
inline constexpr uint8_t FONT_BIG   = 6;

// Splash attribution. On the SPLASH rather than buried in a menu because this
// screen is guaranteed to be seen — every boot, by whoever owns the sign and
// by anyone who happens to be looking at it — and because a device that
// displays another organisation's data ought to say whose data it is and whose
// product it is not.
//
// Wording tracks ATTRIBUTION.md; if one changes the other should too. The two
// arrays carry IDENTICAL words, wrapped to each panel's width at FONT_SMALL.
// Both are always defined, under fixed names, so gen_screenshot.py can read
// either; kSplashAttribution below picks one per board. An empty string is a
// blank line.
inline constexpr const char* kSplashAttributionWide[] = {
    "Not affiliated with, endorsed by, or sponsored by",
    "Caltrain or the Peninsula Corridor Joint Powers Board.",
    "\"Caltrain\" is their trademark, used only to name",
    "the service whose departures this sign displays.",
    "",
    "Live data: 511 SF Bay   Schedule: Caltrain GTFS",
    "This firmware: MIT licensed, no warranty.",
};
// Widest line 275 px of the 2.8" panel's 300.
inline constexpr const char* kSplashAttributionNarrow[] = {
    "Not affiliated with, endorsed by, or",
    "sponsored by Caltrain or the Peninsula",
    "Corridor Joint Powers Board. \"Caltrain\" is",
    "their trademark, used only to name the",
    "service whose departures this sign displays.",
    "",
    "Live data: 511 SF Bay",
    "Schedule: Caltrain GTFS",
    "This firmware: MIT licensed, no warranty.",
};

#ifdef BOARD_ES3C28P
// ---- QDtech ES3C28P 2.8" ------------------------------------------------------
// Landscape 320x240. Plain constants rather than TFT_WIDTH/TFT_HEIGHT, which
// the ILI9341 driver defines as portrait.
inline constexpr int SCREEN_W = 320;
inline constexpr int SCREEN_H = 240;
// Confirmed against the case orientation at bring-up (1 or 3).
inline constexpr uint8_t PANEL_ROTATION = 1;

// 6 px frame: 1.9% of the width, against the 3.5"'s 8 px at 1.7%. Inner area
// 308 x 228.
inline constexpr int BORDER = 6;

// Header: the route and the clock both in the small font — Alan's call at the
// 2026-09-12 preview review. A 35 px clock leaves the route 257 px, and with
// the short names in station_label.h all 870 station pairs fit on one line
// (tightest: 19 px clear of the clock). Wrapping stays enabled as a safety
// net for a future, longer station name.
inline constexpr int HEADER_H = 52;
inline constexpr uint8_t CLOCK_FONT = FONT_SMALL;
inline constexpr int CLOCK_DY = 9;           // same top edge as the route
inline constexpr int CLOCK_PAD_EXTRA = 4;
inline constexpr int COL_RIGHT_INSET = 6;
inline constexpr int ROUTE_CLOCK_GAP = 16;
inline constexpr uint8_t ROUTE_FONT_BIG = FONT_SMALL;
inline constexpr int ROUTE_BIG_DY = 9;
inline constexpr int ROUTE_SMALL_DY = 9;
inline constexpr bool ROUTE_WRAP = true;
inline constexpr int ROUTE_LINE1_DY = 2;     // origin
inline constexpr int ROUTE_LINE2_DY = 18;    // "> destination", ends where the note begins
inline constexpr int NOTE_DY = 34;
inline constexpr bool SHORT_STATION_NAMES = true;

// Departure rows, 58 px each. The countdown is in font 4 on the time line —
// also Alan's call — so "min", the time and the train sit further left: three
// digits are 42 px, and "min" ends 13 px before the time column.
inline constexpr uint8_t COUNT_FONT = FONT_MED;
inline constexpr int COUNT_R = 46;
inline constexpr int COUNT_PAD = 44;
inline constexpr int COUNT_DY = 17;          // centred on the time line
inline constexpr int MIN_LABEL_DX = 4;
inline constexpr int MIN_LABEL_PAD = 22;
inline constexpr int MIN_LABEL_DY = 13;      // top edge, level with the digits' lower half
inline constexpr int COL_INFO_X = 82;
inline constexpr int WHEN_DY = 17;
inline constexpr int WHEN_PAD = 70;
// The status stays in the small font so it reads as secondary to the time.
inline constexpr uint8_t ROW_STATUS_FONT = FONT_SMALL;
inline constexpr int INFO_DY = 43;
inline constexpr int INFO_PAD_INSET = 6;

// Splash: title, nine attribution slots at 17 px, boot note.
inline constexpr int SPLASH_TITLE_Y = 26;
inline constexpr int SPLASH_ATTR_Y  = 50;
inline constexpr int SPLASH_ATTR_DY = 17;
inline constexpr int SPLASH_NOTE_Y  = 220;
inline constexpr const char* const* kSplashAttribution = kSplashAttributionNarrow;
inline constexpr int kSplashAttributionLines =
    (int)(sizeof(kSplashAttributionNarrow) / sizeof(kSplashAttributionNarrow[0]));

// Checklist: four 42 px rows. Widest value ("65535 / 65535 bytes  100%") is
// 182 px of the 204 left of the value column.
inline constexpr int STEP_HEAD_DY  = 8;
inline constexpr int STEP_TOP_DY   = 46;
inline constexpr int STEP_ROW_H    = 42;
inline constexpr int STEP_MARK_DX  = 20;
inline constexpr int STEP_LABEL_DX = 36;
inline constexpr int STEP_VALUE_DX = 100;

// Setup portal: the same seven lines in 80 px less height.
inline constexpr int PORTAL_TITLE_DY   = 4;
inline constexpr int PORTAL_BODY_DY    = 40;
inline constexpr int PORTAL_SSID_DY    = 18;
inline constexpr int PORTAL_PASSLBL_DY = 48;
inline constexpr int PORTAL_PASS_DY    = 66;
inline constexpr int PORTAL_OPENLBL_DY = 96;
inline constexpr int PORTAL_URL_DY     = 114;
inline constexpr int PORTAL_FOOTER_UP  = 20;

// Firmware update screen.
inline constexpr int UPD_TITLE_Y = 40;
inline constexpr int UPD_VER_Y   = 76;
inline constexpr int UPD_BAR_X   = 34;
inline constexpr int UPD_BAR_Y   = 98;
inline constexpr int UPD_BAR_W   = 252;
inline constexpr int UPD_BAR_H   = 24;
inline constexpr int UPD_PCT_Y   = 140;
inline constexpr int UPD_STEP_Y  = 170;
inline constexpr int UPD_WARN_Y  = 200;

#else
// ---- Elecrow CrowPanel 3.5" -----------------------------------------------------
// Landscape, USB on the right. Plain constants because TFT_eSPI's
// ILI9488_Defines.h defines TFT_WIDTH/TFT_HEIGHT as portrait 320x480, and using
// those would silently transpose the whole layout.
inline constexpr int SCREEN_W = 480;
inline constexpr int SCREEN_H = 320;
inline constexpr uint8_t PANEL_ROTATION = 1;  // landscape; 3 would be upside-down

inline constexpr int BORDER = 8;             // urgency frame thickness

// Header: route and clock on one line at the same size, the route dropping to
// the small font only when it would collide with the clock.
inline constexpr int HEADER_H = 52;
inline constexpr uint8_t CLOCK_FONT = FONT_MED;
inline constexpr int CLOCK_DY = 6;
inline constexpr int CLOCK_PAD_EXTRA = 8;
inline constexpr int COL_RIGHT_INSET = 8;
inline constexpr int ROUTE_CLOCK_GAP = 24;
inline constexpr uint8_t ROUTE_FONT_BIG = FONT_MED;
inline constexpr int ROUTE_BIG_DY = 6;       // same top edge as the clock
inline constexpr int ROUTE_SMALL_DY = 11;    // nudged down to stay optically level
inline constexpr bool ROUTE_WRAP = false;    // never needed at 480 px
inline constexpr int ROUTE_LINE1_DY = 0;     // unused while ROUTE_WRAP is false
inline constexpr int ROUTE_LINE2_DY = 0;
inline constexpr int NOTE_DY = 34;
inline constexpr bool SHORT_STATION_NAMES = false;

// Departure rows, 84 px each. The big countdown is centred in the row with
// "min" just below its middle — ROW_H / 2 - 2 and ROW_H / 2 + 4 when these were
// literals in render.cpp, which pins them to that.
inline constexpr uint8_t COUNT_FONT = FONT_BIG;
inline constexpr int COUNT_R = 108;
inline constexpr int COUNT_PAD = 100;
inline constexpr int COUNT_DY = 40;
inline constexpr int MIN_LABEL_DX = 6;
inline constexpr int MIN_LABEL_PAD = 34;
inline constexpr int MIN_LABEL_DY = 46;
inline constexpr int COL_INFO_X = 156;
inline constexpr int WHEN_DY = 28;
inline constexpr int WHEN_PAD = 110;
inline constexpr uint8_t ROW_STATUS_FONT = FONT_MED;
inline constexpr int INFO_DY = 60;
inline constexpr int INFO_PAD_INSET = 8;

// Splash
inline constexpr int SPLASH_TITLE_Y = 74;
inline constexpr int SPLASH_ATTR_Y  = 126;   // first attribution line
inline constexpr int SPLASH_ATTR_DY = 20;    // line pitch, FONT_SMALL
inline constexpr int SPLASH_NOTE_Y  = 282;
inline constexpr const char* const* kSplashAttribution = kSplashAttributionWide;
inline constexpr int kSplashAttributionLines =
    (int)(sizeof(kSplashAttributionWide) / sizeof(kSplashAttributionWide[0]));

// Checklist: four rows of 44 px.
inline constexpr int STEP_HEAD_DY  = 24;
inline constexpr int STEP_TOP_DY   = 84;
inline constexpr int STEP_ROW_H    = 44;
inline constexpr int STEP_MARK_DX  = 56;
inline constexpr int STEP_LABEL_DX = 84;
inline constexpr int STEP_VALUE_DX = 184;

// Setup portal
inline constexpr int PORTAL_TITLE_DY   = 10;
inline constexpr int PORTAL_BODY_DY    = 46;
inline constexpr int PORTAL_SSID_DY    = 22;
inline constexpr int PORTAL_PASSLBL_DY = 58;
inline constexpr int PORTAL_PASS_DY    = 78;
inline constexpr int PORTAL_OPENLBL_DY = 114;
inline constexpr int PORTAL_URL_DY     = 134;
inline constexpr int PORTAL_FOOTER_UP  = 24;

// Firmware update screen. Position numbers come from the OTA task brief's
// layout table.
inline constexpr int UPD_TITLE_Y = 70;
inline constexpr int UPD_VER_Y   = 115;
inline constexpr int UPD_BAR_X   = 60;
inline constexpr int UPD_BAR_Y   = 160;
inline constexpr int UPD_BAR_W   = 360;  // right edge at 420
inline constexpr int UPD_BAR_H   = 30;   // bottom edge at 190
inline constexpr int UPD_PCT_Y   = 205;
inline constexpr int UPD_STEP_Y  = 240;
inline constexpr int UPD_WARN_Y  = 265;

// Pinned to the values render.cpp used before this file existed. The 3.5"
// board is shipped and in use; tuning the 2.8" block must never move it. If
// one of these fires, an edit landed in the wrong #if branch.
static_assert(SCREEN_W == 480 && SCREEN_H == 320 && PANEL_ROTATION == 1,
              "CrowPanel panel geometry changed");
static_assert(BORDER == 8 && HEADER_H == 52 && CLOCK_DY == 6 && CLOCK_PAD_EXTRA == 8 &&
                  COL_RIGHT_INSET == 8 && ROUTE_CLOCK_GAP == 24 && ROUTE_FONT_BIG == FONT_MED &&
                  ROUTE_BIG_DY == 6 && ROUTE_SMALL_DY == 11 && !ROUTE_WRAP && NOTE_DY == 34 &&
                  !SHORT_STATION_NAMES,
              "CrowPanel header layout changed");
static_assert(COUNT_R == 108 && COUNT_PAD == 100 && MIN_LABEL_DX == 6 && MIN_LABEL_PAD == 34 &&
                  COL_INFO_X == 156 && WHEN_DY == 28 && WHEN_PAD == 110 &&
                  ROW_STATUS_FONT == FONT_MED && INFO_DY == 60 && INFO_PAD_INSET == 8,
              "CrowPanel row layout changed");
static_assert(SPLASH_TITLE_Y == 74 && SPLASH_ATTR_Y == 126 && SPLASH_ATTR_DY == 20 &&
                  SPLASH_NOTE_Y == 282 && kSplashAttributionLines == 7,
              "CrowPanel splash layout changed");
static_assert(STEP_HEAD_DY == 24 && STEP_TOP_DY == 84 && STEP_ROW_H == 44 &&
                  STEP_MARK_DX == 56 && STEP_LABEL_DX == 84 && STEP_VALUE_DX == 184,
              "CrowPanel checklist layout changed");
static_assert(PORTAL_TITLE_DY == 10 && PORTAL_BODY_DY == 46 && PORTAL_SSID_DY == 22 &&
                  PORTAL_PASSLBL_DY == 58 && PORTAL_PASS_DY == 78 && PORTAL_OPENLBL_DY == 114 &&
                  PORTAL_URL_DY == 134 && PORTAL_FOOTER_UP == 24,
              "CrowPanel portal layout changed");
static_assert(UPD_TITLE_Y == 70 && UPD_VER_Y == 115 && UPD_BAR_X == 60 && UPD_BAR_Y == 160 &&
                  UPD_BAR_W == 360 && UPD_BAR_H == 30 && UPD_PCT_Y == 205 &&
                  UPD_STEP_Y == 240 && UPD_WARN_Y == 265,
              "CrowPanel update screen layout changed");
static_assert(CLOCK_FONT == FONT_MED && COUNT_FONT == FONT_BIG && COUNT_DY == 40 &&
                  MIN_LABEL_DY == 46,
              "CrowPanel clock or countdown changed");
#endif

// ---- Checks that hold on every board ---------------------------------------------
static_assert(COUNT_R >= COUNT_PAD, "countdown padding would reach into the frame");
static_assert(COL_INFO_X >= COUNT_R + MIN_LABEL_DX + MIN_LABEL_PAD,
              "the 'min' label would overlap the departure time column");
static_assert(2 * UPD_BAR_X + UPD_BAR_W == SCREEN_W, "the update progress bar is not centred");
static_assert(STEP_LABEL_DX < STEP_VALUE_DX, "checklist label and value columns cross");

}  // namespace layout
