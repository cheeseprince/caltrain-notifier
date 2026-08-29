// net_task.h — running the 511 fetch on the other core.
//
// WHY THIS EXISTS
//
// siriFetch() is a blocking HTTPS round trip that takes seconds, nearly all of
// it in the TLS handshake. Called from loop() it stalls everything: the clock
// stops, the countdowns stop, and whatever screen is up when it starts is
// frozen there until it returns. That produced three separate visible faults —
// a staleness note stuck at "live data 240s old", a finished boot checklist
// that sat there looking hung, and no way to show progress during the wait.
//
// So the fetch runs in its own FreeRTOS task pinned to core 0, leaving core 1's
// loop() free to keep painting at 1 Hz throughout.
//
// THREAD SAFETY
//
// Exactly one fetch is in flight at a time, enforced by a busy flag. That
// matters beyond tidiness: siriFetch writes into a file-scope response buffer,
// so a second concurrent call would corrupt the first.
//
// Results cross between tasks BY VALUE. FetchResult and the SiriResult inside
// it are self-contained — fixed-size char arrays, integers, and error pointers
// that only ever address string literals. Nothing points into the response
// buffer, so a copy stays valid after the net task reuses it.
//
// Every shared field is read and written under one mutex.
#pragma once
#ifdef ARDUINO
#include <stdint.h>

#include "siri_client.h"

namespace net_task {

// A snapshot of the in-flight fetch, safe to read from loop().
struct Progress {
  SiriPhase phase;
  uint32_t  done;       // bytes received so far
  uint32_t  total;      // Content-Length, or 0 when the server did not send one
  uint32_t  elapsedMs;  // since start() was called
  bool      busy;

  // Time spent in each phase, indexed by SiriPhase. A finished phase holds its
  // final duration; the running one keeps growing. Kept per phase rather than
  // as one total because the whole point is to see WHICH phase is slow — a
  // single elapsed figure showed the connect row still counting long after it
  // had gone green.
  uint32_t phaseMs[SIRI_PHASE_DONE + 1];
};

// Create the task. Call once from setup(), after WiFi is up.
void begin();

// Ask for a fetch. Returns false if one is already running, in which case
// nothing is queued — the caller simply tries again later.
bool start(const char* token, uint32_t stopCode);

// True from start() until the result is ready to take.
bool busy();

// Where the current or most recent fetch has got to.
Progress progress();

// Hand over a finished result, exactly once. Returns false when none is ready.
bool take(FetchResult* out);

// Deepest the net task's stack has ever got, in bytes remaining. Logged so the
// headroom over the TLS handshake is a measurement rather than a guess.
uint32_t stackHeadroom();

}  // namespace net_task
#endif  // ARDUINO
