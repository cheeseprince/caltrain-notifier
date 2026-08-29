#!/usr/bin/env python3
"""Generate src/timetable_data.h — the full Caltrain timetable, compiled into flash.

Why this exists, and why it isn't fetched at runtime:

The 511 realtime feed answers "when does the next train leave?" but only for the
next THREE departures, and it never says which intermediate stops a train makes.
Its schedule endpoint (stoptimetable) returns a fixed four-entry rolling window
and ignores StartTime/EndTime, so it cannot be cached into a day's timetable.
Verified against the live API — see tools/probe_511.py and the committed
capture in test/fixtures/stopmonitoring_70012.json.

So the device carries the timetable. That buys three things the API cannot give:

  * an offline fallback when 511 is unreachable,
  * the stop list per train, so an Express that skips your destination is
    filtered out instead of being shown as a train you could catch,
  * departures beyond the third, so the board can still fill three rows.

Cost is roughly 24 KB of flash out of a 1.9 MB partition.

Caltrain reissues its schedule a few times a year. When it does:

    python3 tools/gen_timetable.py            # downloads the current feed
    python3 tools/gen_timetable.py --zip x.z  # or use a local copy

Regenerate stations.h from the same feed at the same time — the two headers
share a station index space and must be built from one feed version.
"""

import argparse
import pathlib
import sys

# gen_stations owns the download, CSV reading, and station ordering. Reusing it
# guarantees both headers agree on the station index space, which the generated
# tables reference by position.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_stations import GTFS_URL, collect_stations, load_gtfs, read_csv  # noqa: E402

HEADER_PATH = pathlib.Path(__file__).resolve().parent.parent / "src" / "timetable_data.h"

# GTFS direction_id, confirmed against directions.txt for every Caltrain route.
DIR_NORTH, DIR_SOUTH = 0, 1

# Longest published train number, excluding the NUL. The feed's widest today is
# "M101" at 4; the guard in build() turns any future overflow into a loud skip
# rather than a silently truncated train number.
TRAIN_NUMBER_MAX = 5


def parse_gtfs_time(value: str) -> int | None:
    """'HH:MM:SS' -> minutes since midnight of the service day.

    GTFS lets the hour run past 24 for trips that cross midnight but still
    belong to the previous service day: 26:30:00 is 2:30 am, and a rider looking
    at Saturday's timetable should still see it. We keep the raw value rather
    than wrapping, so ordering stays monotonic within a trip.
    """
    if not value:
        return None
    try:
        h, m, s = (int(p) for p in value.split(":"))
    except ValueError:
        return None
    return h * 60 + m + (1 if s >= 30 else 0)


def resolve_services(gtfs):
    """Work out the service patterns and which dates deviate from the weekly one.

    Caltrain runs three distinct timetables, not two:

      * weekday        Mon-Fri
      * weekend        Sat/Sun, and the five major holidays
      * holiday        a reduced schedule of its own, used on President's Day,
                       the day after Thanksgiving, Christmas Eve, and MLK Day

    That third pattern is easy to miss: it appears only in calendar_dates.txt,
    never in calendar.txt, and it carries 79 trips. Treating those four dates as
    weekends would show the rider trains that are not running.

    Returns (service_ids_in_index_order, labels, {yyyymmdd: service_index}).
    """
    calendar = read_csv(gtfs, "calendar.txt")
    weekday_id = next(r["service_id"] for r in calendar if r["monday"] == "1")
    weekend_id = next(r["service_id"] for r in calendar if r["saturday"] == "1")

    exceptions = read_csv(gtfs, "calendar_dates.txt")
    # Dates where the ordinary weekday service is withdrawn.
    cancelled = {
        r["date"] for r in exceptions
        if r["service_id"] == weekday_id and r["exception_type"] == "2"
    }
    # Services added on a given date, in feed order.
    added: dict[str, list[str]] = {}
    for r in exceptions:
        if r["exception_type"] == "1":
            added.setdefault(r["date"], []).append(r["service_id"])

    service_ids = [weekday_id, weekend_id]
    labels = ["weekday", "weekend"]
    overrides: dict[int, int] = {}
    holiday_names: dict[int, str] = {}
    name_by_date = {r["date"]: r.get("holiday_name", "").strip() for r in exceptions}

    for date in sorted(cancelled):
        replacements = added.get(date, [])
        if not replacements:
            # Weekday service withdrawn with nothing put back: genuinely no
            # service. Nothing sensible to point the rider at, so leave it to the
            # default and let the empty result speak for itself.
            continue
        chosen = replacements[0]
        if chosen not in service_ids:
            service_ids.append(chosen)
            labels.append("holiday")
        overrides[int(date)] = service_ids.index(chosen)
        holiday_names[int(date)] = name_by_date.get(date, "")

    return service_ids, labels, overrides, holiday_names


