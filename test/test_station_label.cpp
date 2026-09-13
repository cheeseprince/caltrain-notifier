// Host coverage for station_label.h — the short station names the 2.8" panel
// uses in its route header.
//
// Display only, but it still has to be right. A short form that stops matching
// its station — because tools/gen_stations.py regenerated stations.h with a
// new spelling — would silently put the long name back and wrap the header,
// and a mapping that caught the wrong station would mislabel the sign.
#include <cstdio>
#include <cstring>

#include "../src/station_label.h"
#include "../src/stations.h"

namespace {
int failures = 0;
void check(bool cond, const char* what) {
  if (!cond) { std::printf("FAIL: %s\n", what); failures++; }
}

bool isStation(const char* name) {
  for (int i = 0; i < kStationCount; i++) {
    if (std::strcmp(kStations[i].name, name) == 0) return true;
  }
  return false;
}
}  // namespace

int main() {
  // --- The two short forms --------------------------------------------------
  check(std::strcmp(stationHeaderName("South San Francisco"), "S. San Francisco") == 0,
        "South San Francisco is shortened to S. San Francisco");
  check(std::strcmp(stationHeaderName("California Avenue"), "California Ave") == 0,
        "California Avenue is shortened to California Ave");

  // --- The generated table still has both full names -------------------------
  // If a timetable regeneration renames either station, this is where it shows.
  for (const StationLabel& l : kStationLabels) {
    char what[96];
    std::snprintf(what, sizeof(what), "\"%s\" is still a station in stations.h", l.full);
    check(isStation(l.full), what);
    std::snprintf(what, sizeof(what), "\"%s\" is shorter than \"%s\"", l.shortName, l.full);
    check(std::strlen(l.shortName) < std::strlen(l.full), what);
  }

  // --- Every other station passes through untouched --------------------------
  // The same pointer back, not merely the same text.
  int shortened = 0;
  for (int i = 0; i < kStationCount; i++) {
    if (stationHeaderName(kStations[i].name) != kStations[i].name) shortened++;
  }
  check(shortened == 2, "exactly two stations are shortened");

  // --- Edge input ------------------------------------------------------------
  check(stationHeaderName(nullptr) == nullptr, "null comes back as null");
  const char* empty = "";
  check(stationHeaderName(empty) == empty, "an empty name comes back unchanged");
  // Matching is exact: a name that merely starts like a mapped one is not it.
  const char* longer = "South San Francisco Airport";
  check(stationHeaderName(longer) == longer, "a longer name with a mapped prefix is not shortened");

  if (failures == 0) std::printf("test_station_label: ALL PASS\n");
  return failures ? 1 : 0;
}
