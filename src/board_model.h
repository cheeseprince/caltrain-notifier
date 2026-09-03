// board_model.h — deciding what the three rows on the screen say.
//
// Pure logic: no clock, no network, no display. Everything it needs is passed
// in, which is what lets the host tests drive it to any moment in time.
//
// The job is to reconcile two sources that disagree in useful ways:
//
//   LIVE (511)      knows delays, but only three departures ahead, and cannot
//                   say whether a train stops at your destination.
//   SCHEDULE (flash) knows every train and every stop it makes, but nothing
//                   about today.
//
// So: the schedule decides WHICH trains belong on the board, and the live feed
// corrects WHEN they leave. A train the live feed reports but the schedule says
// skips your destination is removed — showing it would send you to the platform
// for a train that sails past.
#pragma once
#include <stdint.h>

#include "siri_parse.h"
#include "timetable.h"
#include "urgency.h"

// How many departures the board shows.
inline constexpr int BOARD_ROWS = 3;

struct BoardRow {
  char    number[TRAIN_NUMBER_MAX + 1];
  char    route[24];
  int64_t departure;    // epoch seconds: the live prediction if there is one
  int32_t delaySec;     // expected minus scheduled; 0 when there is no live data
  int32_t minutesAway;  // floor of the remaining time; may be 0, never negative
  bool    isLive;       // false means these are timetable times, not predictions
  Urgency urgency;
};

struct BoardModel {
  BoardRow rows[BOARD_ROWS];
  int      count;

  // True when at least one row carries a live prediction. The display uses this
  // to decide whether to show the "scheduled times only" banner: a board built
  // entirely from flash looks identical to a live one otherwise, and quietly
  // presenting a timetable as real-time is the worst failure this thing has.
  bool anyLive;

  // Live departures discarded because the schedule says they do not stop at the
  // destination. Expected and healthy — it is the express filter working.
  int droppedNotServing;

  // Live departures whose train number is in no service pattern at all. Should
  // be zero; a non-zero value means the compiled timetable has drifted from
  // what Caltrain is running and wants regenerating.
  int droppedUnknown;
};

// Build the board.
//
//   live                 parsed StopMonitoring response; pass an empty result
//                        (count 0) when the fetch failed — the board then falls
//                        back to the timetable alone
//   originIdx, destIdx   indices into kStations
//   service              from serviceForDate()
//   nowEpoch             current time, UTC seconds
//   serviceDayStartEpoch epoch of local midnight beginning the CURRENT service
//                        day. After midnight but before service ends, that is
//                        yesterday's midnight — which is exactly why this is a
//                        parameter rather than something computed here.
//   urgency              the user's border-colour bounds, from Config. Passed
//                        in rather than read from a global so the merge stays
//                        pure and the thresholds are testable on the host.
//
// Returns a board with count 0 when nothing is left to run today; the caller
// then shows the overnight screen via firstScheduled().
BoardModel buildBoard(const SiriResult& live, int originIdx, int destIdx,
                      uint8_t service, int64_t nowEpoch,
                      int64_t serviceDayStartEpoch,
                      const UrgencyThresholds& urgency);
