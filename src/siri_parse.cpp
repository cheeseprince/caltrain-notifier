#include "siri_parse.h"

#include <string.h>

#include <ArduinoJson.h>

namespace {

// Copy a JSON string field into a fixed buffer, always NUL-terminating.
// A field the feed omits leaves an empty string rather than stale bytes.
void copyField(char* dst, size_t cap, JsonVariantConst v) {
  dst[0] = '\0';
  const char* s = v.is<const char*>() ? v.as<const char*>() : nullptr;
  if (!s) return;
  strncpy(dst, s, cap - 1);
  dst[cap - 1] = '\0';
}

// Days since the Unix epoch for a civil date, by Howard Hinnant's days_from_civil.
// Used instead of timegm(), which is not available on every target and which
// mktime() cannot substitute for without dragging in the local timezone.
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);            // [0, 399]
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool readInt(const char*& p, int digits, int& out) {
  int v = 0;
  for (int i = 0; i < digits; i++) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (*p++ - '0');
  }
  out = v;
  return true;
}

}  // namespace

int64_t parseIso8601Utc(const char* s) {
  if (!s) return 0;

  int year, mon, day, hour, min, sec;
  const char* p = s;
  if (!readInt(p, 4, year) || *p++ != '-') return 0;
  if (!readInt(p, 2, mon) || *p++ != '-') return 0;
  if (!readInt(p, 2, day) || (*p != 'T' && *p != ' ')) return 0;
  p++;
  if (!readInt(p, 2, hour) || *p++ != ':') return 0;
  if (!readInt(p, 2, min) || *p++ != ':') return 0;
  if (!readInt(p, 2, sec)) return 0;

  if (mon < 1 || mon > 12 || day < 1 || day > 31) return 0;
  if (hour > 23 || min > 59 || sec > 60) return 0;  // 60 allows a leap second

  int64_t epoch = daysFromCivil(year, (unsigned)mon, (unsigned)day) * 86400
                  + hour * 3600 + min * 60 + sec;

  // Optional fractional seconds, which SIRI may include and we do not need.
  if (*p == '.') {
    p++;
    while (*p >= '0' && *p <= '9') p++;
  }

  // Zone. 'Z' means UTC; an explicit offset must be subtracted to get UTC.
  if (*p == 'Z' || *p == '\0') return epoch;
  if (*p == '+' || *p == '-') {
    const int sign = (*p++ == '+') ? 1 : -1;
    int oh, om;
    if (!readInt(p, 2, oh)) return 0;
    if (*p == ':') p++;
    if (!readInt(p, 2, om)) return 0;

    // readInt bounds oh and om to 0-99 each (two digits), but that leaves
    // "+99:99" and "+45:00" both ACCEPTED and silently shifting a departure
    // by up to ~100 hours -- a wrong-but-plausible board, which is worse than
    // refusing outright. Real UTC offsets run from -12:00 (Baker Island) to
    // +14:00 (Kiribati's Line Islands), and minutes are always 0-59, so
    // refuse anything outside that -- the same "refuse rather than guess"
    // rule already applied to hour/min/sec above.
    if (om > 59) return 0;
    const int totalMinutes = oh * 60 + om;
    if (totalMinutes > 14 * 60) return 0;
    if (sign < 0 && totalMinutes > 12 * 60) return 0;

    return epoch - sign * (oh * 3600 + om * 60);
  }
  return 0;  // some zone format we do not understand: refuse rather than guess
}

SiriResult siriParse(const char* json, size_t len) {
  SiriResult r{};
  r.count = 0;
  r.ok = false;
  r.error = nullptr;

  if (!json || len == 0) {
    r.error = "empty response";
    return r;
  }

  // Tolerate a UTF-8 BOM. The live API does not send one, but a BOM would
  // otherwise fail the entire parse for the sake of three bytes.
  if (len >= 3 && (unsigned char)json[0] == 0xEF && (unsigned char)json[1] == 0xBB &&
      (unsigned char)json[2] == 0xBF) {
    json += 3;
    len -= 3;
  }

  // A filter keeps only the fields used below. The measured response is ~3 KB,
  // so this is not about fitting in RAM — it is about not allocating nodes for
  // vehicle positions, occupancy and bearings that the board never reads.
  JsonDocument filter;
  JsonObject call =
      filter["ServiceDelivery"]["StopMonitoringDelivery"]["MonitoredStopVisit"][0]
            ["MonitoredVehicleJourney"].to<JsonObject>();
  call["LineRef"] = true;
  call["FramedVehicleJourneyRef"]["DatedVehicleJourneyRef"] = true;
  JsonObject mc = call["MonitoredCall"].to<JsonObject>();
  mc["AimedDepartureTime"] = true;
  mc["ExpectedDepartureTime"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, json, len, DeserializationOption::Filter(filter));
  if (err) {
    r.error = "malformed JSON";
    return r;
  }

  JsonArrayConst visits =
      doc["ServiceDelivery"]["StopMonitoringDelivery"]["MonitoredStopVisit"];
  if (visits.isNull()) {
    // A syntactically valid document without the expected envelope. 511 returns
    // this shape for an unknown stop code, so it is a real case, not a
    // hypothetical: parsed fine, no departures, and worth distinguishing from a
    // stop that genuinely has no trains left today.
    r.ok = true;
    r.error = "no StopMonitoringDelivery in response";
    return r;
  }

  r.ok = true;
  for (JsonObjectConst visit : visits) {
    if (r.count >= SIRI_MAX_DEPARTURES) break;

    JsonObjectConst journey = visit["MonitoredVehicleJourney"];
    if (journey.isNull()) continue;
    JsonObjectConst mcall = journey["MonitoredCall"];

    LiveDeparture& d = r.departures[r.count];
    copyField(d.number, sizeof(d.number),
              journey["FramedVehicleJourneyRef"]["DatedVehicleJourneyRef"]);
    copyField(d.route, sizeof(d.route), journey["LineRef"]);
    d.aimedDeparture = parseIso8601Utc(mcall["AimedDepartureTime"]);
    d.expectedDeparture = parseIso8601Utc(mcall["ExpectedDepartureTime"]);

    // A visit with no usable departure time cannot be placed on the board, and
    // showing it with a blank or zero time would be worse than omitting it.
    if (d.aimedDeparture == 0 && d.expectedDeparture == 0) continue;

    r.count++;
  }
  return r;
}
