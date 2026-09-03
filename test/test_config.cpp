// Host tests for the pure half of config.{h,cpp}: what counts as a usable
// configuration, and how a corrupt or half-written record is normalised.
//
// The NVS half is behind #ifdef ARDUINO. These rules decide whether the device
// shows the board or the setup portal, so getting them wrong strands the sign
// on one screen or the other with no touch to escape.
#include <cstdio>
#include <cstring>
#include "config.h"

static int failures = 0;
static void check(bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); failures++; } }

static Config good() {
  Config c = configDefaults();
  strcpy(c.ssid, "HomeNet");
  strcpy(c.pass, "hunter2hunter2");
  strcpy(c.token, "12345678-1234-1234-1234-123456789abc");
  c.originIdx = (int8_t)stationIndexByName("San Francisco");
  c.destIdx = (int8_t)stationIndexByName("San Jose Diridon");
  return c;
}

int main() {
  // --- Defaults -------------------------------------------------------------
  {
    Config d = configDefaults();
    check(!configComplete(d), "a fresh device is not configured");
    check(d.originIdx == -1 && d.destIdx == -1, "route starts unset");
    check(d.brightnessDay == 100 && d.brightnessNight == 25, "sane default brightness");
    // A device that silently stops updating is worse than one that updates,
    // so the factory default has to be on, not off.
    check(d.otaAutoUpdate, "auto-update defaults to enabled");

    // The urgency defaults have to reproduce the rule the sign shipped with —
    // an existing device that takes this update must not see its colours move.
    check(d.redUnder == kUrgencyDefaults.redUnder, "red threshold defaults to 10 min");
    check(d.yellowUnder == kUrgencyDefaults.yellowUnder, "yellow threshold defaults to 16 min");
  }

  // --- Completeness ---------------------------------------------------------
  {
    check(configComplete(good()), "a fully filled config is complete");

    Config c = good();
    c.ssid[0] = '\0';
    check(!configComplete(c), "no SSID means not configured");

    // An open network is legitimate; an empty password must not disqualify it.
    c = good();
    c.pass[0] = '\0';
    check(configComplete(c), "an open network is still a usable configuration");

    c = good();
    c.token[0] = '\0';
    check(!configComplete(c), "no token means not configured");

    c = good();
    strcpy(c.token, "abc");
    check(!configComplete(c), "an obviously truncated token is rejected");

    c = good();
    c.destIdx = c.originIdx;
    check(!configComplete(c), "origin equal to destination is not a route");

    c = good();
    c.originIdx = -1;
    check(!configComplete(c), "half-set route is not configured");

    c = good();
    c.destIdx = (int8_t)kStationCount;
    check(!configComplete(c), "out-of-range station is not configured");

    // Auto-update is an operational preference, not a setup requirement: a
    // device with updates turned off is still a fully configured device, and
    // one with updates on is not configured just because the route is unset.
    c = good();
    c.otaAutoUpdate = false;
    check(configComplete(c), "a device with auto-update disabled is still complete");

    c = good();
    c.ssid[0] = '\0';
    c.otaAutoUpdate = false;
    check(!configComplete(c), "auto-update being off does not excuse a missing SSID");
  }

  // --- Sanitising -----------------------------------------------------------
  {
    // A brightness of zero would leave a black screen that cannot be woken,
    // since this build has no touch.
    Config c = good();
    c.brightnessDay = 0;
    c.brightnessNight = 0;
    configSanitise(c);
    check(c.brightnessDay >= 5, "day brightness is never fully off");
    check(c.brightnessNight >= 5, "night brightness is never fully off");

    c = good();
    c.brightnessDay = 200;
    configSanitise(c);
    check(c.brightnessDay == 100, "brightness is capped at 100");

    // A garbage route is normalised to unset, so the portal asks for both
    // rather than the firmware honouring half of it.
    c = good();
    c.originIdx = 99;
    configSanitise(c);
    check(c.originIdx == -1 && c.destIdx == -1, "an invalid route is cleared entirely");
    check(!configComplete(c), "and the config then reads as incomplete");

    c = good();
    c.destIdx = c.originIdx;
    configSanitise(c);
    check(c.originIdx == -1 && c.destIdx == -1, "origin==destination is cleared too");

    // Sanitising a good config must not disturb it.
    c = good();
    Config before = c;
    configSanitise(c);
    check(memcmp(&before, &c, sizeof(Config)) == 0, "a valid config is left untouched");

    // Zero minutes of red is a band no countdown can ever be inside, so the
    // colour would simply never appear. The portal's min=1 says the same thing;
    // this is the backstop for a hand-crafted POST that ignores it.
    c = good();
    c.redUnder = 0;
    c.yellowUnder = 0;
    configSanitise(c);
    check(c.redUnder >= URGENCY_MIN_MINUTES, "a red threshold of zero is lifted to the minimum");
    check(c.yellowUnder >= c.redUnder, "and yellow is never left below red");

    // An hour of warning is already far more than this sign is useful for;
    // beyond that the whole board would be one colour all day.
    c = good();
    c.redUnder = 200;
    c.yellowUnder = 250;
    configSanitise(c);
    check(c.redUnder == URGENCY_MAX_MINUTES, "the red threshold is capped at 60 min");
    check(c.yellowUnder == URGENCY_MAX_MINUTES, "so is the yellow one");

    // Inverted bounds would make yellow unreachable. Raising yellow to meet red
    // keeps the record meaning what the user most likely wanted — a red band of
    // the width they asked for — rather than silently widening the red one.
    c = good();
    c.redUnder = 20;
    c.yellowUnder = 5;
    configSanitise(c);
    check(c.redUnder == 20, "the red threshold survives an inverted pair");
    check(c.yellowUnder == 20, "and yellow is raised to meet it, collapsing the band");
    check(urgencyFor(19, UrgencyThresholds{c.redUnder, c.yellowUnder}) == URGENCY_RED,
          "the normalised pair still reddens a close train");
    check(urgencyFor(20, UrgencyThresholds{c.redUnder, c.yellowUnder}) == URGENCY_GREEN,
          "and greens a distant one, with no yellow in between");

    // An equal pair is a legitimate choice (no yellow band at all), not an
    // error to be corrected — the same reading as an equal bright window.
    c = good();
    c.redUnder = 12;
    c.yellowUnder = 12;
    configSanitise(c);
    check(c.redUnder == 12 && c.yellowUnder == 12, "a deliberately collapsed band survives");

    // configSanitise() has no opinion about this setting either way — it is
    // not something normalisation ever needs to correct.
    c = good();
    c.otaAutoUpdate = false;
    configSanitise(c);
    check(!c.otaAutoUpdate, "auto-update off survives sanitising");

    c = good();
    c.otaAutoUpdate = true;
    configSanitise(c);
    check(c.otaAutoUpdate, "auto-update on survives sanitising");
  }

  // --- The bright window ----------------------------------------------------
  // tm_wday: 0 = Sunday .. 6 = Saturday. Default window is 12:00-20:00 on
  // weekdays, which is when this sign is actually looked at.
  {
    Config c = good();
    check(c.brightStartHour == 12 && c.brightEndHour == 20, "defaults are noon to 8pm");
    check(c.brightWeekdaysOnly, "and weekdays only");

    check(!inBrightWindow(c, 11, 3), "11:00 Wednesday is before the window");
    check(inBrightWindow(c, 12, 3),  "12:00 Wednesday is the first bright hour");
    check(inBrightWindow(c, 19, 3),  "19:00 Wednesday is still bright");
    check(!inBrightWindow(c, 20, 3), "20:00 is exclusive — already dim");
    check(!inBrightWindow(c, 3, 3),  "the small hours are dim");

    check(!inBrightWindow(c, 14, 6), "Saturday afternoon stays dim");
    check(!inBrightWindow(c, 14, 0), "Sunday afternoon stays dim");
    check(inBrightWindow(c, 14, 1),  "Monday afternoon is bright");
    check(inBrightWindow(c, 14, 5),  "Friday afternoon is bright");

    // Weekend restriction lifted.
    c.brightWeekdaysOnly = false;
    check(inBrightWindow(c, 14, 6), "with weekdays-only off, Saturday is bright");
    check(!inBrightWindow(c, 9, 6),  "but still only inside the hours");
  }

  // A window that wraps past midnight, for a shift that is not nine to five.
  {
    Config c = good();
    c.brightWeekdaysOnly = false;
    c.brightStartHour = 22;
    c.brightEndHour = 6;
    check(inBrightWindow(c, 23, 3),  "23:00 is inside a 22->6 window");
    check(inBrightWindow(c, 2, 3),   "02:00 is inside it too");
    check(inBrightWindow(c, 22, 3),  "the start hour is inclusive");
    check(!inBrightWindow(c, 6, 3),  "the end hour is exclusive");
    check(!inBrightWindow(c, 12, 3), "midday is outside it");
  }

  // Equal bounds mean always bright, not never — "never" is already reachable
  // by turning the day brightness down.
  {
    Config c = good();
    c.brightWeekdaysOnly = false;
    c.brightStartHour = 9;
    c.brightEndHour = 9;
    check(inBrightWindow(c, 9, 3), "equal bounds: bright at the boundary");
    check(inBrightWindow(c, 3, 3), "equal bounds: bright at 03:00 too");
    check(inBrightWindow(c, 23, 3), "equal bounds: bright all day");
  }

  // Out-of-range hours must not silently produce a sign that never brightens.
  {
    Config c = good();
    c.brightStartHour = 99;
    c.brightEndHour = 200;
    configSanitise(c);
    check(c.brightStartHour <= 23 && c.brightEndHour <= 23, "hours clamped into range");
    check(inBrightWindow(c, 14, 3), "and the restored defaults are usable");

    check(!inBrightWindow(good(), -1, 3), "a nonsense hour is not bright");
    check(!inBrightWindow(good(), 24, 3), "nor is hour 24");
  }

  // --- The configured route resolves the way the board expects --------------
  {
    Config c = good();
    const Direction d = routeDirection(c.originIdx, c.destIdx);
    check(d == DIR_SOUTH, "San Francisco to San Jose Diridon is southbound");
    check(stationStopId(c.originIdx, d) == 70012, "and polls the southbound platform");
  }

  if (failures == 0) printf("test_config: all checks passed\n");
  return failures ? 1 : 0;
}
