// Host tests for board_model.{h,cpp} — the merge of live predictions over the
// compiled timetable, the express filter, and the border-colour thresholds.
//
// These run against the real generated timetable, so the scheduled trains are
// genuine Caltrain services. Live departures are synthesised, because the point
// is to drive the merge to states the captured fixture cannot reach: a delayed
// train, an express that skips the destination, a total feed outage.
#include <cstdio>
#include <cstring>
#include "board_model.h"

static int failures = 0;
static void check(bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); failures++; } }

// A fixed, arbitrary local midnight to anchor the service day. The absolute
// value does not matter — only that scheduled minutes are measured from it.
static constexpr int64_t DAY = 1786118400;  // 2026-08-08T00:00:00Z, used as "local midnight"

static int64_t at(int hh, int mm) { return DAY + (hh * 60 + mm) * 60; }

static SiriResult noLive() {
  SiriResult r{};
  r.ok = false;
  r.count = 0;
  return r;
}

static void addLive(SiriResult& r, const char* number, const char* route,
                    int64_t aimed, int64_t expected) {
  LiveDeparture& d = r.departures[r.count++];
  memset(&d, 0, sizeof(d));
  strncpy(d.number, number, sizeof(d.number) - 1);
  strncpy(d.route, route, sizeof(d.route) - 1);
  d.aimedDeparture = aimed;
  d.expectedDeparture = expected;
  r.ok = true;
}

