// render.h — drawing the departure board.
//
// Screens, in the order a device meets them:
//   status()    a single line, during boot / WiFi join / errors
//   portal()    the setup instructions, while the SoftAP is up
//   board()     the three departures — the normal state
//   overnight() after the last train, showing tomorrow's first
//   updating()  a firmware update in progress — takes the panel over
#pragma once
#ifdef ARDUINO
#include "board_model.h"
#include "service_day.h"
#include "timetable.h"

namespace render {

// Discard any cached state so the next draw repaints everything. Call when
// switching between screens, since each tracks its own fields.
void invalidate();

// One centred line on a black screen.
void status(const char* headline, const char* detail);

// The boot splash: the Caltrain mark above the product name and one status
// line. Text only: this firmware draws no logo on any screen.
void splash(const char* detail);

// A boot stage, for the checklist screen below.
enum StepState {
  STEP_PENDING,  // not started
  STEP_ACTIVE,   // running now
  STEP_DONE,     // finished successfully
  STEP_FAILED,   // gave up here
};

struct Step {
  const char* label;  // left column, e.g. "reply"
  const char* value;  // right column, e.g. "2.4s" — may be empty
  StepState   state;
};

// Up to this many rows fit under a headline at a readable size.
constexpr int STEP_ROWS = 4;

// A progress checklist: every stage on screen at once, each marked pending,
// running, done or failed. Unlike status(), a failure stays legible — you can
// see which stage stopped and what the ones before it reported.
void steps(const char* headline, const Step* rows, int count);

// Setup instructions: which network to join, its password, and where to browse.
void portal(const char* apSsid, const char* apPass, const char* url);

// The departure board.
//   originName/destName  station names for the header
//   nowEpoch             used for the header clock
//   dataAgeSec           seconds since the last successful fetch; drives the
//                        staleness note. Negative means "never succeeded".
//   expired              from timetableExpired() in timetable.h, computed by
//                        the caller against the current ServiceDay. Wins over
//                        every other note: ordinary weekday/weekend service is
//                        still broadly right past this point, but a holiday
//                        could be silently wrong, which is worse than "no live
//                        data" (F-1, 2026-08 adversarial review).
void board(const BoardModel& model, const char* originName, const char* destName,
           int64_t nowEpoch, int32_t dataAgeSec, bool expired);

// After the last train of the service day.
//   have/first           tomorrow's first departure, if there is one
//   tomorrowLabel        e.g. "tomorrow" or "Monday"
//   expired              same meaning, same wording and same mechanism as
//                        board()'s `expired` (F-1, 2026-08 adversarial
//                        review): the result of timetableExpired() against
//                        kTimetableLastOverride/kTimetableFeedEnd. NOT
//                        necessarily the same VALUE as the one passed to
//                        board(), though: this screen shows tomorrow's first
//                        departure, so the caller should check
//                        timetableExpired() against tomorrow's date, not
//                        today's — otherwise the one night a year the
//                        threshold falls between the two dates, a still-valid
//                        today would wrongly suppress a warning about data
//                        that is about tomorrow. This screen is not a rare
//                        corner case: several station pairs have no weekend
//                        or weekday service at all, so for them this IS the
//                        primary screen, and a viewer who only ever sees it
//                        would never see board()'s warning otherwise.
void overnight(const char* originName, const char* destName, bool have,
               const ScheduledDeparture& first, const char* tomorrowLabel,
               bool expired);

// The firmware update screen. Takes the panel over for the ~60-90 s an update
// takes, because departures vanishing with no explanation reads as a fault —
// this project has already shipped one frozen-looking board (a staleness note
// stuck at "live data 240s old") and the fix then was also to make the state
// visible rather than silent.
//
//   fromVersion/toVersion  e.g. "v1.1.0" and "v1.2.0"
//   step                   e.g. "downloading", "verifying", "installing"
//   percent                0-100, or negative when there is nothing to show yet
void updating(const char* fromVersion, const char* toVersion,
              const char* step, int percent);

}  // namespace render
#endif  // ARDUINO
