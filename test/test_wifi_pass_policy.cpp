// Host coverage for resolveWifiPassword() -- the disambiguation rule added
// alongside the F-2 token fix: the WiFi password field is never pre-filled,
// so a blank submission is ambiguous between "this network is open" and "I
// didn't touch the password." Resolved on whether the submitted SSID matches
// the stored one.
#include <cstdio>
#include <string>

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

  if (failures) { std::printf("%d FAILED\n", failures); return 1; }
  std::printf("test_wifi_pass_policy: ALL PASS\n");
  return 0;
}
