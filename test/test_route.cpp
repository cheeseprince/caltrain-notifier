// Host tests for route.{h,cpp} — station lookup, direction, platform stop_id,
// and weekday/weekend service selection.
//
// The direction cases below are not academic. An earlier version carried a
// hand-written pair of platform stop_ids in which BOTH ends had the same
// direction digit, so a southbound trip polled two northbound platforms and
// quietly returned the wrong side of the tracks. Picking stations by name and
// DERIVING the platform from the direction is what makes that unrepresentable,
// so the derivation is pinned here rather than assumed.
#include <cstdio>
#include "route.h"

static int failures = 0;
static void check(bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); failures++; } }

int main() {
  // --- Station lookup -------------------------------------------------------
  // The worked example is terminus to terminus: the canonical case on any
  // line, and the widest index range the table can express. Millbrae, the BART
  // transfer hub, stands in wherever a station BETWEEN two others is needed.
  const int sf       = stationIndexByName("San Francisco");
  const int diridon  = stationIndexByName("San Jose Diridon");
  const int millbrae = stationIndexByName("Millbrae");
  const int gilroy   = stationIndexByName("Gilroy");

  check(sf == 0, "San Francisco is the northern terminus");
  check(gilroy == kStationCount - 1, "Gilroy is the southern terminus");
  check(millbrae > sf && millbrae < diridon, "Millbrae sits between the two termini");
  check(stationIndexByName("Atherton") == -1, "closed station is absent");
  check(stationIndexByName("") == -1, "empty name -> -1");
  check(stationIndexByName(nullptr) == -1, "null name -> -1");

  check(stationIndexByBase(7006) == millbrae, "lookup by base code");
  check(stationIndexByBase(7015) == -1, "unused base (old Atherton) -> -1");
  check(stationIndexByBase(0) == -1, "zero base -> -1");

  // --- Direction ------------------------------------------------------------
  // North is a lower index because the table is sorted by descending latitude.
  check(routeDirection(sf, diridon) == DIR_SOUTH, "San Francisco -> Diridon is southbound");
  check(routeDirection(diridon, sf) == DIR_NORTH, "Diridon -> San Francisco is northbound");
  check(routeDirection(sf, gilroy) == DIR_SOUTH, "full line SF -> Gilroy is southbound");

  // --- Platform stop_id -----------------------------------------------------
  // The whole point: the ORIGIN platform is chosen by travel direction.
  check(stationStopId(sf, DIR_SOUTH) == 70012, "San Francisco southbound platform");
  check(stationStopId(sf, DIR_NORTH) == 70011, "San Francisco northbound platform");
  check(stationStopId(diridon, DIR_NORTH) == 70261, "Diridon northbound platform");
  check(stationStopId(diridon, DIR_SOUTH) == 70262, "Diridon southbound platform");
  check(stationStopId(-1, DIR_SOUTH) == 0, "negative index -> 0");
  check(stationStopId(kStationCount, DIR_SOUTH) == 0, "out-of-range index -> 0");

  // A representative southbound route, end to end: origin and destination
  // resolved to the platform stop_ids the feed is actually keyed on.
  {
    const Direction d = routeDirection(sf, diridon);
    check(stationStopId(sf, d) == 70012, "poll 70012 for San Francisco -> Diridon");
    check(stationStopId(diridon, d) == 70262, "arrive on 70262");
  }

  // --- Route validity -------------------------------------------------------
  check(routeValid(sf, diridon), "distinct stations are a valid route");
  check(!routeValid(sf, sf), "same origin and destination is invalid");
  check(!routeValid(-1, diridon), "unset origin is invalid");
  check(!routeValid(sf, kStationCount), "out-of-range destination is invalid");

  // Service-pattern selection by date is tested in test_timetable.cpp, where
  // the generated service table lives.

  if (failures == 0) printf("test_route: all checks passed\n");
  return failures ? 1 : 0;
}
