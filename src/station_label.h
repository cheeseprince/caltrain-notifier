// station_label.h — short station names for a narrow route header.
//
// The 2.8" panel's route header sits beside the clock in 229 px of TFT_eSPI's
// small font. Two station names are what push route pairs onto a second line,
// so on that board the header uses a shorter form of them (layout.h,
// SHORT_STATION_NAMES). Measured over all 870 ordered pairs: 858 fit on one
// line with full names, 864 with these two short forms.
//
// DISPLAY ONLY. stations.h is GENERATED from the GTFS feed and must not be
// edited; the setup portal lists the full names; saved settings store station
// indices, not names; and the 3.5" board shows every name in full.
//
// Header-only and free of Arduino types so the host suite can reach it
// (test/test_station_label.cpp), in the same way as urgency.h.
#pragma once
#include <cstring>

struct StationLabel {
  const char* full;       // exactly as it appears in kStations (stations.h)
  const char* shortName;  // what the 2.8" route header shows instead
};

// One entry per line: tools/gen_screenshot.py reads these with a regex.
inline constexpr StationLabel kStationLabels[] = {
    {"South San Francisco", "S. San Francisco"},
    {"California Avenue", "California Ave"},
};

// The short form of `name` if it has one, otherwise `name` itself — the same
// pointer, so the common case costs nothing. Null comes back as null, so a
// caller can pass kStations[i].name without a separate check.
inline const char* stationHeaderName(const char* name) {
  if (!name) return name;
  for (const StationLabel& l : kStationLabels) {
    if (std::strcmp(l.full, name) == 0) return l.shortName;
  }
  return name;
}
