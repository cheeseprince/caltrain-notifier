// board_dump — print the board model as JSON, for tools/gen_screenshot.py.
//
// Host-only. Links the same pure modules the firmware does, so what this prints
// is what the device would show: the compiled timetable decides which trains
// belong on the board, the captured 511 response corrects when they leave, and
// the express filter drops any train that does not stop at the destination.
//
//   g++ -std=c++17 -Isrc -Ithird_party tools/board_dump.cpp
//       src/board_model.cpp src/timetable.cpp src/route.cpp src/siri_parse.cpp
//       -o /tmp/board_dump
//   /tmp/board_dump "San Francisco" "San Jose Diridon" test/fixtures/<capture>.json
//
// Nothing here is a second implementation of the board. It is a printf around
// buildBoard(), which is the point: a screenshot generated from a reimplementation
// would be a drawing of what someone believed the firmware does.
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#include "board_model.h"
#include "timetable.h"
#include "route.h"
#include "siri_parse.h"

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: board_dump <origin> <destination> <capture.json>\n");
    return 2;
  }
  const int originIdx = stationIndexByName(argv[1]);
  const int destIdx = stationIndexByName(argv[2]);
  if (originIdx < 0 || destIdx < 0) {
    fprintf(stderr, "unknown station name\n");
    return 2;
  }

  std::ifstream f(argv[3]);
  if (!f) { fprintf(stderr, "cannot read %s\n", argv[3]); return 2; }
  std::stringstream ss; ss << f.rdbuf();
  const std::string body = ss.str();
  const SiriResult live = siriParse(body.c_str(), body.size());
  if (!live.ok || live.count == 0) { fprintf(stderr, "capture did not parse\n"); return 2; }

  // Anchor "now" to the capture itself: one minute before its first departure.
  // A screenshot pinned to a wall-clock reading would go stale the moment it was
  // taken, and the countdowns would not correspond to the times beside them.
  const int64_t firstDep = live.departures[0].expectedDeparture
                               ? live.departures[0].expectedDeparture
                               : live.departures[0].aimedDeparture;
  const int64_t nowEpoch = firstDep - 9 * 60;

  // Local midnight beginning the service day that contains nowEpoch.
  const time_t tt = (time_t)nowEpoch;
  struct tm lt; localtime_r(&tt, &lt);
  struct tm mid = lt; mid.tm_hour = 0; mid.tm_min = 0; mid.tm_sec = 0; mid.tm_isdst = -1;
  const int64_t dayStart = (int64_t)mktime(&mid);

  const uint8_t service = serviceForDate(
      (mid.tm_year + 1900) * 10000 + (mid.tm_mon + 1) * 100 + mid.tm_mday, mid.tm_wday);

  const BoardModel m = buildBoard(live, originIdx, destIdx, service, nowEpoch, dayStart);

  char clockStr[8];
  strftime(clockStr, sizeof(clockStr), "%H:%M", &lt);

  printf("{\n");
  printf("  \"origin\": \"%s\",\n", argv[1]);
  printf("  \"destination\": \"%s\",\n", argv[2]);
  printf("  \"clock\": \"%s\",\n", clockStr);
  printf("  \"service\": \"%s\",\n", kServiceNames[service]);
  printf("  \"anyLive\": %s,\n", m.anyLive ? "true" : "false");
  printf("  \"droppedNotServing\": %d,\n", m.droppedNotServing);
  printf("  \"droppedUnknown\": %d,\n", m.droppedUnknown);
  printf("  \"rows\": [\n");
  for (int i = 0; i < m.count; i++) {
    const BoardRow& r = m.rows[i];
    const time_t dt = (time_t)r.departure;
    struct tm dl; localtime_r(&dt, &dl);
    char when[8]; strftime(when, sizeof(when), "%H:%M", &dl);
    printf("    {\"number\": \"%s\", \"route\": \"%s\", \"when\": \"%s\", "
           "\"minutesAway\": %d, \"delaySec\": %d, \"isLive\": %s, \"urgency\": %d}%s\n",
           r.number, r.route, when, (int)r.minutesAway, (int)r.delaySec,
           r.isLive ? "true" : "false", (int)r.urgency, i + 1 < m.count ? "," : "");
  }
  printf("  ]\n}\n");
  return 0;
}