int main() {
  const int sf       = stationIndexByName("San Francisco");
  const int diridon  = stationIndexByName("San Jose Diridon");
  const int collegeP = stationIndexByName("College Park");

  // --- Urgency thresholds ---------------------------------------------------
  // Both bounds are exclusive: red is minutesAway < redUnder, yellow is
  // < yellowUnder, green is everything at or above it. Stated once here and in
  // urgency.h so the two cannot drift.
  //
  // The shipped defaults are 10 and 16, which reproduce the rule this project
  // was built around: >15 green, 10..15 yellow, <10 red. Boundaries pinned
  // exactly, because off-by-one here is invisible until you miss a train.
  {
    const UrgencyThresholds d = kUrgencyDefaults;
    check(d.redUnder == 10 && d.yellowUnder == 16, "defaults are 10 and 16");
    check(urgencyFor(100, d) == URGENCY_GREEN,  "100 min -> green");
    check(urgencyFor(16,  d) == URGENCY_GREEN,  "16 min -> green");
    check(urgencyFor(15,  d) == URGENCY_YELLOW, "exactly 15 -> yellow, not green");
    check(urgencyFor(12,  d) == URGENCY_YELLOW, "12 min -> yellow");
    check(urgencyFor(10,  d) == URGENCY_YELLOW, "exactly 10 -> yellow, not red");
    check(urgencyFor(9,   d) == URGENCY_RED,    "9 min -> red");
    check(urgencyFor(0,   d) == URGENCY_RED,    "0 min -> red");
  }

  // A user who wants a longer warning gets the same shape, moved out.
  {
    const UrgencyThresholds wide{20, 45};
    check(urgencyFor(45, wide) == URGENCY_GREEN,  "45 min -> green at yellowUnder 45");
    check(urgencyFor(44, wide) == URGENCY_YELLOW, "44 min -> yellow");
    check(urgencyFor(20, wide) == URGENCY_YELLOW, "the red bound is exclusive");
    check(urgencyFor(19, wide) == URGENCY_RED,    "19 min -> red at redUnder 20");
  }

  // A short-fuse sign: one minute of red, and nothing green until 5.
  {
    const UrgencyThresholds tight{1, 5};
    check(urgencyFor(0, tight) == URGENCY_RED,    "0 min -> red with redUnder 1");
    check(urgencyFor(1, tight) == URGENCY_YELLOW, "1 min -> yellow with redUnder 1");
    check(urgencyFor(4, tight) == URGENCY_YELLOW, "4 min -> yellow");
    check(urgencyFor(5, tight) == URGENCY_GREEN,  "5 min -> green");
  }

  // Equal bounds collapse the yellow band: the sign goes straight red to green,
  // the same way an equal bright window means "always bright" in config.h.
  {
    const UrgencyThresholds noYellow{10, 10};
    check(urgencyFor(9,   noYellow) == URGENCY_RED,   "collapsed: 9 min is red");
    check(urgencyFor(10,  noYellow) == URGENCY_GREEN, "collapsed: 10 min is green");
    check(urgencyFor(100, noYellow) == URGENCY_GREEN, "collapsed: far out is green");
    for (int32_t m = 0; m <= 120; m++) {
      if (urgencyFor(m, noYellow) == URGENCY_YELLOW) {
        check(false, "collapsed thresholds must never yield yellow");
        break;
      }
    }
  }

  // A record that survived sanitising cannot invert the bands, but the display
  // must stay sane even if one ever did: red wins at the overlap rather than
  // producing an unreachable colour.
  {
    const UrgencyThresholds inverted{30, 5};
    check(urgencyFor(4,  inverted) == URGENCY_RED,   "inverted: 4 min is red");
    check(urgencyFor(29, inverted) == URGENCY_RED,   "inverted: red wins the overlap");
    check(urgencyFor(30, inverted) == URGENCY_GREEN, "inverted: above both is green");
  }

  // --- The thresholds reach the built board --------------------------------
  // urgencyFor() being right is worthless if buildBoard() ignores the setting,
  // so drive it through the real merge: with an hour of red configured, every
  // row on a board built at 07:00 must come back red rather than green.
  {
    BoardModel def = buildBoard(noLive(), sf, diridon, SVC_WEEKDAY,
                                at(7, 0), DAY, kUrgencyDefaults);
    BoardModel red = buildBoard(noLive(), sf, diridon, SVC_WEEKDAY,
                                at(7, 0), DAY, UrgencyThresholds{60, 90});
    check(def.count > 0 && def.count == red.count, "same board, different thresholds");
    bool anyDiff = false;
    for (int i = 0; i < red.count; i++) {
      check(red.rows[i].urgency == URGENCY_RED, "every row is red under a 60 min bound");
      if (red.rows[i].urgency != def.rows[i].urgency) anyDiff = true;
      check(red.rows[i].minutesAway == def.rows[i].minutesAway,
            "the thresholds change only the colour, not the times");
    }
    check(anyDiff, "the configured thresholds actually changed a row's colour");
  }

  // --- Schedule-only board (the 511-is-down case) ---------------------------
  {
    BoardModel b = buildBoard(noLive(), sf, diridon, SVC_WEEKDAY,
                              at(7, 0), DAY, kUrgencyDefaults);
    check(b.count == BOARD_ROWS, "a full board is built with no live data at all");
    check(!b.anyLive, "nothing is marked live");
    check(b.rows[0].delaySec == 0, "no delay is claimed without a prediction");

    bool ascending = true, future = true;
    for (int i = 0; i < b.count; i++) {
      if (b.rows[i].departure < at(7, 0)) future = false;
      if (i && b.rows[i].departure < b.rows[i - 1].departure) ascending = false;
    }
    check(ascending, "rows ascend by departure");
    check(future, "no row is in the past");
    check(b.rows[0].minutesAway ==
              (int32_t)((b.rows[0].departure - at(7, 0)) / 60),
          "minutesAway is the floor of the remaining time");
  }

  // --- A live prediction corrects the scheduled time ------------------------
  {
    // Find a real scheduled train, then report it running six minutes late.
    ScheduledDeparture s[1];
    check(nextScheduled(sf, diridon, SVC_WEEKDAY, 7 * 60, s, 1) == 1,
          "there is a weekday train after 07:00");
    const int64_t aimed = DAY + (int64_t)s[0].depMin * 60;

    SiriResult live{};
    addLive(live, s[0].number, "Local Weekday", aimed, aimed + 6 * 60);

    BoardModel b = buildBoard(live, sf, diridon, SVC_WEEKDAY, at(7, 0), DAY, kUrgencyDefaults);
    check(b.count > 0, "board built");
    check(b.anyLive, "the board reports having live data");

    // The corrected train must appear once, not twice.
    int seen = 0, idx = -1;
    for (int i = 0; i < b.count; i++) {
      if (strcmp(b.rows[i].number, s[0].number) == 0) { seen++; idx = i; }
    }
    check(seen == 1, "the live train replaces its scheduled row rather than duplicating it");
    if (idx >= 0) {
      check(b.rows[idx].isLive, "the merged row is marked live");
      check(b.rows[idx].delaySec == 360, "delay is six minutes");
      check(b.rows[idx].departure == aimed + 360, "departure uses the prediction");
    }
  }

  // --- The express filter ---------------------------------------------------
  // A live train that the schedule says does not stop at the destination must
  // never reach the board, however prominently the feed reports it.
  {
    // Find a weekday train that serves Diridon but NOT College Park.
    const char* skipper = nullptr;
    int64_t skipperAimed = 0;
    for (int t = 0; t < kTripCount && !skipper; t++) {
      if (kTrips[t].service != SVC_WEEKDAY) continue;
      uint16_t dep = 0;
      if (!tripServes(kTrips[t], sf, diridon, &dep)) continue;
      if (tripServes(kTrips[t], sf, collegeP, nullptr)) continue;
      skipper = kTrips[t].number;
      skipperAimed = DAY + (int64_t)dep * 60;
    }
    check(skipper != nullptr, "found a train serving Diridon but skipping College Park");

    if (skipper) {
      SiriResult live{};
      addLive(live, skipper, "Express", skipperAimed, skipperAimed);

      BoardModel b = buildBoard(live, sf, collegeP, SVC_WEEKDAY,
                                skipperAimed - 600, DAY, kUrgencyDefaults);
      bool present = false;
      for (int i = 0; i < b.count; i++) {
        if (strcmp(b.rows[i].number, skipper) == 0) present = true;
      }
      check(!present, "a train that skips the destination is kept off the board");
      check(b.droppedNotServing == 1, "and is counted as filtered, not silently lost");

      // The same train IS valid for a destination it does serve.
      BoardModel c = buildBoard(live, sf, diridon, SVC_WEEKDAY,
                                skipperAimed - 600, DAY, kUrgencyDefaults);
      bool here = false;
      for (int i = 0; i < c.count; i++) {
        if (strcmp(c.rows[i].number, skipper) == 0) here = true;
      }
      check(here, "the same train is shown for a destination it does serve");
      check(c.droppedNotServing == 0, "and is not filtered there");
    }
  }

  // --- A train number the timetable has never heard of ----------------------
  {
    SiriResult live{};
    addLive(live, "9999", "Mystery", at(8, 0), at(8, 0));
    BoardModel b = buildBoard(live, sf, diridon, SVC_WEEKDAY, at(7, 30), DAY, kUrgencyDefaults);
    bool present = false;
    for (int i = 0; i < b.count; i++) {
      if (strcmp(b.rows[i].number, "9999") == 0) present = true;
    }
    check(!present, "an unknown train number is not shown");
    check(b.droppedUnknown == 1, "and is counted so a stale timetable is visible");
  }

  // --- Departed trains fall off --------------------------------------------
  {
    BoardModel early = buildBoard(noLive(), sf, diridon, SVC_WEEKDAY, at(7, 0), DAY, kUrgencyDefaults);
    BoardModel later = buildBoard(noLive(), sf, diridon, SVC_WEEKDAY, at(9, 0), DAY, kUrgencyDefaults);
    check(early.count > 0 && later.count > 0, "both times have service");
    check(later.rows[0].departure > early.rows[0].departure,
          "the head of the board advances as the day goes on");
    check(later.rows[0].departure >= at(9, 0), "the head has not already left");
  }

  // --- End of service -------------------------------------------------------
  {
    BoardModel b = buildBoard(noLive(), sf, diridon, SVC_WEEKDAY, at(23, 59), DAY, kUrgencyDefaults);
    // Whatever remains must still be in the future; an empty board is a valid
    // answer and tells the caller to show the overnight screen.
    for (int i = 0; i < b.count; i++) {
      check(b.rows[i].departure >= at(23, 59), "late-night rows are still in the future");
    }
    BoardModel gone = buildBoard(noLive(), sf, diridon, SVC_WEEKDAY,
                                 DAY + 30 * 3600, DAY, kUrgencyDefaults);
    check(gone.count == 0, "well past the end of the service day the board is empty");
    check(!gone.anyLive, "an empty board is not live");
  }

  // --- Guard rails ----------------------------------------------------------
  {
    BoardModel b = buildBoard(noLive(), sf, sf, SVC_WEEKDAY, at(9, 0), DAY, kUrgencyDefaults);
    check(b.count == 0, "same origin and destination yields nothing");
    b = buildBoard(noLive(), -1, diridon, SVC_WEEKDAY, at(9, 0), DAY, kUrgencyDefaults);
    check(b.count == 0, "invalid origin yields nothing");
  }

  if (failures == 0) printf("test_board_model: all checks passed\n");
  return failures ? 1 : 0;
}
