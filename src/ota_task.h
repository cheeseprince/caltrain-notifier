// ota_task.h — the core-0 task that downloads and installs a signed update.
//
// WHY THIS EXISTS
//
// Same reasoning as net_task.h, one size up: a firmware image on this project
// is ~1.1 MB, against the few KB net_task moves. A blocking HTTPS transfer of
// that size on loop() would freeze the panel for the better part of a minute,
// not the second or two a 511 poll costs. So this runs in its own FreeRTOS
// task, pinned to core 0 exactly like net_task, leaving core 1's loop() free
// to keep painting — in this case, the "Updating firmware" screen itself,
// which needs the done/total this task publishes to draw a moving bar.
//
// THIS IS THE MODULE THAT REPLACES THE RUNNING FIRMWARE. The ordering inside
// ota_task.cpp is a security property, not a style choice — see the comments
// there before touching the sequence of steps.
//
// THREAD SAFETY
//
// Exactly one update is in flight at a time, enforced by a busy flag. That
// matters beyond tidiness: an update in progress is already writing into the
// spare OTA slot, and a second concurrent attempt would race it there.
//
// The Progress snapshot crosses between tasks BY VALUE, under one mutex,
// exactly like net_task::Progress. `error` only ever points at string
// literals baked into this file, never at anything computed at runtime, so a
// copied Progress stays valid for as long as the caller holds it — including
// after this task has moved on to a later phase or torn itself down for a
// restart.
//
// THE CONTRACT
//
// - start() returns false if an update is already running; nothing is
//   queued, so the caller (Task 7) just tries again on a later tick.
// - On a successful install the task NEVER RETURNS to OTA_DONE and wait —
//   it calls ESP.restart() directly from inside the task. The device reboots
//   into the new image. There is no "success, now what" state to observe.
// - On any failure — bad signature, hash mismatch, network error, whatever —
//   the running slot is left completely untouched. The task reports
//   OTA_FAILED with a reason and returns to idle; the caller resumes normal
//   operation on the firmware it already had.
// - "No update available" (the fetched release matches the running version)
//   is not a failure: it reports OTA_DONE with busy=false and error=nullptr.
#pragma once
#ifdef ARDUINO
#include <stdint.h>

// Where the current or most recent update attempt has got to. Mirrors
// SiriPhase in siri_client.h: IDLE before the first start(), then each real
// phase in order, ending at either DONE or FAILED. There is deliberately no
// phase for "rebooting" — the task calls ESP.restart() itself, so nothing is
// ever left observing that state.
enum OtaPhase : uint8_t {
  OTA_IDLE,
  OTA_MANIFEST,   // fetching manifest.txt
  OTA_VERIFY,     // fetching manifest.sig and checking the signature
  OTA_DOWNLOAD,   // streaming the image into the spare slot
  OTA_INSTALL,    // hashing complete, finalising the slot
  OTA_DONE,       // no update was needed (see the contract note above)
  OTA_FAILED,
};

namespace ota_task {

// A snapshot of the in-flight (or most recently finished) update, safe to
// read from loop(). Every field is either a fixed-size POD or a pointer to a
// string literal — see the THREAD SAFETY note above for why that matters.
struct Progress {
  OtaPhase    phase;
  uint32_t    done;      // bytes written so far, meaningful during DOWNLOAD
  uint32_t    total;     // image size in bytes, once known; 0 before then
  bool        busy;
  char        version[32];  // the release version being installed, once known
  const char* error;        // static description on OTA_FAILED, else nullptr
};

// Create the task. Call once from setup(), after WiFi is up — same timing
// requirement as net_task::begin().
void begin();

// Ask for an update check-and-install. Returns false if one is already
// running, in which case nothing is queued.
bool start();

// True from start() until the attempt has reached OTA_DONE or OTA_FAILED.
// Never becomes true again after a successful install, because the device
// has rebooted by then.
bool busy();

// Where the current or most recent attempt has got to.
Progress progress();

}  // namespace ota_task
#endif  // ARDUINO
