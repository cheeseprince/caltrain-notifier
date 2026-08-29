// route.h — turning "from here to there" into the things the 511 API needs.
//
// Pure logic, no Arduino headers: this compiles and is tested on the host.
//
// A rider picks two STATIONS by name. Caltrain's realtime feed is keyed on
// PLATFORMS, and each station has two — one per direction. Deriving the platform
// from the direction of travel (rather than having anyone type a stop_id) is the
// single rule that keeps the board pointed at trains going the right way.
#pragma once
#include <stdint.h>

#include "stations.h"

enum Direction {
  DIR_NORTH,  // toward San Francisco  — platform stop_ids end in 1
  DIR_SOUTH,  // toward Gilroy         — platform stop_ids end in 2
};

// Index into kStations, or -1 if there is no such station.
// Name matching is exact and case-sensitive: the only callers are the config
// store and the setup portal, both of which hand back a string that came from
// kStations in the first place.
int stationIndexByName(const char* name);
int stationIndexByBase(uint16_t base);

// Direction of travel from one station to another. kStations runs north to
// south, so a larger destination index means heading south.
// Undefined for equal indices — callers gate on routeValid() first.
Direction routeDirection(int originIdx, int destIdx);

// The 5-digit GTFS platform stop_id to poll, e.g. (San Francisco, DIR_SOUTH)
// -> 70012. Returns 0 if the index is out of range.
//
// This is the ONLY place the base*10 + direction-digit encoding is applied.
uint32_t stationStopId(int stationIdx, Direction dir);

// True when both indices are in range and refer to different stations.
bool routeValid(int originIdx, int destIdx);

// Which timetable applies on a given date lives in timetable.h, not here:
// it depends on the generated service table, and Caltrain runs three patterns
// rather than the two a "weekend or not" predicate could express.
