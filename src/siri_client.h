// siri_client.h — fetching a StopMonitoring response over HTTPS.
//
// Device-only. The parsing it feeds is in siri_parse.{h,cpp}, which is pure and
// host-tested; this file is just the transport.
#pragma once
#ifdef ARDUINO
#include <stdint.h>

#include "siri_parse.h"

struct FetchResult {
  SiriResult  siri;
  bool        transportOk;  // the HTTPS exchange itself succeeded
  int         httpCode;     // HTTP status, or a negative HTTPClient error
  const char* error;        // static description, or nullptr
  size_t      bytes;        // response size, for the heap/size log
};

// Where a fetch has got to. Reported so the boot screen can say what it is
// waiting on instead of showing one static line for several seconds.
//
// CONNECT covers DNS, TCP, the TLS handshake, the request and the response
// headers — all of it inside a single blocking HTTPClient call, which is why it
// cannot be broken down further. It is also where nearly all the time goes: the
// certificate validation against the Mozilla bundle is expensive on this chip.
// Only DOWNLOAD can report real progress, and on a 3 KB body it is over in
// milliseconds.
enum SiriPhase : uint8_t {
  SIRI_PHASE_IDLE,
  SIRI_PHASE_CONNECT,
  SIRI_PHASE_DOWNLOAD,
  SIRI_PHASE_PARSE,
  SIRI_PHASE_DONE,
};

// Called from inside siriFetch as it progresses. `total` is the Content-Length
// when the server sent one and 0 when it did not.
using SiriProgressFn = void (*)(SiriPhase phase, uint32_t done, uint32_t total);

// GET the next departures at `stopCode` using `token`.
//
// Blocking, with its own timeout, and it writes into a file-scope response
// buffer — so exactly one call may be in flight at a time. Call it through
// net_task, which owns that guarantee; nothing else should call it directly.
FetchResult siriFetch(const char* token, uint32_t stopCode,
                      SiriProgressFn progress = nullptr);
#endif  // ARDUINO
