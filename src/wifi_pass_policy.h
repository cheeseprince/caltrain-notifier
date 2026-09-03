#pragma once

// Decide what to store for the WiFi password on a portal save.
//
// The password field is never pre-filled (portal.cpp's handleRoot(), by
// design — see SECURITY.md), so a blank submission is ambiguous: it could
// mean "this network is open" or "I didn't touch the password." The SSID
// disambiguates it:
//
//   * SSID unchanged + password blank  -> the user didn't touch the network
//     at all (e.g. they reopened the portal only to change a station or the
//     token); keep the stored password rather than wiping it.
//   * SSID changed + password blank    -> selecting a different network
//     legitimately means "this one is open"; take the blank as-is. An open
//     network is a valid configuration (config.cpp / configComplete()).
//   * password non-blank               -> always take the submitted value,
//     regardless of the SSID.
//
// This mirrors the token field's "blank means unchanged" rule (F-2), but the
// token has no equivalent to "open network" — an empty token is never valid,
// so the token rule doesn't need the SSID check this one does.
template <class Str>
Str resolveWifiPassword(const Str& storedSsid, const Str& submittedSsid,
                         const Str& storedPass, const Str& submittedPass) {
  if (storedSsid == submittedSsid && submittedPass.length() == 0) return storedPass;
  return submittedPass;
}

// Which <option> the WiFi dropdown should default to when the setup page
// loads.
//
// Without this, an unmarked <select> defaults to whichever network is FIRST
// in scan order -- not necessarily the one already configured. A user who
// opens the portal only to change an unrelated setting (brightness, the
// urgency timers) and saves without touching this field then silently
// overwrites the WiFi configuration with whatever the radio happened to list
// first, and -- because resolveWifiPassword() above reads a changed SSID as
// "this network is open" -- wipes the stored password at the same time.
template <class Str>
struct SsidDefault {
  int matchIndex;       // index into `scanned` to mark selected, or -1
  bool addStoredOption;  // true: the caller must synthesise an extra, selected
                         // <option> for `stored` because it was not found
};

// `scanned` holds `scannedCount` entries from this scan pass (may be zero,
// e.g. while a scan is still running). `stored` is Config's current ssid,
// which may be empty on a fresh device -- there is nothing to protect there,
// so both the match and the synthetic option are correctly absent.
template <class Str>
SsidDefault<Str> ssidDefaultFor(const Str* scanned, int scannedCount, const Str& stored) {
  if (stored.length() == 0) return SsidDefault<Str>{-1, false};
  for (int i = 0; i < scannedCount; i++) {
    if (scanned[i] == stored) return SsidDefault<Str>{i, false};
  }
  return SsidDefault<Str>{-1, true};
}
