// Host coverage for wifi_pass_policy.h -- both directions of how the setup
// portal's WiFi field relates to the stored config.
//
// resolveWifiPassword(): the disambiguation rule added alongside the F-2
// token fix. The WiFi password field is never pre-filled, so a blank
// submission is ambiguous between "this network is open" and "I didn't touch
// the password." Resolved on whether the submitted SSID matches the stored
// one.
//
// ssidDefaultFor(): which <option> the WiFi dropdown should default to on
// page load. Without an explicit `selected` marker an unmarked <select>
// defaults to whichever network the radio happened to list first -- not
// necessarily the one already configured. A user who opens the portal only
// to change an unrelated setting and saves without touching this field then
// silently overwrites the WiFi configuration (and, via resolveWifiPassword()
// above, wipes the stored password too, since an apparently "different" SSID
// reads as "this network is open").
#include <cstdio>
#include <string>
#include <vector>

#include "../src/wifi_pass_policy.h"

namespace {
int failures = 0;
void check(bool cond, const char* what) {
  if (!cond) { std::printf("FAIL: %s\n", what); failures++; }
}
std::string resolve(const std::string& storedSsid, const std::string& submittedSsid,
                     const std::string& storedPass, const std::string& submittedPass) {
  return resolveWifiPassword(storedSsid, submittedSsid, storedPass, submittedPass);
}

SsidDefault<std::string> pick(const std::vector<std::string>& scanned, const std::string& stored) {
  return ssidDefaultFor(scanned.data(), (int)scanned.size(), stored);
}
}  // namespace

int main() {
  // same SSID + blank password -> keeps the stored password. This is the
  // case the coordinator's bug report is about: reopening the portal to
  // change a station or the token must not wipe a working WiFi password.
  check(resolve("HomeNet", "HomeNet", "s3cr3t", "") == "s3cr3t",
        "wifi-pass: same SSID + blank password keeps stored");

  // same SSID + new password -> takes the new one. Blank isn't special-cased
  // once the user has actually typed something.
  check(resolve("HomeNet", "HomeNet", "s3cr3t", "newpass") == "newpass",
        "wifi-pass: same SSID + new password takes new");

  // changed SSID + blank password -> takes blank. Selecting a different
  // network and leaving the password field empty legitimately means "this
  // one is open" -- config.cpp allows an empty password as a usable config.
  check(resolve("HomeNet", "CafeWiFi", "s3cr3t", "") == "",
        "wifi-pass: changed SSID + blank password takes blank (open network)");

  // changed SSID + new password -> takes new.
  check(resolve("HomeNet", "CafeWiFi", "s3cr3t", "newpass") == "newpass",
        "wifi-pass: changed SSID + new password takes new");

  // no stored password + blank -> takes blank. First-time setup on an open
  // network, or a device that was already configured for one.
  check(resolve("HomeNet", "HomeNet", "", "") == "",
        "wifi-pass: no stored password + blank submission takes blank");
  check(resolve("HomeNet", "CafeWiFi", "", "") == "",
        "wifi-pass: no stored password + blank submission + changed SSID takes blank");

  // --- ssidDefaultFor() ------------------------------------------------------

  // The stored network is present in the scan: mark that index selected,
  // no synthetic option needed.
  {
    std::vector<std::string> scanned{"CafeWiFi", "HomeNet", "Neighbors"};
    SsidDefault<std::string> d = pick(scanned, "HomeNet");
    check(d.matchIndex == 1, "ssid-default: found -> the matching scan index");
    check(!d.addStoredOption, "ssid-default: found -> no synthetic option needed");
  }

  // The stored network is not among the scan results (out of range, hidden
  // SSID, or just not answering this pass) -- this is the exact case that
  // silently overwrote WiFi config before this fix, so it must ask for a
  // synthetic option rather than leaving the first scanned network selected.
  {
    std::vector<std::string> scanned{"CafeWiFi", "Neighbors"};
    SsidDefault<std::string> d = pick(scanned, "HomeNet");
    check(d.matchIndex == -1, "ssid-default: not found -> no scan index to select");
    check(d.addStoredOption, "ssid-default: not found -> synthesize the stored option");
  }

  // No scan results at all yet (still scanning, or scan failed) but a
  // network is already configured: still ask for the synthetic option, so
  // the dropdown is never left with nothing but an unrelated placeholder.
  {
    std::vector<std::string> scanned;
    SsidDefault<std::string> d = pick(scanned, "HomeNet");
    check(d.matchIndex == -1, "ssid-default: empty scan -> no scan index");
    check(d.addStoredOption, "ssid-default: empty scan + stored -> synthesize the option");
  }

  // A fresh device has no stored SSID to protect -- nothing to default to,
  // and no synthetic option to inject.
  {
    std::vector<std::string> scanned{"CafeWiFi", "Neighbors"};
    SsidDefault<std::string> d = pick(scanned, "");
    check(d.matchIndex == -1, "ssid-default: no stored SSID -> no match");
    check(!d.addStoredOption, "ssid-default: no stored SSID -> nothing to synthesize");
  }

  {
    std::vector<std::string> scanned;
    SsidDefault<std::string> d = pick(scanned, "");
    check(d.matchIndex == -1 && !d.addStoredOption,
          "ssid-default: no scan and nothing stored -> the placeholder wins");
  }

  // A mesh or repeated SSID: the first occurrence is the one selected, same
  // as the browser would pick among identically-valued options.
  {
    std::vector<std::string> scanned{"Neighbors", "HomeNet", "HomeNet"};
    SsidDefault<std::string> d = pick(scanned, "HomeNet");
    check(d.matchIndex == 1, "ssid-default: duplicate SSID -> the first occurrence");
  }

  if (failures) { std::printf("%d FAILED\n", failures); return 1; }
  std::printf("test_wifi_pass_policy: ALL PASS\n");
  return 0;
}
