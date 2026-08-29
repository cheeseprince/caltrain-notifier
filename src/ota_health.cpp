#include "ota_health.h"

bool otaHealthSatisfied(const OtaHealth& h) {
  return h.wifiJoined && h.fetchParsed && h.boardPainted;
}

bool otaHealthExpired(uint32_t startedMs, uint32_t nowMs, uint32_t budgetMs) {
  // Compare a signed delta against zero — the same idiom every millis()
  // comparison in main.cpp uses (see lines 81, 206, 244, 485). The deadline is
  // allowed to wrap: unsigned addition wraps with it, and the signed difference
  // stays correct across the ~49-day millis() rollover.
  //
  // Dropping the (int32_t) cast makes this unsigned >= 0, which is
  // unconditionally true — the device would report every image expired
  // immediately and roll back every update. That is what the mutation check
  // below pins.
  const uint32_t deadlineMs = startedMs + budgetMs;
  return (int32_t)(nowMs - deadlineMs) >= 0;
}

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <string.h>
#include <time.h>

#include "fw_version.h"

namespace {
bool     g_pending = false;
uint32_t g_startedMs = 0;
bool     g_settled = false;

// NVS record of the version most recently sent for install, written by
// otaHealthRecordInstalling() just before the ESP.restart() that activates
// it. A namespace of its own, separate from config.cpp's "caltrain": this is
// firmware bookkeeping the device manages for itself, not user configuration,
// and the two must never be at risk of being cleared together.
constexpr const char* OTA_STATE_NS = "otastate";
constexpr const char* KEY_VERSION  = "version";
constexpr const char* KEY_EPOCH    = "epoch";

// Cached read-back of the record above, valid for the life of this boot.
// version[32] matches OtaRelease::version / ota_task::Progress::version, the
// two shapes this string ever actually comes from.
char    g_suppressedVersion[32] = "";
int64_t g_suppressedEpoch = 0;
}  // namespace

// Overrides the weak definition in the Arduino core (esp32-hal-misc.c). Called
// from initArduino() BEFORE setup(); returning true stops the core from
// cancelling rollback on our behalf.
extern "C" bool verifyRollbackLater() { return true; }

void otaHealthBegin() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
    g_pending = (state == ESP_OTA_IMG_PENDING_VERIFY);
  }
  // Deliberately not persisted across a reset (no RTC_DATA_ATTR / NVS): this
  // clock only has to time an image that is alive and looping in loop(). An
  // image that instead crashes or restarts is caught by the bootloader itself
  // on its next boot (PENDING_VERIFY -> ABORTED) before this clock matters —
  // see the header comment above for the full reasoning and citations.
  g_startedMs = millis();
  g_settled = false;
  if (g_pending) {
    Serial.printf("[OTA] new image on trial, %lus to prove itself\n",
                  (unsigned long)(OTA_HEALTH_BUDGET_MS / 1000));
  }

  // Read back whatever the last install attempt recorded (C2, whole-branch
  // review). Preferences.begin(readOnly) failing, or the keys never having
  // been written, both leave the buffer at its "" default — handled as
  // "nothing suppressed", same as config.cpp's configLoad() treats a never-
  // written namespace as "defaults stand".
  g_suppressedVersion[0] = '\0';
  g_suppressedEpoch = 0;
  Preferences p;
  if (p.begin(OTA_STATE_NS, /*readOnly=*/true)) {
    char stored[32] = "";
    p.getString(KEY_VERSION, stored, sizeof(stored));
    const int64_t storedEpoch = p.getLong64(KEY_EPOCH, 0);
    p.end();

    // A stored version EQUAL to the one now running is not a rollback: it is
    // either an ordinary boot with a stale-but-harmless record (about to be
    // cleared below once otaHealthReport() accepts this image), or a boot
    // that never installed anything new at all. Only a MISMATCH means this
    // boot ended up back on the OLD image because the version recorded here
    // just failed its gate and got rolled back — that is the one case this
    // record exists to catch.
    if (stored[0] && strcmp(stored, FW_VERSION) != 0) {
      strncpy(g_suppressedVersion, stored, sizeof(g_suppressedVersion) - 1);
      g_suppressedVersion[sizeof(g_suppressedVersion) - 1] = '\0';
      g_suppressedEpoch = storedEpoch;
      Serial.printf("[OTA] %s was rolled back; suppressing it for 24h\n", stored);
    }
  }
}

bool otaHealthPending() { return g_pending && !g_settled; }

const char* otaSuppressedVersion() { return g_suppressedVersion; }
int64_t otaSuppressedEpoch() { return g_suppressedEpoch; }

void otaHealthRecordInstalling(const char* version) {
  Preferences p;
  if (!p.begin(OTA_STATE_NS, /*readOnly=*/false)) return;
  p.putString(KEY_VERSION, version);
  p.putLong64(KEY_EPOCH, (int64_t)time(nullptr));
  p.end();
}

void otaHealthReport(const OtaHealth& h) {
  if (!g_pending || g_settled) return;

  if (otaHealthSatisfied(h)) {
    g_settled = true;
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[OTA] image accepted: board rendered from live data");

    // This version proved itself: it must never be treated as suppressed,
    // including if some LATER version is installed on top of it and then
    // itself gets rejected and rolls back to here. Clearing the record now
    // is what makes that true, rather than leaving a stale "succeeded" entry
    // that happens to not matter only by coincidence of what gets compared
    // against what later.
    Preferences p;
    if (p.begin(OTA_STATE_NS, /*readOnly=*/false)) {
      p.remove(KEY_VERSION);
      p.remove(KEY_EPOCH);
      p.end();
    }
    return;
  }

  if (otaHealthExpired(g_startedMs, millis(), OTA_HEALTH_BUDGET_MS)) {
    g_settled = true;
    Serial.printf("[OTA] image REJECTED (wifi=%d fetch=%d painted=%d) — rolling back\n",
                  h.wifiJoined, h.fetchParsed, h.boardPainted);
    Serial.flush();
    esp_ota_mark_app_invalid_rollback_and_reboot();  // does not return
  }
}
#endif  // ARDUINO
