// Host tests for service_day.{h,cpp}.
//
// These exercise the two things that are easy to get subtly wrong and hard to
// notice in the field: US Pacific daylight saving, and Caltrain's service day
// running past midnight.
//
// The DST instants below were cross-checked with `TZ=America/Los_Angeles date`
// rather than derived by hand.
#include <cstdio>
#include <cstring>
#include <ctime>
#include "service_day.h"
#include "timetable.h"

static int failures = 0;
static void check(bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); failures++; } }

// Build a UTC epoch from civil UTC parts, so tests can name an instant exactly.
static int64_t utc(int y, int mon, int d, int h, int mi) {
  struct tm t{};
  t.tm_year = y - 1900; t.tm_mon = mon - 1; t.tm_mday = d;
  t.tm_hour = h; t.tm_min = mi; t.tm_sec = 0;
  return (int64_t)timegm(&t);
}

int main() {
  serviceDayInitTimezone();

  // --- The UTC offset is real ----------------------------------------------
  // This is the regression guard for a bug that shipped: configTime(0, 0, ...)
  // derives a TZ string from its offset arguments and calls setenv("TZ"),
  // silently replacing Pacific with UTC. The screen then showed 6:46pm when it
  // was 11:46am, with nothing on the display to say why.
  //
  // An offset of zero means the zone did not take. Asserting the real values
  // catches both that and a wrong zone.
  {
    check(localUtcOffset(utc(2026, 8, 8, 18, 0)) == -7 * 3600,
          "August is PDT, UTC-7");
    check(localUtcOffset(utc(2026, 1, 15, 20, 0)) == -8 * 3600,
          "January is PST, UTC-8");
    check(localUtcOffset(utc(2026, 8, 8, 18, 0)) != 0,
          "the offset is never zero — that would mean the clock is reading UTC");

    // Across a UTC day boundary, where the local date and the UTC date differ.
    check(localUtcOffset(utc(2026, 8, 9, 2, 0)) == -7 * 3600,
          "02:00 UTC still resolves to UTC-7 despite being the previous day locally");
    check(localUtcOffset(utc(2026, 1, 2, 3, 0)) == -8 * 3600,
          "03:00 UTC on Jan 2 is UTC-8 on Jan 1 locally");
  }

  // --- Basic local conversion ----------------------------------------------
  {
    // 2026-08-08 18:00 UTC is 11:00 PDT (UTC-7) on a Saturday.
    ServiceDay d = serviceDayFor(utc(2026, 8, 8, 18, 0));
    check(d.localHour == 11, "18:00 UTC in August is 11:00 Pacific");
    check(d.date == 20260808, "service date is the calendar date");
    check(d.dayOfWeek == 6, "2026-08-08 is a Saturday");
    check(serviceForDate(d.date, d.dayOfWeek) == SVC_WEEKEND, "and runs weekend service");
  }
  {
    // 2026-01-15 20:00 UTC is 12:00 PST (UTC-8) on a Thursday.
    ServiceDay d = serviceDayFor(utc(2026, 1, 15, 20, 0));
    check(d.localHour == 12, "20:00 UTC in January is 12:00 Pacific");
    check(d.dayOfWeek == 4, "2026-01-15 is a Thursday");
    check(serviceForDate(d.date, d.dayOfWeek) == SVC_WEEKDAY, "and runs weekday service");
  }

  // --- The service day runs past midnight -----------------------------------
  {
    // 01:30 Pacific on Saturday 2026-08-08 = 08:30 UTC. Trains running then are
    // Friday's, so the service day must still be Friday the 7th.
    ServiceDay d = serviceDayFor(utc(2026, 8, 8, 8, 30));
    check(d.localHour == 1, "08:30 UTC is 01:30 Pacific");
    check(d.date == 20260807, "01:30 Saturday still belongs to Friday's service day");
    check(d.dayOfWeek == 5, "which is a Friday");
    check(serviceForDate(d.date, d.dayOfWeek) == SVC_WEEKDAY,
          "so late-Friday-night trains use the WEEKDAY timetable");
  }
  {
    // 03:30 Pacific is past the rollover: now it is genuinely Saturday.
    ServiceDay d = serviceDayFor(utc(2026, 8, 8, 10, 30));
    check(d.localHour == 3, "10:30 UTC is 03:30 Pacific");
    check(d.date == 20260808, "past 03:00 the service day has rolled over");
    check(serviceForDate(d.date, d.dayOfWeek) == SVC_WEEKEND, "and weekend service applies");
  }

  // --- startEpoch really is local midnight ----------------------------------
  {
    ServiceDay d = serviceDayFor(utc(2026, 8, 8, 18, 0));
    const time_t st = (time_t)d.startEpoch;
    struct tm lt;
    localtime_r(&st, &lt);
    check(lt.tm_hour == 0 && lt.tm_min == 0 && lt.tm_sec == 0,
          "startEpoch lands exactly on local midnight");
    check(lt.tm_mday == 8, "on the right day");

    // A scheduled 07:03 train must map back to 07:03 local, which is the whole
    // contract board_model relies on.
    const int64_t sevenOhThree = d.startEpoch + (7 * 60 + 3) * 60;
    const time_t s2 = (time_t)sevenOhThree;
    localtime_r(&s2, &lt);
    check(lt.tm_hour == 7 && lt.tm_min == 3, "depMin offsets from startEpoch give local time");
  }

  // --- DST transitions ------------------------------------------------------
  // The short and long days run FROM the transition day TO the next one: the
  // clocks move at 02:00 on the transition day, so it is that day's midnight-to
  // -midnight span that is 23 or 25 hours, not the one leading into it.
  // Verified against `TZ=America/Los_Angeles date -d '<day> 00:00:00' +%s`.
  //
  // Spring forward: 2026-03-08, 02:00 PST -> 03:00 PDT.
  {
    ServiceDay before = serviceDayFor(utc(2026, 3, 7, 20, 0));  // Sat 12:00 PST
    ServiceDay onDay  = serviceDayFor(utc(2026, 3, 8, 20, 0));  // Sun 13:00 PDT
    check(before.date == 20260307, "day before spring forward");
    check(onDay.date == 20260308, "the spring-forward day itself");
    check(onDay.startEpoch - before.startEpoch == 24 * 3600,
          "the day leading into spring forward is a normal 24 hours");
    check(nextServiceDay(onDay).startEpoch - onDay.startEpoch == 23 * 3600,
          "the spring-forward day itself is 23 hours long");
    check(onDay.localHour == 13, "20:00 UTC is 13:00 PDT after the switch");
  }
  // Fall back: 2026-11-01, 02:00 PDT -> 01:00 PST.
  {
    ServiceDay before = serviceDayFor(utc(2026, 10, 31, 19, 0));  // Sat 12:00 PDT
    ServiceDay onDay  = serviceDayFor(utc(2026, 11, 1, 20, 0));   // Sun 12:00 PST
    check(before.date == 20261031, "day before fall back");
    check(onDay.date == 20261101, "the fall-back day itself");
    check(onDay.startEpoch - before.startEpoch == 24 * 3600,
          "the day leading into fall back is a normal 24 hours");
    check(nextServiceDay(onDay).startEpoch - onDay.startEpoch == 25 * 3600,
          "the fall-back day itself is 25 hours long");
    check(onDay.localHour == 12, "20:00 UTC is 12:00 PST after the switch");
  }

  // --- nextServiceDay across DST -------------------------------------------
  {
    ServiceDay d = serviceDayFor(utc(2026, 3, 7, 20, 0));  // Sat before spring forward
    ServiceDay n = nextServiceDay(d);
    check(n.date == 20260308, "next day is the 8th");
    check(n.dayOfWeek == 0, "which is a Sunday");
    const time_t st = (time_t)n.startEpoch;
    struct tm lt;
    localtime_r(&st, &lt);
    check(lt.tm_hour == 0, "and still starts at local midnight, not 01:00");
  }
  {
    ServiceDay d = serviceDayFor(utc(2026, 10, 31, 19, 0));  // Sat before fall back
    ServiceDay n = nextServiceDay(d);
    check(n.date == 20261101, "next day is Nov 1");
    const time_t st = (time_t)n.startEpoch;
    struct tm lt;
    localtime_r(&st, &lt);
    check(lt.tm_hour == 0, "and starts at local midnight, not 23:00 the day before");
  }
  {
    // Month and year rollovers.
    ServiceDay dec31 = serviceDayFor(utc(2026, 12, 31, 20, 0));
    ServiceDay jan1  = nextServiceDay(dec31);
    check(dec31.date == 20261231, "new year's eve");
    check(jan1.date == 20270101, "rolls into 2027");
    check(serviceForDate(jan1.date, jan1.dayOfWeek) == SVC_WEEKEND,
          "New Year's Day runs weekend service");
  }

  // --- A holiday reached through the real clock path ------------------------
  {
    // Christmas Eve 2026 is a Thursday: an ordinary weekday by the calendar,
    // but Caltrain runs its reduced holiday timetable.
    ServiceDay d = serviceDayFor(utc(2026, 12, 24, 20, 0));
    check(d.date == 20261224, "Christmas Eve");
    check(d.dayOfWeek == 4, "is a Thursday");
    check(serviceForDate(d.date, d.dayOfWeek) == SVC_HOLIDAY,
          "and runs the reduced holiday timetable, not weekday service");
  }

  // --- Rendering that offset for the boot screen ---------------------------
  // The clock checklist prints the offset it actually observes, so a clobbered
  // zone is visible on the panel rather than only in the serial log. This is
  // the only part of that screen a host test can reach.
  {
    char buf[16];

    formatUtcOffset(-7 * 3600, buf, sizeof(buf));
    check(strcmp(buf, "UTC-7") == 0, "PDT renders as UTC-7");

    formatUtcOffset(-8 * 3600, buf, sizeof(buf));
    check(strcmp(buf, "UTC-8") == 0, "PST renders as UTC-8");

    // Zero is the failure this screen exists to surface: it means the zone did
    // not apply and the panel is about to show UTC.
    formatUtcOffset(0, buf, sizeof(buf));
    check(strcmp(buf, "UTC+0") == 0, "a clobbered zone renders as UTC+0");

    // Not every zone is on the hour. Rounding these away would turn a useful
    // clue into a misleading one.
    formatUtcOffset(5 * 3600 + 30 * 60, buf, sizeof(buf));
    check(strcmp(buf, "UTC+5:30") == 0, "half-hour zones keep their minutes");

    formatUtcOffset(-(9 * 3600 + 30 * 60), buf, sizeof(buf));
    check(strcmp(buf, "UTC-9:30") == 0, "and so do negative half-hour zones");

    // The sign comes from the original value, not from the hour count, so an
    // offset inside the first hour west of Greenwich stays negative.
    formatUtcOffset(-30 * 60, buf, sizeof(buf));
    check(strcmp(buf, "UTC-0:30") == 0, "sub-hour negative offsets stay negative");

    // Real offsets from the real clock, not just hand-written constants.
    formatUtcOffset(localUtcOffset(utc(2026, 8, 8, 18, 0)), buf, sizeof(buf));
    check(strcmp(buf, "UTC-7") == 0, "August through the live path is UTC-7");
  }

  if (failures == 0) printf("test_service_day: all checks passed\n");
  return failures ? 1 : 0;
}
