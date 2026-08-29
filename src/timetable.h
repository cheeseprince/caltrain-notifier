// timetable.h — querying the compiled Caltrain schedule.
//
// Pure logic over the generated tables in timetable_data.h. No Arduino headers,
// no network: this compiles and is tested on the host.
//
// The schedule earns its place in flash for three reasons the live 511 feed
// cannot cover (all verified against the live API — see tools/probe_511.py and
// the committed capture in test/fixtures/stopmonitoring_70012.json):
//
//   * 511 returns only the next THREE departures at a stop, so the board would
//     run out of rows;
//   * it never says which intermediate stops a train makes, so an Express that
//     skips your destination looks identical to a train you could catch;
//   * it is unreachable when the network or the service is down.
//
// SERVICE DAYS. Caltrain's day does not end at midnight — a train departing at
// 00:40 belongs to the previous service day and the feed writes it as 24:40.
// Everything here works in "minutes since midnight of the service day", so a
// value above 1440 is normal. Converting wall-clock time into a (service day,
// minute) pair is the caller's job; see serviceDayMinutes() in main.
#pragma once
#include <stdint.h>

#include "route.h"
#include "timetable_data.h"

// One scheduled departure from the origin station.
// `number` and `route` point into the generated tables, which have static
// storage duration, so they stay valid for the life of the program.
struct ScheduledDeparture {
  const char* number;   // "614", or "M101" on the holiday timetable
  const char* route;    // "Local Weekday", "Express", ...
  uint16_t    depMin;   // minutes since midnight of the service day
};

// Which of the generated service patterns applies on a date.
// `dayOfWeek` follows struct tm's tm_wday: 0 = Sunday .. 6 = Saturday.
//
// Dates listed in kDateOverrides win over the weekly pattern. Note that a
// holiday is not automatically a weekend: four of Caltrain's holidays run a
// separate, reduced timetable.
uint8_t serviceForDate(uint32_t yyyymmdd, int dayOfWeek);

// Does this trip call at `originIdx` and then later at `destIdx`?
// On success, writes the origin departure time to `depMin` (when non-null).
//
// "Later" is positional, not chronological: the trip's calls are stored in
// travel order, so requiring origin before destination is what rejects a train
// running the opposite way, and requiring both to be present is what rejects an
// Express that skips your destination.
bool tripServes(const Trip& trip, int originIdx, int destIdx, uint16_t* depMin);

// Fill `out` with up to `maxOut` scheduled departures from `originIdx` that
// reach `destIdx`, on the given service pattern, departing at or after
// `afterMin`. Returns how many were written, in ascending departure order.
//
// Returns 0 for an invalid route rather than treating it as "no trains".
int nextScheduled(int originIdx, int destIdx, uint8_t service, uint16_t afterMin,
                  ScheduledDeparture* out, int maxOut);

// The first departure of the service day, used for the overnight screen
// ("first train tomorrow"). Returns false if the route has no service that day —
// which really happens: several stations have no weekend service at all.
bool firstScheduled(int originIdx, int destIdx, uint8_t service,
                    ScheduledDeparture* out);

// Has the compiled schedule run off the end of what it can vouch for?
//
// FINDING F-1 (2026-08 adversarial review): serviceForDate() silently falls
// back to plain weekday/weekend once `serviceDate` runs past the last entry in
// kDateOverrides — which is exactly wrong on the next unlisted holiday, and
// nothing said so. This is the pure decision behind that warning.
//
// `lastOverride` and `feedEnd` are two different dates and they disagree:
// pass kTimetableLastOverride and kTimetableFeedEnd from timetable_data.h.
// The EARLIER of the two governs, because that is the first date at which
// something in the header is no longer guaranteed correct — usually
// kTimetableLastOverride, since a holiday pattern runs out before the feed's
// own declared validity window does.
//
// Expired does not mean unusable: ordinary weekday/weekend service is still
// right past this point, so the caller should keep showing times and only add
// a warning (see render::board's note) rather than blank the board.
bool timetableExpired(uint32_t serviceDate, uint32_t lastOverride, uint32_t feedEnd);

// Find a scheduled trip by its published train number, or nullptr.
//
// This is the join between the live feed and the schedule: 511 gives a train
// number but not a stop list, so the number is looked up here to answer "does
// this train actually stop where I am going".
//
// `service` is tried first. If the number is not in that pattern, every other
// pattern is searched before giving up — a stale date-override table would
// otherwise blank the whole board on a holiday, and a trip found under the
// "wrong" service still carries the stop list, which is what the caller wants.
const Trip* findTripByNumber(const char* number, uint8_t service);
