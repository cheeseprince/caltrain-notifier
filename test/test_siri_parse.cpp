// Host tests for siri_parse.{h,cpp}.
//
// The happy path runs against test/fixtures/stopmonitoring_70012.json, which is
// a verbatim response captured from the live 511 API on 2026-08-18 for San
// Francisco southbound — the northern terminus, chosen because a terminus is
// the canonical example on any line. Testing against a real capture rather than
// a hand-written sample is the point: it is the actual field names, the actual
// time format, and the actual three-departure cap that the firmware has to cope
// with.
//
// Properties that are ACCIDENTS OF THIS CAPTURE — the specific train numbers,
// and the fact that every train here happens to be exactly on time — are
// asserted against the fixture. Behaviour that must hold for ANY response, such
// as a late train producing a positive delay, is exercised with synthetic JSON
// below, so that re-capturing the fixture cannot silently drop the coverage.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "siri_parse.h"

static int failures = 0;
static void check(bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); failures++; } }

static std::string slurp(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return {};
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  fclose(f);
  return out;
}

static SiriResult parseStr(const std::string& s) { return siriParse(s.c_str(), s.size()); }

int main() {
  // --- ISO-8601 ------------------------------------------------------------
  // Values cross-checked against `date -u -d '<stamp>' +%s`, not derived by hand.
  check(parseIso8601Utc("2026-08-08T18:00:00Z") == 1786212000, "UTC instant");
  // 1970-01-01 now hits the year-bound check below (added after fuzzing found
  // the year was unbounded) and is refused for being outside 2000-2100 --
  // still 0, but for a different reason than "this is genuinely epoch zero".
  // Kept here, reworded, rather than deleted, so the case is not silently
  // dropped by a future refactor.
  check(parseIso8601Utc("1970-01-01T00:00:00Z") == 0, "pre-2000 epoch itself is now refused, not computed");
  check(parseIso8601Utc("2026-01-01T00:00:00Z") == 1767225600, "new year 2026");

  // An explicit offset must be converted, not ignored. The stoptimetable
  // endpoint returns local times in exactly this form.
  check(parseIso8601Utc("2026-08-08T11:00:00-07:00") == parseIso8601Utc("2026-08-08T18:00:00Z"),
        "-07:00 offset equals the same instant in Z");
  check(parseIso8601Utc("2026-08-08T19:00:00+01:00") == parseIso8601Utc("2026-08-08T18:00:00Z"),
        "+01:00 offset converts the other way");

  // Leap years, since a naive day count gets these wrong.
  check(parseIso8601Utc("2028-03-01T00:00:00Z") - parseIso8601Utc("2028-02-28T00:00:00Z") == 2 * 86400,
        "2028 is a leap year: Feb 28 -> Mar 1 is two days");
  check(parseIso8601Utc("2027-03-01T00:00:00Z") - parseIso8601Utc("2027-02-28T00:00:00Z") == 86400,
        "2027 is not a leap year");
  check(parseIso8601Utc("2100-03-01T00:00:00Z") - parseIso8601Utc("2100-02-28T00:00:00Z") == 86400,
        "2100 is not a leap year despite being divisible by 4");

  // Rejections. Returning 0 rather than a wrong instant is what matters.
  check(parseIso8601Utc(nullptr) == 0, "null -> 0");
  check(parseIso8601Utc("") == 0, "empty -> 0");
  check(parseIso8601Utc("not a time") == 0, "garbage -> 0");
  check(parseIso8601Utc("2026-08-08") == 0, "date without time -> 0");
  check(parseIso8601Utc("2026-13-08T18:00:00Z") == 0, "month 13 -> 0");
  check(parseIso8601Utc("2026-08-08T25:00:00Z") == 0, "hour 25 -> 0");
  check(parseIso8601Utc("2026-08-08T18:00:00 PDT") == 0, "unknown zone -> 0, not a guess");
  check(parseIso8601Utc("2026-08-08T18:00:00.500Z") == parseIso8601Utc("2026-08-08T18:00:00Z"),
        "fractional seconds are ignored, not fatal");

  // Offset validation. Found by direct test: an out-of-range offset was
  // ACCEPTED and silently applied, shifting a departure by up to ~100 hours
  // while still returning a non-zero (looks-valid) epoch -- a confidently
  // wrong departure board, worse than refusing outright.
  check(parseIso8601Utc("2026-08-29T12:00:00+99:99") == 0, "nonsense offset +99:99 -> 0");
  check(parseIso8601Utc("2026-08-29T12:00:00+45:00") == 0, "out-of-range offset +45:00 -> 0");

  // Boundaries: -12:00 (Baker Island) and +14:00 (Line Islands) are real,
  // in-use UTC offsets and must still be accepted.
  check(parseIso8601Utc("2026-08-29T12:00:00-12:00") == parseIso8601Utc("2026-08-30T00:00:00Z"),
        "-12:00 is the most negative real offset and is accepted");
  check(parseIso8601Utc("2026-08-29T12:00:00+14:00") == parseIso8601Utc("2026-08-28T22:00:00Z"),
        "+14:00 is the most positive real offset and is accepted");

  // One minute past either boundary is refused, not clamped.
  check(parseIso8601Utc("2026-08-29T12:00:00+14:01") == 0, "+14:01 is one minute past the max -> 0");
  check(parseIso8601Utc("2026-08-29T12:00:00-12:01") == 0, "-12:01 is one minute past the min -> 0");

  // The minutes component of an offset is still a minutes value: 0-59, same
  // as the minutes-of-the-hour checked earlier in the function.
  check(parseIso8601Utc("2026-08-29T12:00:00+05:75") == 0, "offset minutes 75 -> 0");

  // Year validation. Fuzzing (test/fuzz/fuzz_siri_parse.cpp) found that year
  // was the one date/time field left unbounded: "0001" produced a huge
  // negative epoch that board_model.cpp turned into a phantom RED "departing
  // now" row, and "2227" produced a meaningless far-future GREEN one. Both
  // are syntactically valid ISO-8601 and were silently accepted before this
  // guard existed.
  check(parseIso8601Utc("0001-01-01T00:00:00Z") == 0, "year 0001 -> 0, not a phantom negative epoch");
  check(parseIso8601Utc("2227-08-18T04:55:00Z") == 0, "year 2227 -> 0, not a meaningless far-future epoch");

  // Boundaries: 2000 and 2100 are the edges of the accepted window and must
  // still parse; one year past either edge must be refused, not clamped --
  // the same pattern already used for the UTC-offset boundaries above.
  check(parseIso8601Utc("2000-01-01T00:00:00Z") == 946684800, "year 2000 is the minimum accepted year");
  check(parseIso8601Utc("2100-01-01T00:00:00Z") == 4102444800, "year 2100 is the maximum accepted year");
  check(parseIso8601Utc("1999-12-31T23:59:59Z") == 0, "year 1999 is one year below the minimum -> 0");
  check(parseIso8601Utc("2101-01-01T00:00:00Z") == 0, "year 2101 is one year above the maximum -> 0");

  // Existing valid offsets from before this fix must still work unchanged.
  check(parseIso8601Utc("2026-08-08T11:00:00-07:00") == parseIso8601Utc("2026-08-08T18:00:00Z"),
        "-07:00 still accepted after adding offset validation");
  check(parseIso8601Utc("2026-08-08T19:00:00+01:00") == parseIso8601Utc("2026-08-08T18:00:00Z"),
        "+01:00 still accepted after adding offset validation");
  check(parseIso8601Utc("2026-08-08T18:00:00Z") == 1786212000, "Z form unaffected by offset validation");

  // --- The real captured response -------------------------------------------
  const std::string live = slurp("fixtures/stopmonitoring_70012.json");
  check(!live.empty(), "fixture loads");

  SiriResult r = parseStr(live);
  check(r.ok, "live capture parses");
  check(r.count == 3, "live capture yields the feed's three departures");

  if (r.count == 3) {
    check(strcmp(r.departures[0].number, "164") == 0, "first train is 164");
    check(strcmp(r.departures[1].number, "166") == 0, "second train is 166");
    check(strcmp(r.departures[2].number, "168") == 0, "third train is 168");
    check(strcmp(r.departures[0].route, "Local Weekday") == 0, "service type carried through");

    check(r.departures[0].aimedDeparture == parseIso8601Utc("2026-08-18T03:55:00Z"),
          "aimed departure matches the capture");
    check(r.departures[0].expectedDeparture == parseIso8601Utc("2026-08-18T03:55:00Z"),
          "expected departure matches the capture");

    // In THIS capture every train is running to time, so the delay the board
    // would display is zero. That is a fact about the capture, not about the
    // parser — the late-train path is exercised with synthetic JSON below.
    check(r.departures[0].expectedDeparture - r.departures[0].aimedDeparture == 0,
          "train 164 is on time in this capture");

    bool ascending = true;
    for (int i = 1; i < r.count; i++) {
      if (r.departures[i].aimedDeparture <= r.departures[i - 1].aimedDeparture) ascending = false;
    }
    check(ascending, "the feed's departures are in time order");
  }

  // --- A late train ---------------------------------------------------------
  // Delay is expected minus aimed, and the board colours on it. The captured
  // fixture happens to contain no late trains, so this is synthetic on purpose:
  // a property that must hold for any response should not depend on the weather
  // at the moment somebody ran the probe.
  {
    const std::string j = R"({"ServiceDelivery":{"StopMonitoringDelivery":{
      "MonitoredStopVisit":[{"MonitoredVehicleJourney":{
        "LineRef":"Local Weekday",
        "FramedVehicleJourneyRef":{"DatedVehicleJourneyRef":"166"},
        "MonitoredCall":{"AimedDepartureTime":"2026-08-18T04:25:00Z",
                         "ExpectedDepartureTime":"2026-08-18T04:27:30Z"}}}]}}})";
    SiriResult late = parseStr(j);
    check(late.ok && late.count == 1, "a late visit parses");
    check(late.departures[0].expectedDeparture - late.departures[0].aimedDeparture == 150,
          "a train 2.5 minutes down reports 150 seconds of delay");
  }

  // --- Missing live prediction ---------------------------------------------
  // A scheduled train with no prediction yet: aimed present, expected absent.
  {
    const std::string j = R"({"ServiceDelivery":{"StopMonitoringDelivery":{
      "MonitoredStopVisit":[{"MonitoredVehicleJourney":{
        "LineRef":"Express",
        "FramedVehicleJourneyRef":{"DatedVehicleJourneyRef":"512"},
        "MonitoredCall":{"AimedDepartureTime":"2026-08-08T18:00:00Z"}}}]}}})";
    SiriResult m = parseStr(j);
    check(m.ok && m.count == 1, "a visit without a prediction is still a departure");
    check(m.departures[0].expectedDeparture == 0, "absent prediction reads as 0, not garbage");
    check(m.departures[0].aimedDeparture != 0, "aimed time still parsed");
  }

  // A visit with neither time is unplaceable and must be dropped, not shown
  // with a blank or an epoch-zero time.
  {
    const std::string j = R"({"ServiceDelivery":{"StopMonitoringDelivery":{
      "MonitoredStopVisit":[
        {"MonitoredVehicleJourney":{"FramedVehicleJourneyRef":{"DatedVehicleJourneyRef":"1"},
         "MonitoredCall":{}}},
        {"MonitoredVehicleJourney":{"FramedVehicleJourneyRef":{"DatedVehicleJourneyRef":"2"},
         "MonitoredCall":{"AimedDepartureTime":"2026-08-08T18:00:00Z"}}}]}}})";
    SiriResult m = parseStr(j);
    check(m.ok && m.count == 1, "timeless visit dropped, the usable one kept");
    check(strcmp(m.departures[0].number, "2") == 0, "the kept one is the right one");
  }

  // --- Degenerate and hostile input ----------------------------------------
  {
    SiriResult m = parseStr("");
    check(!m.ok && m.count == 0, "empty string is not ok");
    check(siriParse(nullptr, 10).ok == false, "null pointer is not ok");

    m = parseStr("{ this is not json");
    check(!m.ok && m.count == 0, "malformed JSON is not ok");

    m = parseStr("[]");
    check(m.ok && m.count == 0, "valid JSON of the wrong shape parses to zero departures");

    // 511's reply for an unknown stop code: valid JSON, empty visit list.
    m = parseStr(R"({"ServiceDelivery":{"StopMonitoringDelivery":{"MonitoredStopVisit":[]}}})");
    check(m.ok && m.count == 0, "empty visit list is a clean zero");

    // Overlong strings must truncate, never overflow the fixed buffers.
    std::string longRoute(200, 'X');
    const std::string j =
        R"({"ServiceDelivery":{"StopMonitoringDelivery":{"MonitoredStopVisit":[{
        "MonitoredVehicleJourney":{"LineRef":")" + longRoute +
        R"(","FramedVehicleJourneyRef":{"DatedVehicleJourneyRef":")" + longRoute +
        R"("},"MonitoredCall":{"AimedDepartureTime":"2026-08-08T18:00:00Z"}}}]}}})";
    m = parseStr(j);
    check(m.ok && m.count == 1, "overlong fields still yield a departure");
    check(strlen(m.departures[0].route) == sizeof(m.departures[0].route) - 1,
          "route truncated to its buffer");
    check(strlen(m.departures[0].number) == sizeof(m.departures[0].number) - 1,
          "train number truncated to its buffer");
  }

  // --- A BOM must not defeat the parse -------------------------------------
  {
    std::string bom = "\xEF\xBB\xBF";
    bom += R"({"ServiceDelivery":{"StopMonitoringDelivery":{"MonitoredStopVisit":[{
      "MonitoredVehicleJourney":{"FramedVehicleJourneyRef":{"DatedVehicleJourneyRef":"999"},
      "MonitoredCall":{"AimedDepartureTime":"2026-08-08T18:00:00Z"}}}]}}})";
    SiriResult m = parseStr(bom);
    check(m.ok && m.count == 1, "a leading UTF-8 BOM is tolerated");
  }

  // --- More visits than we keep --------------------------------------------
  {
    std::string j = R"({"ServiceDelivery":{"StopMonitoringDelivery":{"MonitoredStopVisit":[)";
    for (int i = 0; i < SIRI_MAX_DEPARTURES + 5; i++) {
      if (i) j += ",";
      j += R"({"MonitoredVehicleJourney":{"FramedVehicleJourneyRef":{"DatedVehicleJourneyRef":")" +
           std::to_string(i) + R"("},"MonitoredCall":{"AimedDepartureTime":"2026-08-08T18:00:00Z"}}})";
    }
    j += "]}}}";
    SiriResult m = parseStr(j);
    check(m.ok && m.count == SIRI_MAX_DEPARTURES, "extra visits are capped, not overflowed");
  }

  if (failures == 0) printf("test_siri_parse: all checks passed\n");
  return failures ? 1 : 0;
}
