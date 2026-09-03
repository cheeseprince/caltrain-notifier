// config.h — everything the user sets up once, and where it lives.
//
// Stored in NVS, never compiled in. The 511 token is a credential and the WiFi
// password obviously is; neither belongs in a binary that might be published or
// handed to someone to flash.
//
// The validation half is pure and host-tested. The NVS half is behind
// #ifdef ARDUINO, so the rules about what counts as a usable configuration are
// checked on the desktop rather than only discovered on the device.
#pragma once
#include <stdint.h>

#include "route.h"
#include "urgency.h"

// A 511 token is a 36-character UUID. The buffer allows a little slack in case
// the format ever changes, but the length check below stays strict enough to
// catch an empty or obviously truncated paste.
inline constexpr int TOKEN_MAX = 48;
inline constexpr int TOKEN_MIN_USABLE = 8;

inline constexpr int SSID_MAX = 32;
inline constexpr int PASS_MAX = 64;

struct Config {
  char ssid[SSID_MAX + 1];
  char pass[PASS_MAX + 1];
  char token[TOKEN_MAX + 1];

  // Indices into kStations, or -1 when unset. Stored as indices rather than
  // stop_ids so the direction is always derived and never stale.
  int8_t originIdx;
  int8_t destIdx;

  // Backlight percentages, 5..100. "Day" is the bright window below; "night"
  // is everything outside it.
  uint8_t brightnessDay;
  uint8_t brightnessNight;

  // The window in which the sign runs at full brightness, as local hours.
  // Defaults to 12:00-20:00, which is when this particular sign is looked at.
  // End is exclusive: 12 to 20 means the last bright hour is 19:59.
  //
  // A window whose end is before its start wraps past midnight (22 to 6 is a
  // legitimate overnight-shift setting), and start == end means always bright.
  uint8_t brightStartHour;
  uint8_t brightEndHour;

  // Restrict the bright window to Monday-Friday. A commute sign has no reason
  // to be at full brightness on a Sunday afternoon.
  bool brightWeekdaysOnly;

  // Border-colour bounds, in whole minutes. Both are exclusive and both read
  // the same way — see urgency.h, which owns the rule and the 1..60 limits.
  // Defaults are 10 and 16, i.e. exactly the behaviour the sign had when these
  // were compiled in, so taking this update changes nothing until someone opens
  // the setup page.
  uint8_t redUnder;
  uint8_t yellowUnder;

  // Let the sign install firmware updates on its own daily check. Defaults to
  // on: a sign that silently stops updating is a worse failure mode than one
  // that updates. This only gates starting a NEW update (ota_task::start()) —
  // it must never gate otaHealthReport()/otaHealthPending(), or disabling it
  // right after an install would strand that build in pending-verify and the
  // bootloader would roll back a perfectly good image. See main.cpp's OTA gate.
  bool otaAutoUpdate;
};

// A Config with the defaults a first boot should see.
Config configDefaults();

// Is there enough here to run? A missing token or an unset route means the
// setup portal must come up instead of the board.
//
// An empty WiFi password is allowed — open networks exist — but an empty SSID
// is not.
bool configComplete(const Config& c);

// Clamp anything out of range and normalise an invalid route to unset.
// Applied on load so a corrupted or half-written NVS record cannot put the
// firmware into a state the rest of the code does not expect.
void configSanitise(Config& c);

// Should the sign be at full brightness at this local hour and weekday?
// `dayOfWeek` follows struct tm's tm_wday: 0 = Sunday .. 6 = Saturday.
bool inBrightWindow(const Config& c, int hour, int dayOfWeek);

#ifdef ARDUINO
// NVS-backed persistence. Namespace "caltrain".
void configLoad(Config& c);
void configSave(const Config& c);
void configClear();
#endif