def build(gtfs):
    stations = collect_stations(gtfs)
    # Map a platform stop_id (70012) to its station index. Both platforms of a
    # station collapse to the same index; direction is carried on the trip.
    station_index = {}
    for idx, (base, _name, _lat) in enumerate(stations):
        station_index[str(base * 10 + 1)] = idx
        station_index[str(base * 10 + 2)] = idx

    routes = {r["route_id"]: r["route_short_name"] for r in read_csv(gtfs, "routes.txt")}
    service_ids, service_labels, overrides, holiday_names = resolve_services(gtfs)
    service_index = {sid: i for i, sid in enumerate(service_ids)}

    # Group stop events by trip, keeping GTFS stop_sequence order — that order is
    # the direction of travel, which the "does this train reach my destination"
    # test relies on.
    by_trip: dict[str, list[tuple[int, int, int]]] = {}
    for row in read_csv(gtfs, "stop_times.txt"):
        idx = station_index.get(row["stop_id"].strip())
        if idx is None:
            continue
        dep = parse_gtfs_time(row["departure_time"].strip())
        if dep is None:
            continue
        by_trip.setdefault(row["trip_id"], []).append(
            (int(row["stop_sequence"]), idx, dep)
        )

    route_names: list[str] = []
    trips = []
    skipped = []
    for row in read_csv(gtfs, "trips.txt"):
        trip_id = row["trip_id"]
        stops = by_trip.get(trip_id)
        if not stops:
            skipped.append((trip_id, "no usable stop_times"))
            continue

        # Services we cannot select for by date are dropped. In practice this is
        # only the 2026 World Cup extras, which supplement the normal timetable
        # on four June dates rather than replacing it, and which the feed does
        # not describe well enough to schedule against.
        svc = service_index.get(row["service_id"])
        if svc is None:
            skipped.append((trip_id, f"service {row['service_id']} is not date-selectable"))
            continue

        # Train numbers are not always numeric: the holiday timetable prefixes
        # its trains with M ("M101"). We keep the published string rather than an
        # integer, both because it is what the rider sees on the platform sign
        # and because it is exactly what the live feed puts in
        # DatedVehicleJourneyRef, which is how live and scheduled rows are joined.
        number = (row["trip_short_name"] or "").strip()
        if not number:
            skipped.append((trip_id, "no trip_short_name"))
            continue
        if len(number) > TRAIN_NUMBER_MAX:
            skipped.append((trip_id, f"train number {number!r} exceeds {TRAIN_NUMBER_MAX} chars"))
            continue

        name = routes.get(row["route_id"], "Caltrain")
        if name not in route_names:
            route_names.append(name)

        stops.sort()
        trips.append(
            {
                "number": number,
                "service": svc,
                "route": route_names.index(name),
                "direction": int(row["direction_id"]),
                "stops": [(idx, dep) for _seq, idx, dep in stops],
            }
        )

    # Order by service, then direction, then departure from the trip's first
    # stop. Lookup scans linearly, and a time-ordered scan can stop early.
    trips.sort(key=lambda t: (t["service"], t["direction"], t["stops"][0][1]))
    return stations, route_names, trips, skipped, service_labels, overrides, holiday_names


