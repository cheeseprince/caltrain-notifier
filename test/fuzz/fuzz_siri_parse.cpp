// fuzz_siri_parse.cpp — libFuzzer entry point for siri_parse.cpp.
//
// siri_parse.cpp is the ONLY code in this project that consumes bytes from
// outside: a 511 SF Bay transit API response, over HTTPS. Everything else
// (the display, the OTA installer, the board model) operates on the
// compiled-in timetable or on already signature-verified data. That makes
// this parser the highest-value place in the codebase to fuzz.
//
// Built separately from the host test suite, with clang's
// -fsanitize=fuzzer,address,undefined (see test/Makefile's `fuzz` target).
// ASan and UBSan matter as much as libFuzzer's mutation engine here: a
// plain crash-hunting fuzzer only notices a segfault or an ASan report.
// UBSan additionally catches signed-integer-overflow, shift-out-of-range
// and the like -- the class of bug that corrupts a value without ever
// touching memory out of bounds.
//
// THIS IS NOT A CRASH-ONLY FUZZER. The bug that motivated adding fuzzing at
// all (see the parseIso8601Utc fix in this same change series) was a parser
// that never crashed: an out-of-range UTC offset was silently accepted and
// applied, producing a confidently wrong departure time while still
// returning ok=true. A crash-only fuzzer would never have found that shape
// of bug, because nothing ever segfaults. So every successful parse here is
// checked against invariants that must hold for ANY input, not just
// "did not crash" -- see CHECK_INVARIANTS below. If one of these fires, that
// is itself a finding to report, not a bug in the harness to paper over.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "siri_parse.h"

namespace {

// The exact range of instants parseIso8601Utc() can legitimately return.
// These are DERIVED FROM THE PARSER'S OWN CONTRACT, not picked as round
// numbers -- the point is to catch parser garbage (year 0000, a date
// centuries out, an offset applied twice), so the harness must accept every
// value the parser is documented to accept, and nothing outside it.
//
// The parser bounds the LOCAL date to years 2000-2100 inclusive and the UTC
// offset to -12:00..+14:00 (see src/siri_parse.cpp), then converts to UTC:
//
//   floor   = 2000-01-01T00:00:00+14:00  -> 1999-12-31T10:00:00Z
//   ceiling = 2100-12-31T23:59:60-12:00  -> 2101-01-01T12:00:00Z
//             (sec=60 is the leap-second allowance)
//
// History: these were once 2000-01-01Z and 2100-01-01Z. The fuzz smoke test
// then found "2100-08-18T04:55:00Z" -- a value the parser correctly accepts
// -- and aborted on it. The harness was wrong, not the parser. Both edges
// are pinned in test/test_siri_parse.cpp and seeded in fuzz/seeds/, so the
// harness and the parser cannot silently drift apart again.
constexpr int64_t kPlausibleFloor = 946634400;     // 1999-12-31T10:00:00Z
constexpr int64_t kPlausibleCeiling = 4134024000;  // 2101-01-01T12:00:00Z

// A departure time of exactly 0 is siri_parse's OWN sentinel for "absent"
// (see LiveDeparture's comments in siri_parse.h) and is filtered out before
// a visit is kept (see the `continue` in siriParse() when both times are 0).
// Anything else -- including a non-zero but wildly implausible instant --
// reaching a KEPT departure is a parser defect, not a display concern.
void checkDeparture(int64_t t, const char* which) {
  if (t == 0) return;  // the documented "absent" sentinel; not our concern here
  assert(t > 0 && "a kept departure time must not be negative (pre-1970)");
  assert(t >= kPlausibleFloor && t <= kPlausibleCeiling &&
         "a kept departure time must fall in a plausible calendar range");
  (void)which;  // only used in the assert message above under NDEBUG-off builds
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  SiriResult r = siriParse(reinterpret_cast<const char*>(data), size);

  // r.ok == false carries no promise about the rest of the struct -- the
  // function is documented to yield count=0 on a malformed document, but a
  // caller that ignores ok and reads departures anyway is a caller bug, not
  // a parser one. Nothing to check on that path.
  if (!r.ok) return 0;

  // count must never exceed the fixed array it indexes, in either direction.
  assert(r.count >= 0 && r.count <= SIRI_MAX_DEPARTURES &&
         "SiriResult.count must stay within [0, SIRI_MAX_DEPARTURES]");

  for (int i = 0; i < r.count; i++) {
    const LiveDeparture& d = r.departures[i];

    // A kept visit must have at least one usable time. siriParse() itself
    // is supposed to `continue` (drop the visit) when both are 0 -- see the
    // comment there ("showing it with a blank or zero time would be worse
    // than omitting it"). If a kept row has both fields 0, that guarantee
    // broke somewhere between the check and the increment.
    assert(!(d.aimedDeparture == 0 && d.expectedDeparture == 0) &&
           "a kept departure must not have both times absent");

    checkDeparture(d.aimedDeparture, "aimedDeparture");
    checkDeparture(d.expectedDeparture, "expectedDeparture");

    // String fields must be NUL-terminated within their fixed buffers.
    // copyField() is supposed to guarantee this on every path (it writes
    // '\0' first, then truncates and re-terminates), so this should never
    // fire -- it is here to catch a future refactor that breaks that
    // guarantee, not because today's implementation is expected to fail it.
    assert(memchr(d.number, '\0', sizeof(d.number)) != nullptr &&
           "train number must be NUL-terminated within its buffer");
    assert(memchr(d.route, '\0', sizeof(d.route)) != nullptr &&
           "route must be NUL-terminated within its buffer");
  }

  return 0;
}
