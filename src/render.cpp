#ifdef ARDUINO
#include "render.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "display_hw.h"

// There is deliberately NO logo on any screen. An earlier version could push a
// bitmap plate here when a generated header was present; that header held
// Caltrain's mark, which is not this project's to display, so the whole path is
// gone rather than left switched off. Boot screens are text. See ATTRIBUTION.md.

namespace {

// Step, StepState and STEP_ROWS are declared in render.h, inside namespace
// render. The helpers below are part of the same screen and read badly fully
// qualified, so pull the names in rather than spelling out render:: on every
// enumerator.
using namespace render;

// --- Palette --------------------------------------------------------------
// Dark background: this sits on a desk and is looked at in passing, so the lit
// area should be the information, not the panel.
constexpr uint16_t COL_BG      = TFT_BLACK;
constexpr uint16_t COL_TEXT    = TFT_WHITE;
// Secondary text. 0x8410 — a true 50% grey — was measured on the panel as
// unreadable a few tens of degrees off dead centre, which is how this sign is
// normally seen. 0xC618 is ~78% luminance: still clearly subordinate to white,
// but it survives the viewing angle. Everything secondary uses it, so the fix
// is this one constant.
constexpr uint16_t COL_DIM     = 0xC618;

// The idle frame — boot screens, the overnight screen, and a board with nothing
// on it. This keeps the old 50% grey deliberately. COL_DIM was brightened for
// legibility of small text, but the frame is an 8px band around the whole
// panel: at 0xC618 it stops reading as "no alarm here" and starts competing
// with the urgency colours it exists to be quieter than.
constexpr uint16_t COL_FRAME_IDLE = 0x8410;
constexpr uint16_t COL_RULE    = 0x2965;  // faint row divider
constexpr uint16_t COL_GREEN   = 0x0640;
constexpr uint16_t COL_YELLOW  = 0xFE60;
constexpr uint16_t COL_RED     = 0xF800;
constexpr uint16_t COL_LATE    = 0xFD20;  // amber, for a delay figure
constexpr uint16_t COL_SCHED   = 0x05FF;  // cyan, marks a non-live row

// --- Geometry -------------------------------------------------------------
constexpr int BORDER = 8;                       // urgency frame thickness
constexpr int INNER_X = BORDER;
constexpr int INNER_Y = BORDER;
constexpr int INNER_W = SCREEN_W - 2 * BORDER;  // 464
constexpr int INNER_H = SCREEN_H - 2 * BORDER;  // 304

// The header carries the two things you read without caring about any
// particular train: where this sign is pointed, and what time it is now. They
// sit on one line at the same size, which is also the only option — font 6 has
// no letters in it, so the route name cannot be set any larger than font 4 and
// the clock comes down to match rather than towering over it.
constexpr int HEADER_H = 52;
constexpr int ROWS_Y = INNER_Y + HEADER_H;
constexpr int ROW_H = (INNER_H - HEADER_H) / BOARD_ROWS;  // 84

// Fonts. TFT_eSPI's built-ins: 2 is ~16px, 4 is ~26px, 6 is ~48px and covers
// digits, colon and the a/p of an am/pm clock — which is exactly the header
// clock and the big countdown, and nothing else.
constexpr uint8_t FONT_SMALL = 2;
constexpr uint8_t FONT_MED   = 4;
constexpr uint8_t FONT_BIG   = 6;

// Column offsets within a row.
constexpr int COL_MIN_R = INNER_X + 108;  // right edge of the big countdown
constexpr int COL_INFO  = INNER_X + 156;  // departure time, train, service
constexpr int COL_RIGHT = INNER_X + INNER_W - 8;

// --- Boot screens ---------------------------------------------------------
// Splash: the product name, then the attribution block, then a status line that
// updates as boot progresses. The attribution is on the SPLASH rather than
// buried in a menu because this screen is guaranteed to be seen — every boot,
// by whoever owns the sign and by anyone who happens to be looking at it — and
// because a device that displays another organisation's data ought to say whose
// data it is and whose product it is not.
constexpr int SPLASH_TITLE_Y = 74;
constexpr int SPLASH_ATTR_Y  = 126;   // first attribution line
constexpr int SPLASH_ATTR_DY = 20;    // line pitch, FONT_SMALL
constexpr int SPLASH_NOTE_Y  = 282;

// Kept to the panel width at FONT_SMALL. Wording tracks ATTRIBUTION.md; if one
// changes the other should too.
constexpr const char* kSplashAttribution[] = {
    "Not affiliated with, endorsed by, or sponsored by",
    "Caltrain or the Peninsula Corridor Joint Powers Board.",
    "\"Caltrain\" is their trademark, used only to name",
    "the service whose departures this sign displays.",
    "",
    "Live data: 511 SF Bay   Schedule: Caltrain GTFS",
    "This firmware: MIT licensed, no warranty.",
};
constexpr int kSplashAttributionLines =
    (int)(sizeof(kSplashAttribution) / sizeof(kSplashAttribution[0]));

// Checklist. Four rows of 44px starting below the headline, each one a marker,
// a label in the left column and a value beside it.
constexpr int STEP_HEAD_Y   = INNER_Y + 24;
constexpr int STEP_TOP      = INNER_Y + 84;
constexpr int STEP_ROW_H    = 44;
constexpr int STEP_MARK_X   = INNER_X + 56;   // centre of the marker
constexpr int STEP_LABEL_X  = INNER_X + 84;
constexpr int STEP_VALUE_X  = INNER_X + 184;
constexpr int STEP_MARK_R   = 7;              // marker radius

// Checklist markers, drawn rather than typed: the built-in fonts have no tick
// glyph, and a shape reads faster than a punctuation character anyway.
void drawMarker(int cx, int cy, StepState state) {
  TFT_eSPI& t = display::tft();
  switch (state) {
    case STEP_PENDING:
      t.drawCircle(cx, cy, STEP_MARK_R, COL_DIM);
      break;
    case STEP_ACTIVE:
      t.fillCircle(cx, cy, STEP_MARK_R, COL_TEXT);
      break;
    case STEP_DONE:
      // A tick, three overlapping lines deep so it does not read as a hairline.
      for (int o = 0; o < 3; o++) {
        t.drawLine(cx - 7, cy + o, cx - 2, cy + 5 + o, COL_GREEN);
        t.drawLine(cx - 2, cy + 5 + o, cx + 7, cy - 5 + o, COL_GREEN);
      }
      break;
    case STEP_FAILED:
      for (int o = 0; o < 3; o++) {
        t.drawLine(cx - 6, cy - 6 + o, cx + 6, cy + 6 + o, COL_RED);
        t.drawLine(cx - 6, cy + 6 - o, cx + 6, cy - 6 - o, COL_RED);
      }
      break;
  }
}

uint16_t urgencyColour(Urgency u) {
  switch (u) {
    case URGENCY_GREEN:  return COL_GREEN;
    case URGENCY_YELLOW: return COL_YELLOW;
    default:             return COL_RED;
  }
}

// --- Update screen ----------------------------------------------------------
// Geometry for updating(). Position numbers come from the task brief's layout
// table; there is no other screen this one's proportions need to match.
constexpr int UPD_TITLE_Y = 70;
constexpr int UPD_VER_Y   = 115;
constexpr int UPD_BAR_X   = 60;
constexpr int UPD_BAR_Y   = 160;
constexpr int UPD_BAR_W   = 360;  // right edge at 420
constexpr int UPD_BAR_H   = 30;   // bottom edge at 190
constexpr int UPD_PCT_Y   = 205;
constexpr int UPD_STEP_Y  = 240;
constexpr int UPD_WARN_Y  = 265;

// --- Cached state ---------------------------------------------------------
// Only fields that changed are repainted. A full fillScreen at 27 MHz takes
// long enough to be seen as a flash, and this screen redraws every second.
struct Cache {
  bool     valid;
  char     screen[12];
  uint16_t border;
  char     header[48];
  char     clock[12];
  char     note[40];
  // overnight()'s own expiry warning (F-1, 2026-08 adversarial review). Kept
  // separate from `note` above rather than reused: on the "night" screen,
  // `note` already holds the "No more trains tonight" headline, and the two
  // need to change independently. invalidate()/enter() memset the whole Cache,
  // so this resets exactly like every other field — no separate reset code.
  char     expiredNote[40];
  char     step[STEP_ROWS][64];  // "<state>|<label>|<value>", one per checklist row
  char     mins[BOARD_ROWS][8];
  char     when[BOARD_ROWS][12];
  char     info[BOARD_ROWS][40];
  char     delay[BOARD_ROWS][16];
  uint16_t minsColour[BOARD_ROWS];
  char     pct[8];  // formatted "NN" (or empty), for updating()'s bar + label
  // "vFrom  >  vTo" for updating(). Sized for the worst case: two 31-char
  // version fields (OtaRelease::version and ota_task::Progress::version are
  // both char[32]) plus the "  >  " separator (5) plus the NUL — 68 minimum;
  // rounded up for headroom.
  char     ver[72];
};
Cache g_cache;

// Largest of two fonts that renders `s` within `maxW`, else the smaller.
//
// Station names vary from "Belmont" to "South San Francisco", and the longest
// possible pairing is about 38 characters. Rather than pick a font that always
// fits the worst case — and so looks undersized for the common one — measure
// and step down only when needed.
uint8_t fontThatFits(const char* s, int maxW, uint8_t big, uint8_t small) {
  return display::tft().textWidth(s, big) <= maxW ? big : small;
}

bool changed(char* slot, size_t cap, const char* now) {
  if (strncmp(slot, now, cap - 1) == 0) return false;
  strncpy(slot, now, cap - 1);
  slot[cap - 1] = '\0';
  return true;
}

// Enter a screen, wiping the panel only when arriving from a different one.
bool enter(const char* name) {
  if (g_cache.valid && strcmp(g_cache.screen, name) == 0) return false;
  memset(&g_cache, 0, sizeof(g_cache));
  strncpy(g_cache.screen, name, sizeof(g_cache.screen) - 1);
  g_cache.valid = true;
  display::tft().fillScreen(COL_BG);
  return true;
}

// Draw the urgency frame as four bars, so the inner area is never touched.
void drawBorder(uint16_t colour) {
  if (colour == g_cache.border) return;
  g_cache.border = colour;
  TFT_eSPI& t = display::tft();
  t.fillRect(0, 0, SCREEN_W, BORDER, colour);
  t.fillRect(0, SCREEN_H - BORDER, SCREEN_W, BORDER, colour);
  t.fillRect(0, 0, BORDER, SCREEN_H, colour);
  t.fillRect(SCREEN_W - BORDER, 0, BORDER, SCREEN_H, colour);
}

// Local clock as "18:12". 24-hour throughout: it is unambiguous at a glance,
// and every time is the same width, which is what lets the fixed text padding
// erase the previous value cleanly.
void formatClock(int64_t epoch, char* out, size_t cap) {
  const time_t tt = (time_t)epoch;
  struct tm lt;
  localtime_r(&tt, &lt);
  snprintf(out, cap, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

// The same, from minutes past midnight of a service day. Values above 1440 are
// after-midnight trains, so the hour is wrapped back into 0..23.
void formatClockMin(uint16_t depMin, char* out, size_t cap) {
  snprintf(out, cap, "%02d:%02d", (depMin / 60) % 24, depMin % 60);
}

}  // namespace

namespace render {

void invalidate() { memset(&g_cache, 0, sizeof(g_cache)); }

void status(const char* headline, const char* detail) {
  TFT_eSPI& t = display::tft();
  enter("status");
  drawBorder(COL_FRAME_IDLE);

  t.setTextDatum(MC_DATUM);
  if (changed(g_cache.header, sizeof(g_cache.header), headline ? headline : "")) {
    t.setTextColor(COL_TEXT, COL_BG);
    t.setTextPadding(INNER_W);
    t.drawString(headline ? headline : "", SCREEN_W / 2, SCREEN_H / 2 - 18, 4);
  }
  if (changed(g_cache.note, sizeof(g_cache.note), detail ? detail : "")) {
    t.setTextColor(COL_DIM, COL_BG);
    t.setTextPadding(INNER_W);
    t.drawString(detail ? detail : "", SCREEN_W / 2, SCREEN_H / 2 + 20, 2);
  }
  t.setTextPadding(0);
}

void portal(const char* apSsid, const char* apPass, const char* url) {
  TFT_eSPI& t = display::tft();
  if (!enter("portal")) return;  // static screen: draw once
  drawBorder(COL_YELLOW);

  // A plain "Setup" title. This screen is the one a stranger is most likely to
  // be reading, so it says what it is in words.
  int y = INNER_Y + 2;
  t.setTextDatum(TC_DATUM);
  t.setTextColor(COL_TEXT, COL_BG);
  t.drawString("Setup", SCREEN_W / 2, y + 8, 4);
  y += 44;

  t.setTextDatum(TC_DATUM);
  t.setTextColor(COL_DIM, COL_BG);
  t.drawString("Join this WiFi network from your phone", SCREEN_W / 2, y, 2);

  t.setTextColor(COL_TEXT, COL_BG);
  t.drawString(apSsid, SCREEN_W / 2, y + 22, 4);

  t.setTextColor(COL_DIM, COL_BG);
  t.drawString("password", SCREEN_W / 2, y + 58, 2);
  t.setTextColor(COL_TEXT, COL_BG);
  t.drawString(apPass, SCREEN_W / 2, y + 78, 4);

  t.setTextColor(COL_DIM, COL_BG);
  t.drawString("then open", SCREEN_W / 2, y + 114, 2);
  t.setTextColor(COL_SCHED, COL_BG);
  t.drawString(url, SCREEN_W / 2, y + 134, 4);

  t.setTextColor(COL_DIM, COL_BG);
  t.drawString("Hold BOOT at power-on to return here later",
               SCREEN_W / 2, INNER_Y + INNER_H - 24, 2);
}

void splash(const char* detail) {
  TFT_eSPI& t = display::tft();
  const bool fresh = enter("splash");
  drawBorder(COL_FRAME_IDLE);

  // The plate and the product name never change, so they are painted once on
  // entry; only the status line underneath is rewritten as boot progresses.
  if (fresh) {
    t.setTextDatum(MC_DATUM);
    t.setTextColor(COL_TEXT, COL_BG);
    t.drawString("Caltrain Notifier", SCREEN_W / 2, SPLASH_TITLE_Y, FONT_MED);

    // Dim, not white: this has to be readable without competing with the name
    // or with the status line that changes underneath it.
    t.setTextColor(COL_DIM, COL_BG);
    for (int i = 0; i < kSplashAttributionLines; i++) {
      if (!kSplashAttribution[i][0]) continue;
      t.drawString(kSplashAttribution[i], SCREEN_W / 2,
                   SPLASH_ATTR_Y + i * SPLASH_ATTR_DY, FONT_SMALL);
    }
  }

  if (changed(g_cache.note, sizeof(g_cache.note), detail ? detail : "")) {
    // Clear by rectangle, not by drawing background-coloured text: TFT_eSPI
    // skips its padding fill when the two colours match, so an empty string
    // would leave the previous line on screen.
    t.fillRect(INNER_X + 4, SPLASH_NOTE_Y - 12, INNER_W - 8, 24, COL_BG);
    if (detail && detail[0]) {
      t.setTextDatum(MC_DATUM);
      t.setTextColor(COL_DIM, COL_BG);
      t.drawString(detail, SCREEN_W / 2, SPLASH_NOTE_Y, FONT_SMALL);
    }
  }
}

void steps(const char* headline, const Step* rows, int count) {
  TFT_eSPI& t = display::tft();
  const bool fresh = enter("steps");
  drawBorder(COL_FRAME_IDLE);

  if (count > STEP_ROWS) count = STEP_ROWS;

  (void)fresh;  // every field below decides for itself whether it needs redrawing
  if (changed(g_cache.header, sizeof(g_cache.header), headline ? headline : "")) {
    t.setTextDatum(TC_DATUM);
    t.setTextColor(COL_TEXT, COL_BG);
    t.setTextPadding(INNER_W);
    t.drawString(headline ? headline : "", SCREEN_W / 2, STEP_HEAD_Y, FONT_MED);
    t.setTextPadding(0);
  }

  for (int i = 0; i < count; i++) {
    const Step& s = rows[i];

    // One cache key per row covering everything drawn from it. A row whose
    // marker changed but whose text did not still has to repaint.
    char key[64];
    snprintf(key, sizeof(key), "%d|%s|%s", (int)s.state,
             s.label ? s.label : "", s.value ? s.value : "");
    if (!changed(g_cache.step[i], sizeof(g_cache.step[i]), key)) continue;

    const int y = STEP_TOP + i * STEP_ROW_H;

    // Wipe the whole row first. Every field here is variable width — "0.4s"
    // replacing "10.2s", a value appearing where there was none — and a
    // rectangle is both simpler and more certain than per-field padding.
    t.fillRect(INNER_X + 4, y, INNER_W - 8, STEP_ROW_H - 4, COL_BG);

    drawMarker(STEP_MARK_X, y + STEP_ROW_H / 2 - 2, s.state);

    t.setTextDatum(ML_DATUM);
    t.setTextColor(COL_DIM, COL_BG);
    t.drawString(s.label ? s.label : "", STEP_LABEL_X, y + STEP_ROW_H / 2 - 2,
                 FONT_SMALL);

    // The value carries the state's colour: a failed row should be legible as
    // failed from across the desk, not only by its marker.
    uint16_t colour = COL_TEXT;
    if (s.state == STEP_PENDING) colour = COL_DIM;
    else if (s.state == STEP_FAILED) colour = COL_RED;
    t.setTextColor(colour, COL_BG);
    t.drawString(s.value ? s.value : "", STEP_VALUE_X, y + STEP_ROW_H / 2 - 2,
                 FONT_SMALL);
  }

  // Wipe any row left over from a longer checklist. Two different checklists
  // share this screen — four rows for the clock, three for the fetch — and
  // enter() only clears the panel when arriving from a *different* screen, so
  // without this the clock's fourth row would still be sitting there under the
  // fetch's three.
  for (int i = count; i < STEP_ROWS; i++) {
    if (g_cache.step[i][0] == '\0') continue;
    g_cache.step[i][0] = '\0';
    t.fillRect(INNER_X + 4, STEP_TOP + i * STEP_ROW_H, INNER_W - 8, STEP_ROW_H - 4,
               COL_BG);
  }
}

void board(const BoardModel& model, const char* originName, const char* destName,
           int64_t nowEpoch, int32_t dataAgeSec, bool expired) {
  TFT_eSPI& t = display::tft();
  const bool fresh = enter("board");

  // The frame takes its colour from the soonest train — the one the glance is
  // actually about. With nothing to show, a dim frame says "no alarm here".
  drawBorder(model.count > 0 ? urgencyColour(model.rows[0].urgency) : COL_FRAME_IDLE);

  // --- Header -------------------------------------------------------------
  // The clock is laid out first because it is fixed-width and reserves the
  // right-hand space the route name then has to fit inside.
  char clockStr[12];
  formatClock(nowEpoch, clockStr, sizeof(clockStr));

  // Width of the WIDEST clock, not of the current one. Padding erases the
  // previous render, so sizing it to a narrow value would leave a sliver of a
  // wider predecessor. It also keeps the route's available width constant, so
  // the header does not reflow as the digits change.
  const int clockW = t.textWidth("88:88", FONT_MED);
  const int routeMaxW = INNER_W - clockW - 24;

  if (changed(g_cache.clock, sizeof(g_cache.clock), clockStr)) {
    t.setTextDatum(TR_DATUM);
    t.setTextColor(COL_TEXT, COL_BG);
    t.setTextPadding(clockW + 8);
    t.drawString(clockStr, COL_RIGHT, INNER_Y + 6, FONT_MED);
  }

  char header[48];
  snprintf(header, sizeof(header), "%s  >  %s", originName, destName);
  if (changed(g_cache.header, sizeof(g_cache.header), header)) {
    const uint8_t f = fontThatFits(header, routeMaxW, FONT_MED, FONT_SMALL);
    t.setTextDatum(TL_DATUM);
    t.setTextColor(COL_TEXT, COL_BG);
    t.setTextPadding(routeMaxW);
    // Same top edge as the clock at the matching size; nudged down when the
    // fallback font is shorter so the two stay optically level.
    t.drawString(header, INNER_X + 4, INNER_Y + (f == FONT_MED ? 6 : 11), f);
  }

  // Data provenance. A board built from flash must never pass for a live one.
  // One line, so the cases below are a priority order, not independent checks.
  char note[40];
  if (expired) {
    // FINDING F-1 (2026-08 adversarial review): the compiled timetable has run
    // past the last date it can vouch for. This wins over every case below —
    // an ordinary weekday/weekend board still looks fine but a holiday could be
    // silently wrong, which is worse than "no live data" or "live data is old".
    // The board keeps showing times rather than blanking: most days it is still
    // right, and refusing to render at all would be the worse failure.
    snprintf(note, sizeof(note), "TIMETABLE EXPIRED - holidays may differ");
  } else if (!model.anyLive) {
    snprintf(note, sizeof(note), "SCHEDULED TIMES - no live data");
  } else if (model.droppedUnknown > 0) {
    // FINDING F-4 (2026-08 adversarial review): a live train whose number
    // matches no compiled service pattern at all — unlike droppedNotServing,
    // which is the healthy express filter, this means the timetable itself has
    // drifted from what Caltrain is running (e.g. after a renumbering) and
    // wants regenerating. A hint, not an alarm: the board is still live and
    // still showing real trains, just possibly not all of them.
    snprintf(note, sizeof(note), "%d train(s) not in schedule", model.droppedUnknown);
  } else if (dataAgeSec > 180) {
    snprintf(note, sizeof(note), "live data %ds old", (int)dataAgeSec);
  } else {
    note[0] = '\0';
  }
  if (changed(g_cache.note, sizeof(g_cache.note), note)) {
    if (note[0]) {
      t.setTextDatum(TL_DATUM);
      t.setTextColor(COL_SCHED, COL_BG);
      t.setTextPadding(INNER_W - 8);
      t.drawString(note, INNER_X + 4, INNER_Y + 34, FONT_SMALL);
    } else {
      // An empty string cannot erase this field the way it does the others.
      // Padding is what repaints the old text, and TFT_eSPI skips the padding
      // fill when the text and background colours are equal — which is exactly
      // the case when clearing. The note would otherwise stay on screen until
      // the next full repaint. Clear the strip directly instead.
      t.fillRect(INNER_X + 4, INNER_Y + 34, INNER_W - 8,
                 t.fontHeight(FONT_SMALL), COL_BG);
    }
  }

  if (fresh) {
    for (int i = 1; i < BOARD_ROWS; i++) {
      t.drawFastHLine(INNER_X + 4, ROWS_Y + i * ROW_H, INNER_W - 8, COL_RULE);
    }
  }

  // --- Rows ---------------------------------------------------------------
  for (int i = 0; i < BOARD_ROWS; i++) {
    const int y = ROWS_Y + i * ROW_H;
    const bool present = i < model.count;

    char mins[8], when[12], info[40], delay[16];
    uint16_t minsColour = COL_TEXT;

    if (present) {
      const BoardRow& r = model.rows[i];
      snprintf(mins, sizeof(mins), "%d", (int)r.minutesAway);
      minsColour = urgencyColour(r.urgency);

      // Departure time and train identity are split across two lines. Kept on
      // one, the longest case ("11:00a #614 Local Weekend") overruns the space
      // left by the countdown at this font size.
      formatClock(r.departure, when, sizeof(when));
      snprintf(info, sizeof(info), "#%s %s", r.number, r.route);

      if (!r.isLive) {
        snprintf(delay, sizeof(delay), "SCHED");
      } else if (r.delaySec >= 60) {
        snprintf(delay, sizeof(delay), "+%d late", (int)(r.delaySec / 60));
      } else if (r.delaySec <= -60) {
        // Negate: delaySec is negative when running early, and "-2 early"
        // reads as a double negative.
        snprintf(delay, sizeof(delay), "%d early", (int)(-r.delaySec / 60));
      } else {
        snprintf(delay, sizeof(delay), "on time");
      }
    } else {
      mins[0] = '\0';
      when[0] = '\0';
      info[0] = '\0';
      delay[0] = '\0';
    }

    // Big countdown. Font 6 covers digits only, which is all this is.
    if (changed(g_cache.mins[i], sizeof(g_cache.mins[i]), mins) ||
        g_cache.minsColour[i] != minsColour) {
      g_cache.minsColour[i] = minsColour;
      t.setTextDatum(MR_DATUM);
      t.setTextColor(minsColour, COL_BG);
      t.setTextPadding(100);
      t.drawString(mins, COL_MIN_R, y + ROW_H / 2 - 2, FONT_BIG);

      // The unit label only makes sense next to a number.
      t.setTextDatum(TL_DATUM);
      t.setTextColor(COL_DIM, COL_BG);
      t.setTextPadding(34);
      t.drawString(mins[0] ? "min" : "", COL_MIN_R + 6, y + ROW_H / 2 + 4, FONT_SMALL);
    }

    // Departure time on the left, status on the right, sharing a line at the
    // same size: they are one statement — when it leaves, and whether that is
    // to be believed — so they read together rather than as separate facts.
    if (changed(g_cache.when[i], sizeof(g_cache.when[i]), when)) {
      t.setTextDatum(ML_DATUM);
      t.setTextColor(COL_TEXT, COL_BG);
      t.setTextPadding(110);
      t.drawString(when, COL_INFO, y + 28, FONT_MED);
    }

    if (changed(g_cache.delay[i], sizeof(g_cache.delay[i]), delay)) {
      uint16_t c = COL_DIM;
      if (strncmp(delay, "+", 1) == 0) c = COL_LATE;
      else if (strcmp(delay, "SCHED") == 0) c = COL_SCHED;
      t.setTextDatum(MR_DATUM);
      t.setTextColor(c, COL_BG);
      // Padding sized for the longest status this can produce ("+99 late"),
      // not the current one, so a shorter value fully erases a longer one.
      t.setTextPadding(t.textWidth("+99 late", FONT_MED) + 10);
      t.drawString(delay, COL_RIGHT, y + 28, FONT_MED);
    }

    if (changed(g_cache.info[i], sizeof(g_cache.info[i]), info)) {
      t.setTextDatum(ML_DATUM);
      t.setTextColor(COL_DIM, COL_BG);
      t.setTextPadding(INNER_W - (COL_INFO - INNER_X) - 8);
      t.drawString(info, COL_INFO, y + 60, FONT_SMALL);
    }
  }
  t.setTextPadding(0);
}

void overnight(const char* originName, const char* destName, bool have,
               const ScheduledDeparture& first, const char* tomorrowLabel,
               bool expired) {
  TFT_eSPI& t = display::tft();
  enter("night");
  drawBorder(COL_RULE);

  char header[48];
  snprintf(header, sizeof(header), "%s  >  %s", originName, destName);
  if (changed(g_cache.header, sizeof(g_cache.header), header)) {
    t.setTextDatum(TC_DATUM);
    t.setTextColor(COL_DIM, COL_BG);
    t.setTextPadding(INNER_W);
    t.drawString(header, SCREEN_W / 2, INNER_Y + 12, 2);
  }

  // FINDING F-1 (2026-08 adversarial review): this screen is not a rare
  // corner case — several station pairs have no weekend or weekday service at
  // all, so for them this IS the primary screen, and "First tomorrow ..."
  // below comes straight from the same compiled timetable that goes stale.
  // Same slot, same wording, same colour as board()'s note, at the same
  // offset below the header, so the two screens read as one system rather
  // than drifting apart.
  char expiredNote[40];
  if (expired) {
    snprintf(expiredNote, sizeof(expiredNote), "TIMETABLE EXPIRED - holidays may differ");
  } else {
    expiredNote[0] = '\0';
  }
  if (changed(g_cache.expiredNote, sizeof(g_cache.expiredNote), expiredNote)) {
    if (expiredNote[0]) {
      t.setTextDatum(TC_DATUM);
      t.setTextColor(COL_SCHED, COL_BG);
      t.setTextPadding(INNER_W);
      t.drawString(expiredNote, SCREEN_W / 2, INNER_Y + 34, FONT_SMALL);
    } else {
      // Same reason as board()'s note: TFT_eSPI's padding fill is skipped
      // when text colour equals background colour, so an empty drawString
      // here would silently leave the old warning's pixels on screen. This
      // shipped as a real bug on this project before (see board()); fillRect
      // instead of trusting padding to erase it.
      t.fillRect(INNER_X, INNER_Y + 34, INNER_W, t.fontHeight(FONT_SMALL), COL_BG);
    }
  }

  char line[48];
  snprintf(line, sizeof(line), "No more trains tonight");
  if (changed(g_cache.note, sizeof(g_cache.note), line)) {
    t.setTextDatum(MC_DATUM);
    t.setTextColor(COL_TEXT, COL_BG);
    t.setTextPadding(INNER_W);
    t.drawString(line, SCREEN_W / 2, SCREEN_H / 2 - 40, 4);
  }

  char detail[64];
  if (have) {
    char when[8];
    formatClockMin(first.depMin, when, sizeof(when));
    snprintf(detail, sizeof(detail), "First %s  %s  #%s", tomorrowLabel, when, first.number);
  } else {
    snprintf(detail, sizeof(detail), "No service %s on this route", tomorrowLabel);
  }
  if (changed(g_cache.info[0], sizeof(g_cache.info[0]), detail)) {
    t.setTextDatum(MC_DATUM);
    t.setTextColor(have ? COL_SCHED : COL_DIM, COL_BG);
    t.setTextPadding(INNER_W);
    t.drawString(detail, SCREEN_W / 2, SCREEN_H / 2 + 20, 4);
  }
  t.setTextPadding(0);
}

void updating(const char* fromVersion, const char* toVersion,
              const char* step, int percent) {
  TFT_eSPI& t = display::tft();
  const bool fresh = enter("updating");
  // Nothing here is an alarm to react to — same idle frame as boot/overnight.
  drawBorder(COL_FRAME_IDLE);

  // The title, the bar outline and the "do not unplug" warning are all
  // static for the life of this screen — they only need to be drawn once, on
  // arrival. The version line below is NOT included here (I2, whole-branch
  // review): `fresh` is true for exactly one tick, the first after
  // ota_task::start(), and toVersion is still the zeroed initial
  // Progress::version at that point — setVersion() is not called until
  // seconds later, once the manifest signature has verified. Drawing the
  // version line only when fresh left the panel permanently reading
  // "v1.1.0  >  " with a blank target for the rest of the update.
  if (fresh) {
    t.setTextDatum(MC_DATUM);
    t.setTextColor(COL_TEXT, COL_BG);
    t.drawString("Updating firmware", SCREEN_W / 2, UPD_TITLE_Y, FONT_MED);

    // Outline only; the fill is drawn separately below and is the only part
    // of the bar that ever needs to move again.
    t.drawRect(UPD_BAR_X, UPD_BAR_Y, UPD_BAR_W, UPD_BAR_H, COL_DIM);

    t.drawString("do not unplug", SCREEN_W / 2, UPD_WARN_Y, FONT_SMALL);
  }

  // "vFrom  >  vTo" — the same "  >  " separator the board's route header and
  // the overnight header already use for "this  >  that" pairs. Repainted on
  // every change (own cache slot, own erase), the same pattern the step line
  // below already uses, and for the same reason: unlike the title/bar/warning
  // above, this line's content is NOT fixed at the moment the screen appears.
  char ver[sizeof(g_cache.ver)];
  snprintf(ver, sizeof(ver), "%s  >  %s", fromVersion ? fromVersion : "?",
           toVersion ? toVersion : "?");
  if (changed(g_cache.ver, sizeof(g_cache.ver), ver)) {
    t.fillRect(INNER_X, UPD_VER_Y - 12, INNER_W, 24, COL_BG);
    t.setTextDatum(MC_DATUM);
    t.setTextColor(COL_DIM, COL_BG);
    t.drawString(ver, SCREEN_W / 2, UPD_VER_Y, FONT_SMALL);
  }

  // Clamp before scaling: a bad `total` reported by the server must not be
  // able to draw a fill wider than the outline. Negative is left alone — it
  // is the "nothing to show yet" sentinel, and must stay distinguishable
  // from a real 0%.
  int pct = percent;
  if (pct > 100) pct = 100;

  char pctKey[8];
  if (pct < 0) pctKey[0] = '\0';
  else snprintf(pctKey, sizeof(pctKey), "%d", pct);

  if (changed(g_cache.pct, sizeof(g_cache.pct), pctKey)) {
    const int innerX = UPD_BAR_X + 2;
    const int innerY = UPD_BAR_Y + 2;
    const int innerW = UPD_BAR_W - 4;
    const int innerH = UPD_BAR_H - 4;

    // Clear the whole interior rather than just the grown/shrunk sliver: a
    // retried step can restart progress partway through, so the fill is not
    // guaranteed to only ever grow. A rectangle is simpler and more certain
    // than working out which slice needs erasing.
    t.fillRect(innerX, innerY, innerW, innerH, COL_BG);
    if (pct >= 0) {
      const int fillW = innerW * pct / 100;
      if (fillW > 0) t.fillRect(innerX, innerY, fillW, innerH, COL_TEXT);
    }

    // The "NN%" figure under the bar. Cleared by rectangle, not by drawing
    // background-on-background text: TFT_eSPI skips its padding fill when
    // the text and background colours match, so an empty string here would
    // leave a stale percentage on screen instead of erasing it.
    t.fillRect(INNER_X, UPD_PCT_Y - 12, INNER_W, 24, COL_BG);
    if (pct >= 0) {
      char label[8];
      snprintf(label, sizeof(label), "%d%%", pct);
      t.setTextDatum(MC_DATUM);
      t.setTextColor(COL_TEXT, COL_BG);
      t.drawString(label, SCREEN_W / 2, UPD_PCT_Y, FONT_SMALL);
    }
  }

  // The current step, e.g. "downloading" / "verifying" / "installing".
  // Shares the generic "note" cache slot that status()/splash()/board() each
  // use for their own secondary line — safe because enter() wipes the whole
  // cache on a screen change, so nothing survives from another screen's use
  // of the same field.
  if (changed(g_cache.note, sizeof(g_cache.note), step ? step : "")) {
    t.fillRect(INNER_X, UPD_STEP_Y - 12, INNER_W, 24, COL_BG);
    if (step && step[0]) {
      t.setTextDatum(MC_DATUM);
      t.setTextColor(COL_DIM, COL_BG);
      t.drawString(step, SCREEN_W / 2, UPD_STEP_Y, FONT_SMALL);
    }
  }
}

}  // namespace render
#endif  // ARDUINO