def render(stations, route_names, trips, service_labels, overrides, holiday_names,
           feed_version: str, feed_end_date: int) -> str:
    flat: list[tuple[int, int]] = []
    rows = []
    for t in trips:
        first = len(flat)
        flat.extend(t["stops"])
        rows.append((t["number"], first, len(t["stops"]), t["service"], t["route"], t["direction"]))

    lines = [
        "// GENERATED by tools/gen_timetable.py — do not edit by hand.",
        f"// Source: {GTFS_URL}",
        f"// Feed version: {feed_version}",
        "//",
        "// Station indices refer to kStations in stations.h. Both headers must be",
        "// generated from the same feed version or the indices will not line up.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "// Longest published train number, excluding the terminating NUL.",
        f"inline constexpr int TRAIN_NUMBER_MAX = {TRAIN_NUMBER_MAX};",
        "",
        "// Service patterns, in index order. Caltrain runs THREE, not two: besides",
        "// weekday and weekend there is a separate reduced holiday timetable used on",
        "// President's Day, the day after Thanksgiving, Christmas Eve and MLK Day.",
        "// Use serviceForDate() in timetable.h rather than deriving this yourself.",
        "enum : uint8_t {",
        "  SVC_WEEKDAY = 0,",
        "  SVC_WEEKEND = 1,",
        "  SVC_HOLIDAY = 2,",
        "};",
        f"inline constexpr int kServiceCount = {len(service_labels)};",
        "inline constexpr const char* kServiceNames[] = {",
    ]
    for label in service_labels:
        lines.append(f'    "{label}",')
    lines += [
        "};",
        "",
        "// Dates whose service differs from the plain Mon-Fri / Sat-Sun rule.",
        "// Sorted ascending so lookup can binary-search or stop early.",
        "struct DateOverride {",
        "  uint32_t date;      // YYYYMMDD",
        "  uint8_t  service;",
        "};",
        "inline constexpr DateOverride kDateOverrides[] = {",
    ]
    for date in sorted(overrides):
        label = service_labels[overrides[date]]
        why = holiday_names.get(date, "")
        lines.append(f"    {{{date}, {overrides[date]}}},  // {why} -> {label}")
    lines += [
        "};",
        f"inline constexpr int kDateOverrideCount = {len(overrides)};",
        "",
        "// FINDING F-1 (2026-08 adversarial review): the two dates that bound how",
        "// far this header can be trusted.",
        "//",
        "//   kTimetableLastOverride  the last date in kDateOverrides above. Past",
        "//                           it, serviceForDate() has no holiday pattern",
        "//                           to fall back on and silently returns plain",
        "//                           weekday/weekend — right most days, wrong on",
        "//                           the next unlisted holiday.",
        "//   kTimetableFeedEnd       the GTFS feed's own declared validity end",
        "//                           (feed_info.txt, feed_end_date).",
        "//",
        "// The two differ, and the earlier one governs: see timetableExpired() in",
        "// timetable.h, which the firmware calls once per service day to decide",
        "// whether to show a staleness warning.",
        f"inline constexpr uint32_t kTimetableLastOverride = {max(overrides) if overrides else 0};",
        f"inline constexpr uint32_t kTimetableFeedEnd = {feed_end_date};",
        "",
        "// One scheduled call: a station, and the minute past midnight the train",
        "// departs it. Values above 1440 are after-midnight trains that still belong",
        "// to the previous service day (GTFS writes those as 25:10:00).",
        "struct TripStop {",
        "  uint16_t depMin;",
        "  uint8_t  station;   // index into kStations",
        "};",
        "",
        "// One scheduled train. Its calls occupy kTripStops[firstStop .. firstStop+nStops)",
        "// in travel order, so a trip reaches `dest` from `origin` exactly when both",
        "// appear and origin comes first.",
        "struct Trip {",
        "  char     number[TRAIN_NUMBER_MAX + 1];  // e.g. \"614\", or \"M101\" on holidays",
        "  uint16_t firstStop;   // offset into kTripStops",
        "  uint8_t  nStops;",
        "  uint8_t  service;     // SVC_WEEKDAY | SVC_WEEKEND | SVC_HOLIDAY",
        "  uint8_t  route;       // index into kRouteNames",
        "  uint8_t  direction;   // 0 = north, 1 = south (matches enum Direction)",
        "};",
        "",
        "// Service names as Caltrain publishes them, e.g. \"Local Weekday\", \"Express\".",
        "inline constexpr const char* kRouteNames[] = {",
    ]
    for name in route_names:
        lines.append(f'    "{name}",')
    lines += [
        "};",
        f"inline constexpr int kRouteNameCount = {len(route_names)};",
        "",
        "inline constexpr TripStop kTripStops[] = {",
    ]
    # 8 calls per source line keeps the file readable without bloating it.
    for i in range(0, len(flat), 8):
        chunk = "".join(f"{{{dep:>4},{idx:>3}}}," for idx, dep in flat[i : i + 8])
        lines.append("    " + chunk)
    lines += [
        "};",
        f"inline constexpr int kTripStopCount = {len(flat)};",
        "",
        "// Sorted by service, then direction, then departure from the first stop.",
        "inline constexpr Trip kTrips[] = {",
    ]
    for number, first, n, service, route, direction in rows:
        lines.append(
            f'    {{{("\"" + number + "\""):>7},{first:>6},{n:>3},{service},{route},{direction}}},'
        )
    lines += [
        "};",
        f"inline constexpr int kTripCount = {len(rows)};",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zip", help="path to a local GTFS zip instead of downloading")
    parser.add_argument("-o", "--output", default=str(HEADER_PATH))
    args = parser.parse_args()

    gtfs = load_gtfs(args.zip)
    stations, route_names, trips, skipped, labels, overrides, names = build(gtfs)
    feed_info = read_csv(gtfs, "feed_info.txt")[0]
    feed_version = feed_info.get("feed_version", "unknown")

    # feed_end_date is what F-1 (2026-08 adversarial review) hangs the staleness
    # warning off: GTFS requires it in feed_info.txt as YYYYMMDD, so a missing or
    # unparsable value is a feed problem worth stopping for rather than silently
    # emitting a 0 that would make the board look permanently expired.
    feed_end_raw = feed_info.get("feed_end_date", "")
    try:
        feed_end_date = int(feed_end_raw)
    except ValueError:
        print(
            f"error: feed_info.txt feed_end_date is missing or not YYYYMMDD: {feed_end_raw!r}",
            file=sys.stderr,
        )
        return 1

    if not trips:
        print("error: no trips built — feed format may have changed", file=sys.stderr)
        return 1

    text = render(stations, route_names, trips, labels, overrides, names, feed_version,
                  feed_end_date)
    out = pathlib.Path(args.output)
    out.write_text(text)

    calls = sum(len(t["stops"]) for t in trips)
    per_service = {
        label: sum(1 for t in trips if t["service"] == i)
        for i, label in enumerate(labels)
    }
    breakdown = ", ".join(f"{n} {label}" for label, n in per_service.items())
    print(
        f"wrote {out}\n"
        f"  {len(trips)} trips ({breakdown}), {calls} calls, "
        f"{len(route_names)} route types\n"
        f"  {len(overrides)} date overrides, last {max(overrides) if overrides else 0}\n"
        f"  feed valid through {feed_end_date}\n"
        f"  approx flash: {calls * 4 + len(trips) * 8:,} bytes\n"
        f"  skipped {len(skipped)} trips",
        file=sys.stderr,
    )
    # Silent omissions are how a timetable quietly loses trains, so name them.
    for trip_id, why in skipped[:10]:
        print(f"    - trip {trip_id}: {why}", file=sys.stderr)
    if len(skipped) > 10:
        print(f"    ... and {len(skipped) - 10} more", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
