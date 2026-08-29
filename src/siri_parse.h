// siri_parse.h — turning a 511.org StopMonitoring response into departures.
//
// Pure logic over a JSON string, with no network and no Arduino headers, so the
// host tests run the identical parser against responses captured from the live
// API (test/fixtures/).
//
// WHAT THE FEED ACTUALLY GIVES US. Two observations, both against the live API:
//
//   A. an intermediate stop, weekend service (captured with tools/probe_511.py;
//      not committed — tools/captures/ is gitignored)
//   B. stop 70012 (San Francisco, a terminus), weekday service — committed as
//      test/fixtures/stopmonitoring_70012.json
//
// The two differ in three ways at once — when each was taken, service pattern,
// and terminus versus intermediate — so no single one of them can be blamed
// for a difference below. That is stated rather than glossed, because it
// bounds what these measurements can prove.
//
// Stable across both:
//
//   * exactly THREE departures, always. MaximumStopVisits is accepted and then
//     ignored. This is why the board also consults the compiled timetable.
//   * times as ISO-8601 UTC, e.g. "2026-08-18T03:55:00Z".
//   * DatedVehicleJourneyRef is the public train number ("164"), which is the
//     join key to the compiled timetable.
//   * LineRef is the service type ("Local Weekday", "Local Weekend", ...).
//   * DestinationRef is the train's TERMINUS, not its stop list — so it cannot
//     tell you whether the train stops where you are going. Only the timetable
//     can answer that.
//   * the JSON shape is identical at a terminus and at an intermediate stop —
//     same keys, same nesting. Only the values differ.
//
// NOT stable, and the reason this parser trusts none of it:
//
//   * RecordedAtTime DISAGREED between the two observations. In A it was
//     "1970-01-01T00:00:00Z" — epoch zero — on all three visits. In B it was a
//     real and plausible time, 13 s before ResponseTimestamp. Two samples
//     cannot say whether that is a fix, a weekday/weekend difference, or a
//     per-stop one, and the field is not worth a third request to find out:
//     a freshness signal that is sometimes epoch zero is not a freshness
//     signal. Staleness is tracked by the caller's own clock either way, so
//     this changed the documentation and not a line of code.
//   * ExpectedDepartureTime can be absent when there is no live prediction. In
//     A it ran 30 s past AimedDepartureTime; in B it equalled it exactly, while
//     ExpectedArrivalTime was null on the same call. Treat "expected" as
//     optional and fall back to "aimed".
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "timetable_data.h"  // TRAIN_NUMBER_MAX

// Upper bound on departures kept from one response. The feed returns three;
// the extra slots cost nothing and stop a feed change from overflowing.
inline constexpr int SIRI_MAX_DEPARTURES = 8;

// One live departure. Times are UNIX epoch seconds UTC, which is what the
// device's clock is in and what makes "minutes from now" a subtraction.
struct LiveDeparture {
  char    number[TRAIN_NUMBER_MAX + 1];  // "614"; empty if the feed omitted it
  char    route[24];                     // "Local Weekday", "Express", ...
  int64_t aimedDeparture;                // 0 if absent
  int64_t expectedDeparture;             // 0 if absent — no live prediction
};

struct SiriResult {
  LiveDeparture departures[SIRI_MAX_DEPARTURES];
  int           count;
  bool          ok;       // false if the payload could not be parsed at all
  const char*   error;    // static string describing the failure, else nullptr
};

// Parse a StopMonitoring JSON document.
//
// Accepts a leading UTF-8 BOM even though the live API does not send one, since
// that costs three bytes of tolerance and a BOM would otherwise fail the whole
// parse. Never throws; a malformed document yields ok=false and count=0.
SiriResult siriParse(const char* json, size_t len);

// Parse an ISO-8601 instant like "2026-08-08T18:00:00Z" into epoch seconds.
// Returns 0 on anything it does not understand.
//
// Only the trailing-Z UTC form and a numeric +/-HH:MM offset are handled, which
// is all 511 emits. Deliberately not a general ISO-8601 parser.
int64_t parseIso8601Utc(const char* s);
