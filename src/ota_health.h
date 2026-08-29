// ota_health.h — deciding whether a freshly installed build gets to stay.
//
// The ESP32 bootloader supports rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
// is set in the core we pin). A newly activated image boots in the state
// ESP_OTA_IMG_PENDING_VERIFY; unless it calls
// esp_ota_mark_app_valid_cancel_rollback(), the next reset returns to the
// previous slot.
//
// By default Arduino cancels that for us inside initArduino(), before setup()
// runs. Overriding the weak verifyRollbackLater() to return true hands the
// decision here instead.
//
// The bar is deliberately "renders a real board from live data", not "boots".
// A build that comes up but cannot reach 511 shows an empty sign and is no more
// useful than one that does not boot at all. The accepted cost: an update that
// lands during a WiFi or 511 outage rolls back a perfectly good build, which is
// simply offered again the next day.
//
// "Offered again the next day" is a promise this file has to keep, not just
// ota_task's. Without help, a rejected release is retried on every ~75s OTA
// check forever: the rollback returns the device to an image that is no
// longer PENDING_VERIFY, so nothing about the running state remembers that
// this exact version already failed. otaHealthRecordInstalling() below (and
// the read-back inside otaHealthBegin()) is that memory: a small NVS record,
// written just before the restart that installs a candidate image, read back
// on the next boot to recognise "I am running the OLD image because THIS
// version just failed its gate," and cleared the moment a version is
// actually accepted. otaVersionSuppressed() in ota_manifest.h is the pure
// 24-hour decision this record feeds.
//
// Why the budget clock (otaHealthBegin's g_startedMs = millis()) does NOT need
// to survive a reset, and is not persisted in RTC_DATA_ATTR or NVS:
//
// Two distinct failure shapes are covered by two distinct mechanisms, and the
// split is the design, not an oversight:
//   - The app reaches a working state but can't get further (WiFi joins, the
//     board boots and keeps running, but the fetch or render path is broken).
//     Nothing reboots here, so otaHealthReport() keeps running from loop() and
//     the 5-minute OTA_HEALTH_BUDGET_MS clock is what eventually catches it.
//   - The app crashes, hangs, or calls ESP.restart() before ever reaching that
//     loop. The bootloader itself catches this on the very next boot: an image
//     still in ESP_OTA_IMG_PENDING_VERIFY that boots again without having been
//     marked valid is flipped to ESP_OTA_IMG_ABORTED and is never selected to
//     boot again — see tools/sdk/esp32/include/bootloader_support/include/
//     esp_flash_partitions.h:51 (PENDING_VERIFY -> ABORTED on a second boot)
//     and :54 (an ABORTED image "will not [be] selected to boot at all").
//     Verified against the pinned Arduino core 2.0.17 / espressif32@7.0.1,
//     with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE confirmed set at
//     tools/sdk/esp32/dio_qspi/include/sdkconfig.h:27.
//
// So a reboot loop cannot dodge the gate: restarting is not an escape from
// otaHealthReport(), it is simply the faster of the two paths through it —
// the bootloader condemns the image before our millis()-based clock would
// ever have gotten the chance to. Resetting g_startedMs to millis() on every
// boot is therefore correct, not a bug: it only needs to time a build that is
// alive and looping, and that build's clock legitimately restarts with it.
// Persisting the timestamp in RTC_DATA_ATTR or NVS would add real complexity
// (an extra write path, another thing to get wrong across a device reset)
// for a case the bootloader already closes without our help.
#pragma once
#include <stdint.h>

// What the running build has managed to do since boot.
struct OtaHealth {
  bool wifiJoined;    // the stored credentials still work on this build
  bool fetchParsed;   // one 511 response arrived and parsed: TLS, HTTP, parser
  bool boardPainted;  // at least one departure reached the panel
};

// Five minutes. Long enough for a WiFi join, NTP, and the first 511 fetch —
// which setup() already allows 45 s for by itself.
constexpr uint32_t OTA_HEALTH_BUDGET_MS = 300000;

// True once the build has proven itself.
bool otaHealthSatisfied(const OtaHealth& h);

// True once the budget has elapsed. The deadline (startedMs + budgetMs) may
// itself wrap; the signed compare-to-zero idiom (the same one every millis()
// comparison in main.cpp uses) stays correct across that ~49-day rollover.
bool otaHealthExpired(uint32_t startedMs, uint32_t nowMs, uint32_t budgetMs);

#ifdef ARDUINO
// Call once early in setup(). Records whether this boot is a pending-verify
// boot and, if so, when the budget started.
void otaHealthBegin();

// True while this image still has to prove itself. False on an ordinary boot.
bool otaHealthPending();

// Call from the tick with the current state. Commits the image once satisfied,
// or triggers a rollback reboot once the budget expires. Does nothing when the
// image is not pending verification.
//
// On acceptance, this also clears the NVS record otaHealthRecordInstalling()
// wrote: an accepted version must never be treated as suppressed later, even
// if some future version is installed on top of it and then rejected.
void otaHealthReport(const OtaHealth& h);

// The version most recently rolled back from, or "" if nothing is currently
// suppressed. Populated by otaHealthBegin() by comparing the NVS record
// otaHealthRecordInstalling() last wrote against the version now running: a
// stored version that differs from FW_VERSION means THIS boot is the old
// image, reached because the stored one just failed its gate. A missing or
// blank NVS record (nothing ever installed, or Preferences unavailable) reads
// as "nothing suppressed".
const char* otaSuppressedVersion();

// The wall-clock epoch (time(nullptr)) otaHealthRecordInstalling() captured
// for the version above. 0 when otaSuppressedVersion() is "".
int64_t otaSuppressedEpoch();

// Record the version about to be installed, together with the current wall
// clock, in NVS. Call from ota_task.cpp immediately before ESP.restart() —
// see the header comment above and the call site in ota_task.cpp's step 7.
void otaHealthRecordInstalling(const char* version);
#endif  // ARDUINO
