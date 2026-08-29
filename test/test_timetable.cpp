// Host tests for timetable.{h,cpp} — service selection and schedule lookup
// against the real generated tables.
//
// These run over the committed timetable_data.h, so they assert on genuine
// Caltrain service. That makes them sensitive to a feed regeneration by design:
// if a future schedule breaks an assumption here, it should be a loud test
// failure and not a quietly wrong departure board.
#include <cstdio>
#include <cstring>
#include "timetable.h"

static int failures = 0;
static void check(bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); failures++; } }

int main() {
  const int sf       = stationIndexByName("San Francisco");
  const int paloAlto = stationIndexByName("Palo Alto");
  const int diridon  = stationIndexByName("San Jose Diridon");
  const int collegeP = stationIndexByName("College Park");
  const int gilroy   = stationIndexByName("Gilroy");

  // --- Service selection ----------------------------------------------------
  // tm_wday: 0 = Sunday .. 6 = Saturday.
  check(serviceForDate(20260810, 1) == SVC_WEEKDAY, "ordinary Monday -> weekday");
  check(serviceForDate(20260808, 6) == SVC_WEEKEND, "Saturday -> weekend");
  check(serviceForDate(20260809, 0) == SVC_WEEKEND, "Sunday -> weekend");

  // The distinction that a simple is-it-a-holiday flag would get wrong:
  // five holidays run the weekend timetable, four run a separate reduced one.
  check(serviceForDate(20261225, 5) == SVC_WEEKEND, "Christmas Day -> weekend service");
  check(serviceForDate(20261224, 4) == SVC_HOLIDAY, "Christmas Eve -> reduced holiday service");
  check(serviceForDate(20270127, 3) == SVC_HOLIDAY, "MLK Day -> reduced holiday service");
  check(serviceForDate(20261127, 5) == SVC_HOLIDAY, "day after Thanksgiving -> holiday service");
  check(serviceForDate(20261126, 4) == SVC_WEEKEND, "Thanksgiving Day itself -> weekend service");
  check(serviceForDate(20261228, 1) == SVC_WEEKDAY, "the Monday after Christmas is ordinary");

  // A holiday falling on a weekend must not be promoted to weekday service.
  check(serviceForDate(20270101, 5) == SVC_WEEKEND, "New Year's Day 2027 -> weekend");

  // --- Departures exist on the real schedule --------------------------------
  ScheduledDeparture out[8];

  int n = nextScheduled(sf, diridon, SVC_WEEKDAY, 0, out, 8);
  check(n == 8, "weekday San Francisco -> Diridon fills the request");
  bool ascending = true;
  for (int i = 1; i < n; i++) if (out[i].depMin < out[i - 1].depMin) ascending = false;
  check(ascending, "departures come back in ascending time order");

  // Every returned train must genuinely reach Diridon — this is the express
  // filter, and it is the reason the timetable is on the device at all.
  check(n > 0 && out[0].number != nullptr, "departures carry a train number");
  check(n > 0 && out[0].route != nullptr, "departures carry a service type");

  // --- The express filter, stated directly ----------------------------------
  // College Park is served by only a couple of trains a day, so a naive "next
  // three departures southbound from San Francisco" would list trains that sail
  // straight past it. Anything returned here must actually stop there.
  {
    ScheduledDeparture cp[4];
    int m = nextScheduled(sf, collegeP, SVC_WEEKDAY, 0, cp, 4);
    // Whatever the count, each result must be a trip that truly serves both.
    bool allServe = true;
    for (int i = 0; i < m; i++) {
      bool found = false;
      for (int t = 0; t < kTripCount; t++) {
        if (strcmp(kTrips[t].number, cp[i].number) != 0) continue;
        if (kTrips[t].service != SVC_WEEKDAY) continue;
        uint16_t d = 0;
        if (tripServes(kTrips[t], sf, collegeP, &d) && d == cp[i].depMin) found = true;
      }
      if (!found) allServe = false;
    }
    check(allServe, "every College Park result is a train that stops there");
    // And the sparse stop must return strictly fewer trains than a busy one.
    ScheduledDeparture pa[64];
    int busy = nextScheduled(sf, paloAlto, SVC_WEEKDAY, 0, pa, 64);
    ScheduledDeparture cp2[64];
    int sparse = nextScheduled(sf, collegeP, SVC_WEEKDAY, 0, cp2, 64);
    check(sparse < busy, "College Park is served by fewer trains than Palo Alto");
    check(sparse > 0, "College Park still has some weekday service");
  }

  // --- Earliest-N correctness (the overtaking case) -------------------------
  // Asking for N must return the N EARLIEST qualifying trains, not the first N
  // encountered while scanning. kTrips is ordered by each trip's FIRST stop,
  // which is a different ordering than departure from a mid-line origin.
  //
  // San Jose Diridon -> Tamien is the pair where the two orderings actually
  // diverge in the current feed: South County trains begin at Gilroy, so they
  // sort far from the mainline trains they interleave with at Diridon. A sweep
  // of all 30x30x3 station/service combinations found exactly four divergent
  // pairs, all on this segment — so this is the case to pin.
  {
    const int diridon = stationIndexByName("San Jose Diridon");
    const int tamien  = stationIndexByName("Tamien");
    check(diridon >= 0 && tamien >= 0, "Diridon and Tamien are in the table");

    // The window matters. From midnight the first three trains happen to be
    // the first three scanned, so a broken implementation would still pass.
    // Late afternoon is where the South County trains interleave: scanning from
    // 16:40 encounters 17:13 and 18:13 before it ever reaches the 17:01 train.
    const uint16_t afternoon = 1000;  // 16:40

    ScheduledDeparture all[64], few[3];
    int total = nextScheduled(diridon, tamien, SVC_WEEKDAY, afternoon, all, 64);
    int got   = nextScheduled(diridon, tamien, SVC_WEEKDAY, afternoon, few, 3);
    check(total > 3, "Diridon -> Tamien has more than three trains left at 16:40");
    check(got == 3, "asking for three returns three");

    bool sameThree = true;
    for (int i = 0; i < got; i++) {
      if (few[i].depMin != all[i].depMin) sameThree = false;
      if (strcmp(few[i].number, all[i].number) != 0) sameThree = false;
    }
    check(sameThree, "the three returned are the three earliest, despite scan order");

    // Spelled out, so a failure says what actually went wrong: the third train
    // of the afternoon is the 18:01 South County service, not the 18:13 local
    // that a scan-ordered implementation would reach first.
    check(got == 3 && few[2].depMin == 1081,
          "third afternoon train is the 18:01, not the 18:13");

    // Guard the guard: if a future feed removes the interleaving, this test
    // silently stops testing anything. Assert that the scan order really is
    // different from the departure order for this pair.
    bool scanOrderDiffers = false;
    uint16_t prev = 0;
    for (int t = 0; t < kTripCount; t++) {
      if (kTrips[t].service != SVC_WEEKDAY || kTrips[t].direction != 1) continue;
      uint16_t dep = 0;
      if (!tripServes(kTrips[t], diridon, tamien, &dep)) continue;
      if (dep < prev) scanOrderDiffers = true;
      prev = dep;
    }
    check(scanOrderDiffers,
          "Diridon->Tamien still exercises overtaking (regenerate this test if not)");
  }

  // --- afterMin filtering ---------------------------------------------------
  {
    ScheduledDeparture a[4], b[4];
    int na = nextScheduled(sf, diridon, SVC_WEEKDAY, 0, a, 4);
    check(na > 0, "there is weekday service at all");
    const uint16_t cutoff = (uint16_t)(a[0].depMin + 1);
    int nb = nextScheduled(sf, diridon, SVC_WEEKDAY, cutoff, b, 4);
    check(nb > 0 && b[0].depMin >= cutoff, "afterMin excludes trains already gone");
    check(strcmp(b[0].number, a[0].number) != 0, "the first train drops out past its time");

    // Late enough and nothing is left — the overnight case the board must handle.
    check(nextScheduled(sf, diridon, SVC_WEEKDAY, 2000, b, 4) == 0,
          "no departures after the service day ends");
  }

  // --- Direction is derived, never assumed ---------------------------------
  {
    ScheduledDeparture south[2], north[2];
    check(nextScheduled(sf, diridon, SVC_WEEKDAY, 0, south, 2) > 0, "southbound works");
    check(nextScheduled(diridon, sf, SVC_WEEKDAY, 0, north, 2) > 0, "northbound works");
  }

  // --- First train of the service day --------------------------------------
  {
    ScheduledDeparture first;
    check(firstScheduled(sf, diridon, SVC_WEEKDAY, &first), "weekday has a first train");
    check(first.depMin >= 4 * 60 && first.depMin <= 8 * 60,
          "first weekday train leaves between 04:00 and 08:00");

    ScheduledDeparture wknd;
    if (firstScheduled(sf, diridon, SVC_WEEKEND, &wknd)) {
      check(wknd.depMin > first.depMin, "weekend service starts later than weekday");
    }
  }

  // --- Guard rails ----------------------------------------------------------
  {
    ScheduledDeparture o[2];
    check(nextScheduled(sf, sf, SVC_WEEKDAY, 0, o, 2) == 0, "same station -> none");
    check(nextScheduled(-1, diridon, SVC_WEEKDAY, 0, o, 2) == 0, "bad origin -> none");
    check(nextScheduled(sf, diridon, SVC_WEEKDAY, 0, o, 0) == 0, "maxOut 0 -> none");
    check(nextScheduled(sf, diridon, SVC_WEEKDAY, 0, nullptr, 2) == 0, "null out -> none");
    check(nextScheduled(sf, gilroy, 99, 0, o, 2) == 0, "unknown service -> none");
  }

  // --- timetableExpired() (F-1, 2026-08 adversarial review) -----------------
  // Synthetic bounds rather than the real kTimetableLastOverride/kTimetableFeedEnd,
  // so this keeps testing the right shape of dates even after a feed regeneration
  // moves those constants. lower < upper models the realistic case (the last
  // holiday override runs out before the feed's own declared validity window).
  {
    const uint32_t lower = 20270127;  // stands in for kTimetableLastOverride
    const uint32_t upper = 20270131;  // stands in for kTimetableFeedEnd

    check(!timetableExpired(20270101, lower, upper), "before both bounds -> not expired");
    check(!timetableExpired(lower, lower, upper), "exactly on the earlier bound -> not expired yet");
    check(timetableExpired(20270128, lower, upper), "one day past the earlier bound -> expired");
    check(timetableExpired(20270129, lower, upper), "between the two bounds -> expired (earlier bound governs)");
    check(timetableExpired(upper, lower, upper), "exactly on the later bound -> expired");
    check(timetableExpired(20270201, lower, upper), "after both bounds -> expired");

    // The function must not assume which argument is smaller: same set of
    // dates with the arguments swapped should behave identically.
    check(!timetableExpired(20270101, upper, lower), "swapped args, before both -> not expired");
    check(timetableExpired(20270129, upper, lower), "swapped args, between the two -> expired");
  }

  if (failures == 0) printf("test_timetable: all checks passed\n");
  return failures ? 1 : 0;
}
