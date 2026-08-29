// Host tests for the post-update health gate.
//
// A freshly installed image boots in ESP_OTA_IMG_PENDING_VERIFY. If it never
// marks itself valid, the bootloader reverts to the previous slot. These rules
// decide whether a new build keeps the sign — too lax and a broken build
// sticks; too strict and a good build is thrown away over a WiFi blip.
#include <cstdio>
#include <cstdint>
#include "ota_health.h"

static int failures = 0;
static void check(bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); failures++; } }

int main() {
  // --- All three conditions are required ------------------------------------
  {
    OtaHealth h{true, true, true};
    check(otaHealthSatisfied(h), "all three conditions satisfy the gate");

    check(!otaHealthSatisfied(OtaHealth{false, true, true}), "no WiFi fails");
    check(!otaHealthSatisfied(OtaHealth{true, false, true}), "no parsed fetch fails");
    check(!otaHealthSatisfied(OtaHealth{true, true, false}), "no painted board fails");
    check(!otaHealthSatisfied(OtaHealth{false, false, false}), "nothing fails");
  }

  // --- The timeout ----------------------------------------------------------
  {
    check(!otaHealthExpired(1000, 1000, OTA_HEALTH_BUDGET_MS), "not expired at t=0");
    check(!otaHealthExpired(1000, 1000 + OTA_HEALTH_BUDGET_MS - 1, OTA_HEALTH_BUDGET_MS),
          "not expired one ms early");
    check(otaHealthExpired(1000, 1000 + OTA_HEALTH_BUDGET_MS, OTA_HEALTH_BUDGET_MS),
          "expired exactly on the budget");
    check(otaHealthExpired(1000, 1000 + OTA_HEALTH_BUDGET_MS + 5000, OTA_HEALTH_BUDGET_MS),
          "expired past the budget");
  }

  // millis() wraps every ~49 days. Unsigned subtraction across the wrap would
  // report a huge elapsed time and roll back a healthy build. The existing
  // firmware uses signed deltas everywhere for exactly this reason
  // (see the comment in main.cpp's loop()).
  {
    const uint32_t nearMax = 0xFFFFF000u;
    check(!otaHealthExpired(nearMax, nearMax + 1000, OTA_HEALTH_BUDGET_MS),
          "does not expire across the millis() rollover");
    check(otaHealthExpired(nearMax, nearMax + OTA_HEALTH_BUDGET_MS + 1000,
                           OTA_HEALTH_BUDGET_MS),
          "still expires correctly across the rollover");
  }

  // The budget must be long enough for a real boot: WiFi join, NTP, and the
  // first 511 fetch, which the firmware already allows 45 s for on its own.
  check(OTA_HEALTH_BUDGET_MS >= 120000u, "budget leaves room for a slow boot");

  printf(failures ? "test_ota_health FAILED\n" : "test_ota_health passed\n");
  return failures != 0;
}
