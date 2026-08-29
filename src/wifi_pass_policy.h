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
