#include "service_day.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

namespace {

// Epoch of local midnight on the calendar day containing `t`.
//
// mktime() is the right tool despite its reputation: given a struct tm with
// tm_isdst = -1 it resolves the local-to-UTC offset itself, including the two
// days a year when midnight-to-midnight is not 24 hours. Computing this by
// subtracting seconds-since-midnight would be wrong on exactly those days.
int64_t localMidnight(int64_t t) {
  const time_t tt = (time_t)t;
  struct tm lt;
  localtime_r(&tt, &lt);
  lt.tm_hour = 0;
  lt.tm_min = 0;
  lt.tm_sec = 0;
  lt.tm_isdst = -1;  // let mktime decide; do not assume
  return (int64_t)mktime(&lt);
}

ServiceDay describe(int64_t startEpoch, int64_t nowEpoch) {
  ServiceDay d{};
  d.startEpoch = startEpoch;

  const time_t st = (time_t)startEpoch;
  struct tm lt;
  localtime_r(&st, &lt);
  d.date = (uint32_t)((lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday);
  d.dayOfWeek = lt.tm_wday;

  const time_t nt = (time_t)nowEpoch;
  struct tm nlt;
  localtime_r(&nt, &nlt);
  d.localHour = nlt.tm_hour;
  d.localMinute = nlt.tm_min;
  return d;
}

}  // namespace

void serviceDayInitTimezone() {
  setenv("TZ", SERVICE_DAY_TZ, 1);
  tzset();
}

void formatUtcOffset(int32_t offsetSec, char* out, size_t cap) {
  if (!out || cap == 0) return;

  // Sign is taken from the value before it is made positive, so that offsets
  // inside the first hour west of Greenwich still read as negative: -1800 is
  // "UTC-0:30", not "UTC+0:30".
  const char sign = offsetSec < 0 ? '-' : '+';
  int32_t magnitude = offsetSec < 0 ? -offsetSec : offsetSec;

  const int32_t hours = magnitude / 3600;
  const int32_t minutes = (magnitude % 3600) / 60;

  // Whole hours are the common case and are cleaner without ":00"; the minutes
  // form exists because not every zone is on the hour, and a zone read back as
  // "UTC+5:30" is a far more useful clue than one silently rounded to "UTC+5".
  if (minutes == 0) {
    snprintf(out, cap, "UTC%c%d", sign, (int)hours);
  } else {
    snprintf(out, cap, "UTC%c%d:%02d", sign, (int)hours, (int)minutes);
  }
}

int32_t localUtcOffset(int64_t epoch) {
  const time_t tt = (time_t)epoch;
  struct tm lt, gt;
  localtime_r(&tt, &lt);
  gmtime_r(&tt, &gt);

  // Compare the two renderings of the same instant. Using mktime on the UTC tm
  // would re-apply the local zone, so difference the fields directly instead.
  int32_t diff = (lt.tm_hour - gt.tm_hour) * 3600 + (lt.tm_min - gt.tm_min) * 60 +
                 (lt.tm_sec - gt.tm_sec);
  // Fold a day boundary crossed between the two renderings.
  const int dayDelta = lt.tm_yday - gt.tm_yday;
  if (dayDelta == 1 || dayDelta < -1) diff += 86400;       // local is a day ahead
  else if (dayDelta == -1 || dayDelta > 1) diff -= 86400;  // local is a day behind
  return diff;
}

ServiceDay serviceDayFor(int64_t nowEpoch) {
  const time_t tt = (time_t)nowEpoch;
  struct tm lt;
  localtime_r(&tt, &lt);

  int64_t start = localMidnight(nowEpoch);
  if (lt.tm_hour < SERVICE_DAY_ROLLOVER_HOUR) {
    // Still running yesterday's timetable. Step back a day and re-derive local
    // midnight rather than subtracting 86400 from it, so a DST boundary in
    // between does not shift the result by an hour.
    start = localMidnight(start - 12 * 3600);
  }
  return describe(start, nowEpoch);
}

ServiceDay nextServiceDay(const ServiceDay& day) {
  // Noon of the following day is safely inside it whatever DST does, and
  // localMidnight() then snaps back to that day's true start.
  const int64_t nextNoon = day.startEpoch + 36 * 3600;
  const int64_t start = localMidnight(nextNoon);
  return describe(start, start);
}
