#include "timetable.h"

#include <string.h>

uint8_t serviceForDate(uint32_t yyyymmdd, int dayOfWeek) {
  // An explicit date override beats the weekly pattern. The table is small
  // (nine entries for a year) and sorted, so a linear scan with an early exit
  // costs less than the branch to set up a binary search.
  for (int i = 0; i < kDateOverrideCount; i++) {
    if (kDateOverrides[i].date == yyyymmdd) return kDateOverrides[i].service;
    if (kDateOverrides[i].date > yyyymmdd) break;
  }
  return (dayOfWeek == 0 || dayOfWeek == 6) ? SVC_WEEKEND : SVC_WEEKDAY;
}

bool tripServes(const Trip& trip, int originIdx, int destIdx, uint16_t* depMin) {
  int originPos = -1;
  for (int i = 0; i < trip.nStops; i++) {
    const TripStop& call = kTripStops[trip.firstStop + i];
    if (originPos < 0) {
      if (call.station == originIdx) {
        originPos = i;
        if (depMin) *depMin = call.depMin;
      }
      continue;
    }
    // Past the origin: the first match for the destination settles it.
    if (call.station == destIdx) return true;
  }
  return false;
}

int nextScheduled(int originIdx, int destIdx, uint8_t service, uint16_t afterMin,
                  ScheduledDeparture* out, int maxOut) {
  if (!out || maxOut <= 0) return 0;
  if (!routeValid(originIdx, destIdx)) return 0;

  const uint8_t wantDir = (routeDirection(originIdx, destIdx) == DIR_NORTH) ? 0 : 1;

  // Every matching trip must be examined, not just the first maxOut of them.
  // kTrips is ordered by the departure from each trip's FIRST stop, which is a
  // different ordering than departure from our origin: Caltrain's four-track
  // sections let an Express that left San Francisco later overtake a Local and
  // reach a mid-line origin first. Stopping the scan early would silently
  // return the wrong three trains at exactly the stations where it matters.
  int n = 0;
  for (int t = 0; t < kTripCount; t++) {
    const Trip& trip = kTrips[t];
    if (trip.service != service) continue;
    if (trip.direction != wantDir) continue;

    uint16_t dep = 0;
    if (!tripServes(trip, originIdx, destIdx, &dep)) continue;
    if (dep < afterMin) continue;

    // Full, and this train is no earlier than the latest one held: discard it.
    if (n == maxOut && dep >= out[n - 1].depMin) continue;

    // Insertion sort into the kept set, evicting the latest when full.
    int pos = (n < maxOut) ? n++ : maxOut - 1;
    while (pos > 0 && out[pos - 1].depMin > dep) {
      out[pos] = out[pos - 1];
      pos--;
    }
    out[pos] = ScheduledDeparture{trip.number, kRouteNames[trip.route], dep};
  }
  return n;
}

bool firstScheduled(int originIdx, int destIdx, uint8_t service,
                    ScheduledDeparture* out) {
  return nextScheduled(originIdx, destIdx, service, 0, out, 1) == 1;
}

bool timetableExpired(uint32_t serviceDate, uint32_t lastOverride, uint32_t feedEnd) {
  // The earlier bound governs, not the later one: past kTimetableLastOverride
  // there is no holiday pattern left to consult even if the GTFS feed itself
  // claims to still be valid, and past kTimetableFeedEnd the feed no longer
  // vouches for anything even if the last holiday override happens to be later.
  // min() rather than picking a named argument keeps this correct regardless of
  // which of the two constants happens to be earlier for a given feed.
  const uint32_t threshold = (lastOverride < feedEnd) ? lastOverride : feedEnd;
  return serviceDate > threshold;
}

const Trip* findTripByNumber(const char* number, uint8_t service) {
  if (!number || !*number) return nullptr;

  const Trip* fallback = nullptr;
  for (int t = 0; t < kTripCount; t++) {
    if (strcmp(kTrips[t].number, number) != 0) continue;
    if (kTrips[t].service == service) return &kTrips[t];
    if (!fallback) fallback = &kTrips[t];
  }
  return fallback;
}
