#include "board_model.h"

#include <string.h>

namespace {

// Candidates are gathered before being trimmed to BOARD_ROWS, because a live
// train can be later than a scheduled one and the merge has to see both.
constexpr int MAX_CANDIDATES = SIRI_MAX_DEPARTURES + 8;

void setStr(char* dst, size_t cap, const char* src) {
  dst[0] = '\0';
  if (!src) return;
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

// Index of an existing candidate with this train number, or -1.
int findByNumber(const BoardRow* rows, int n, const char* number) {
  if (!number || !*number) return -1;
  for (int i = 0; i < n; i++) {
    if (strcmp(rows[i].number, number) == 0) return i;
  }
  return -1;
}

}  // namespace

Urgency urgencyFor(int32_t minutesAway) {
  // Boundaries are inclusive at the yellow band: exactly 15 is yellow, and
  // exactly 10 is yellow. Only strictly under 10 is red, and strictly over 15
  // is green. Stated here once so the display cannot drift from the tests.
  if (minutesAway > 15) return URGENCY_GREEN;
  if (minutesAway >= 10) return URGENCY_YELLOW;
  return URGENCY_RED;
}

BoardModel buildBoard(const SiriResult& live, int originIdx, int destIdx,
                      uint8_t service, int64_t nowEpoch,
                      int64_t serviceDayStartEpoch) {
  BoardModel board{};
  board.count = 0;
  board.anyLive = false;
  board.droppedNotServing = 0;
  board.droppedUnknown = 0;

  if (!routeValid(originIdx, destIdx)) return board;

  BoardRow cand[MAX_CANDIDATES];
  int nCand = 0;

  // --- 1. The schedule proposes -------------------------------------------
  // Everything still to come today that actually reaches the destination.
  // Asking for more than BOARD_ROWS leaves headroom for trains the live feed
  // reports as delayed past their scheduled slot.
  {
    const int64_t sinceStart = nowEpoch - serviceDayStartEpoch;
    // Before the service day has begun, start from its first minute rather than
    // a negative offset.
    const uint16_t afterMin =
        sinceStart <= 0 ? 0 : (uint16_t)(sinceStart / 60);

    ScheduledDeparture sched[MAX_CANDIDATES];
    const int n = nextScheduled(originIdx, destIdx, service, afterMin, sched,
                                MAX_CANDIDATES);
    for (int i = 0; i < n && nCand < MAX_CANDIDATES; i++) {
      BoardRow& row = cand[nCand++];
      setStr(row.number, sizeof(row.number), sched[i].number);
      setStr(row.route, sizeof(row.route), sched[i].route);
      row.departure = serviceDayStartEpoch + (int64_t)sched[i].depMin * 60;
      row.delaySec = 0;
      row.isLive = false;
    }
  }

  // --- 2. The live feed corrects -------------------------------------------
  for (int i = 0; i < live.count; i++) {
    const LiveDeparture& d = live.departures[i];

    // Without a prediction there is nothing to correct with; the scheduled row
    // already says the same thing.
    const int64_t when = d.expectedDeparture ? d.expectedDeparture : d.aimedDeparture;
    if (when == 0) continue;

    // Does this train stop at the destination? Only the schedule knows.
    const Trip* trip = findTripByNumber(d.number, service);
    if (!trip) {
      board.droppedUnknown++;
      continue;
    }
    if (!tripServes(*trip, originIdx, destIdx, nullptr)) {
      board.droppedNotServing++;
      continue;
    }

    const int32_t delay =
        (d.expectedDeparture && d.aimedDeparture)
            ? (int32_t)(d.expectedDeparture - d.aimedDeparture)
            : 0;

    const int existing = findByNumber(cand, nCand, d.number);
    if (existing >= 0) {
      // Upgrade the scheduled row in place, keeping its position in the list.
      cand[existing].departure = when;
      cand[existing].delaySec = delay;
      cand[existing].isLive = true;
      if (d.route[0]) setStr(cand[existing].route, sizeof(cand[existing].route), d.route);
      continue;
    }

    // A live train the schedule scan did not produce. This is normal near the
    // boundary: a train running late can still be listed live after its
    // scheduled minute has passed, and afterMin has already excluded it.
    if (nCand < MAX_CANDIDATES) {
      BoardRow& row = cand[nCand++];
      setStr(row.number, sizeof(row.number), d.number);
      setStr(row.route, sizeof(row.route), d.route[0] ? d.route : kRouteNames[trip->route]);
      row.departure = when;
      row.delaySec = delay;
      row.isLive = true;
    }
  }

  // --- 3. Drop what has already gone, then take the earliest three ----------
  // Sorting by insertion: the list is at most a couple of dozen entries and
  // this keeps the whole module allocation-free.
  for (int i = 1; i < nCand; i++) {
    BoardRow key = cand[i];
    int j = i - 1;
    while (j >= 0 && cand[j].departure > key.departure) {
      cand[j + 1] = cand[j];
      j--;
    }
    cand[j + 1] = key;
  }

  for (int i = 0; i < nCand && board.count < BOARD_ROWS; i++) {
    if (cand[i].departure < nowEpoch) continue;  // already left

    BoardRow row = cand[i];
    // Floor, not round: "12 min" should mean at least twelve, so a rider who
    // leaves now is never later than the number promised.
    row.minutesAway = (int32_t)((row.departure - nowEpoch) / 60);
    if (row.minutesAway < 0) row.minutesAway = 0;
    row.urgency = urgencyFor(row.minutesAway);

    board.rows[board.count++] = row;
    if (row.isLive) board.anyLive = true;
  }

  return board;
}
