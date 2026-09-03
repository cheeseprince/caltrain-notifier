#include "config.h"

#include <string.h>

namespace {
uint8_t clampBrightness(uint8_t v) {
  // Never allow 0: a screen that has gone completely black is indistinguishable
  // from a dead device, and with no touch there is no way to wake it.
  if (v < 5) return 5;
  if (v > 100) return 100;
  return v;
}
}  // namespace

Config configDefaults() {
  Config c{};
  c.ssid[0] = '\0';
  c.pass[0] = '\0';
  c.token[0] = '\0';
  c.originIdx = -1;
  c.destIdx = -1;
  c.brightnessDay = 100;
  c.brightnessNight = 25;
  c.brightStartHour = 12;
  c.brightEndHour = 20;
  c.brightWeekdaysOnly = true;
  c.redUnder = kUrgencyDefaults.redUnder;
  c.yellowUnder = kUrgencyDefaults.yellowUnder;
  c.otaAutoUpdate = true;
  return c;
}

bool inBrightWindow(const Config& c, int hour, int dayOfWeek) {
  if (hour < 0 || hour > 23) return false;
  if (c.brightWeekdaysOnly && (dayOfWeek == 0 || dayOfWeek == 6)) return false;

  // Equal bounds mean the window covers the whole day rather than none of it:
  // "always bright" is a setting someone might reasonably want, whereas "never
  // bright" is already available by turning the day brightness down.
  if (c.brightStartHour == c.brightEndHour) return true;

  if (c.brightStartHour < c.brightEndHour) {
    return hour >= c.brightStartHour && hour < c.brightEndHour;
  }
  // Wraps past midnight, e.g. 22 to 6.
  return hour >= c.brightStartHour || hour < c.brightEndHour;
}

bool configComplete(const Config& c) {
  if (c.ssid[0] == '\0') return false;
  if (strlen(c.token) < TOKEN_MIN_USABLE) return false;
  return routeValid(c.originIdx, c.destIdx);
}

void configSanitise(Config& c) {
  c.ssid[SSID_MAX] = '\0';
  c.pass[PASS_MAX] = '\0';
  c.token[TOKEN_MAX] = '\0';

  // A route that is half-set, out of range, or origin==destination is treated
  // as unset rather than partially honoured — the portal then asks for both.
  if (!routeValid(c.originIdx, c.destIdx)) {
    c.originIdx = -1;
    c.destIdx = -1;
  }

  c.brightnessDay = clampBrightness(c.brightnessDay);
  c.brightnessNight = clampBrightness(c.brightnessNight);

  // An hour outside 0..23 would make inBrightWindow() unsatisfiable, leaving a
  // permanently dim sign with no obvious cause. Fall back to the defaults.
  if (c.brightStartHour > 23) c.brightStartHour = 12;
  if (c.brightEndHour > 23) c.brightEndHour = 20;

  // Out-of-range or inverted thresholds would leave a colour that can never
  // appear on the border; urgency.h owns what "in range" means here.
  UrgencyThresholds t{c.redUnder, c.yellowUnder};
  urgencySanitise(t);
  c.redUnder = t.redUnder;
  c.yellowUnder = t.yellowUnder;
}

#ifdef ARDUINO
#include <Preferences.h>

namespace {
constexpr const char* NS = "caltrain";
}

void configLoad(Config& c) {
  c = configDefaults();

  Preferences p;
  if (!p.begin(NS, /*readOnly=*/true)) return;  // never written: defaults stand

  p.getString("ssid", c.ssid, sizeof(c.ssid));
  p.getString("pass", c.pass, sizeof(c.pass));
  p.getString("token", c.token, sizeof(c.token));
  c.originIdx = (int8_t)p.getChar("origin", -1);
  c.destIdx = (int8_t)p.getChar("dest", -1);
  c.brightnessDay = p.getUChar("brday", 100);
  c.brightnessNight = p.getUChar("brnight", 25);
  c.brightStartHour = p.getUChar("bstart", 12);
  c.brightEndHour = p.getUChar("bend", 20);
  c.brightWeekdaysOnly = p.getBool("bwkonly", true);
  // A device updating from a build that predates these keys reads the defaults
  // here, which are the thresholds it was already using.
  c.redUnder = p.getUChar("redmin", kUrgencyDefaults.redUnder);
  c.yellowUnder = p.getUChar("yelmin", kUrgencyDefaults.yellowUnder);
  c.otaAutoUpdate = p.getBool("otaauto", true);
  p.end();

  configSanitise(c);
}

void configSave(const Config& c) {
  Config tmp = c;
  configSanitise(tmp);

  Preferences p;
  if (!p.begin(NS, /*readOnly=*/false)) return;
  p.putString("ssid", tmp.ssid);
  p.putString("pass", tmp.pass);
  p.putString("token", tmp.token);
  p.putChar("origin", tmp.originIdx);
  p.putChar("dest", tmp.destIdx);
  p.putUChar("brday", tmp.brightnessDay);
  p.putUChar("brnight", tmp.brightnessNight);
  p.putUChar("bstart", tmp.brightStartHour);
  p.putUChar("bend", tmp.brightEndHour);
  p.putBool("bwkonly", tmp.brightWeekdaysOnly);
  p.putUChar("redmin", tmp.redUnder);
  p.putUChar("yelmin", tmp.yellowUnder);
  p.putBool("otaauto", tmp.otaAutoUpdate);
  p.end();
}

void configClear() {
  Preferences p;
  if (!p.begin(NS, /*readOnly=*/false)) return;
  p.clear();
  p.end();
}
#endif  // ARDUINO
