// service_day.h — mapping the current instant onto Caltrain's service day.
//
// Two conversions the rest of the firmware should never do by hand:
//
// 1. UTC to Pacific. Everything from 511 is UTC; everything a rider thinks
//    about is local, and California observes DST. Rather than hand-roll the
//    rule, this sets the POSIX zone "PST8PDT,M3.2.0,M11.1.0" and lets libc do
//    it — the same code path on the device and in the host tests.
//
// 2. Calendar day to SERVICE day. Caltrain's day does not end at midnight: the
//    feed writes a 00:40 train as 24:40, and it belongs to the previous day's
//    timetable. At 01:00 on a Saturday the trains still running are Friday's,
//    so asking "is it the weekend?" of the calendar gives the wrong answer.
//    The boundary here is 03:00 local, comfortably after the last scheduled
//    train (the latest in the current feed is 26:30 = 02:30) and comfortably
//    before the first (04:37).
#pragma once
#include <stddef.h>
#include <stdint.h>

// The service day containing a given instant.
struct ServiceDay {
  int64_t  startEpoch;  // UTC epoch of the local midnight that begins this service day
  uint32_t date;        // YYYYMMDD of that midnight — feed this to serviceForDate()
  int      dayOfWeek;   // 0 = Sunday .. 6 = Saturday, of that midnight
  int      localHour;   // 0..23, wall-clock hour of `nowEpoch` itself
  int      localMinute; // 0..59
};

// Hour before which we are still in the previous service day.
inline constexpr int SERVICE_DAY_ROLLOVER_HOUR = 3;

// The POSIX zone this project runs in. Exposed because the NTP call must use
// it too: Arduino's configTime(gmtOffset, dstOffset, ...) derives a TZ string
// from its offset arguments and calls setenv("TZ", ...) itself, so calling it
// after serviceDayInitTimezone() silently replaces Pacific with whatever those
// offsets describe. Use configTzTime(SERVICE_DAY_TZ, ...) instead, which takes
// the zone string directly and leaves it intact.
inline constexpr const char* SERVICE_DAY_TZ = "PST8PDT,M3.2.0,M11.1.0";

// Seconds that local time is offset from UTC at `epoch`, e.g. -25200 during
// PDT and -28800 during PST. Used as a start-up self-check: if the zone has
// been clobbered this reads 0, which is a fault worth reporting rather than
// quietly showing UTC on the screen.
int32_t localUtcOffset(int64_t epoch);

// Render an offset from localUtcOffset() as "UTC-7", "UTC+0" or "UTC+5:30".
// Written out on the boot screen so a clobbered zone is visible on the panel
// and not only in the serial log. Truncates rather than overruns; `cap` should
// be at least 12.
void formatUtcOffset(int32_t offsetSec, char* out, size_t cap);

// Install the Pacific timezone. Call once at start-up, before any conversion.
// On the device this also needs an NTP sync to have happened for the clock to
// mean anything; the zone itself is independent of that.
void serviceDayInitTimezone();

// Which service day `nowEpoch` falls in. Requires serviceDayInitTimezone().
ServiceDay serviceDayFor(int64_t nowEpoch);

// The service day after `day`, for the overnight "first train tomorrow" screen.
// Handles the DST transitions, where a local day is 23 or 25 hours long.
ServiceDay nextServiceDay(const ServiceDay& day);
