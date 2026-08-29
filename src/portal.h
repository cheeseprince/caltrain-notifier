// portal.h — first-boot setup over a phone.
//
// The device raises its own access point and serves a form: WiFi network and
// password, the 511 API token, and the two stations. Adapted from the captive
// portal in obd-gauge-cluster, keeping its security properties:
//
//   * the AP password is random per device and stored in NVS, not compiled in.
//     A constant would be identical on every unit AND readable with `strings`
//     on any published binary; a MAC-derived one is public, since the MAC is
//     the BSSID being broadcast.
//   * every state-changing request carries a per-session CSRF token.
//   * scanned SSIDs are HTML-escaped. An SSID is attacker-controlled text and
//     any nearby device can choose one.
//
// Stations are chosen from a dropdown built from kStations, never typed, so a
// stop_id is never entered by hand and the direction is always derived.
#pragma once
#ifdef ARDUINO
#include "config.h"

namespace portal {

// Run the setup portal until the user saves, or until the timeout expires.
//
// Blocking. `pump` is called continuously with a short status string so the
// caller can keep the screen alive; it must not block.
//
// Returns true if a configuration was saved. The caller is expected to reboot.
bool run(Config& cfg, void (*pump)(const char* apSsid, const char* apPass,
                                   const char* url));

}  // namespace portal
#endif  // ARDUINO
