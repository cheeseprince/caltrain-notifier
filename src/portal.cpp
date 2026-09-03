#ifdef ARDUINO
#include "portal.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>   // esp_wifi_disconnect(), for startScan() below

#include "csrf_check.h"
#include "html_escape.h"
#include "stations.h"
#include "wifi_pass_policy.h"

namespace {

constexpr uint16_t DNS_PORT = 53;
constexpr uint32_t PORTAL_TIMEOUT_MS = 10UL * 60UL * 1000UL;  // 10 minutes
constexpr uint32_t SCAN_WAIT_MS = 6000;  // bound on handleRescan's wait
constexpr int MAX_SCAN = 24;

WebServer  g_server(80);
DNSServer  g_dns;
Config*    g_cfg = nullptr;
bool       g_saved = false;
char       g_csrf[17];
char       g_apSsid[24];
char       g_apPass[15];   // AP password + NUL; see kPassLen in makeApIdentity()
                           // NOTE: keep in step with kPassLen — strncpy() below
                           // truncates silently, which would undo the widening.

// --- Access point identity -------------------------------------------------

void makeApIdentity() {
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(g_apSsid, sizeof(g_apSsid), "Caltrain-%04X", (unsigned)(mac & 0xFFFF));

  // Persist the AP password rather than regenerating it. Rotating it on every
  // boot would strand any phone that saved the network.
  Preferences p;
  p.begin("caltrain", false);
  // 14 characters over a 31-symbol alphabet is ~69.36 bits. The source is public,
  // so an attacker knows both the alphabet and the length; the entropy is all
  // that stands between a captured WPA2 handshake and an offline crack. The
  // alphabet is lowercase only and drops 0, 1, i, l and o, because this
  // password is read off a screen and typed into a phone by hand.
  static const char kAlphabet[] = "23456789abcdefghjkmnpqrstuvwxyz";  // 31 chars
  constexpr int kPassLen = 14;
  String saved = p.getString("appass", "");
  if (saved.length() != kPassLen) {
    char buf[kPassLen + 1];
    for (int i = 0; i < kPassLen; i++)
      buf[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
    buf[kPassLen] = '\0';
    saved = buf;
    p.putString("appass", saved);
  }
  p.end();
  static_assert(sizeof(g_apPass) > kPassLen,
                "g_apPass must hold kPassLen characters plus a NUL");
  strncpy(g_apPass, saved.c_str(), sizeof(g_apPass) - 1);
  g_apPass[sizeof(g_apPass) - 1] = '\0';
}

void makeCsrf() {
  for (int i = 0; i < 16; i++) g_csrf[i] = "0123456789abcdef"[esp_random() & 0xF];
  g_csrf[16] = '\0';
}

bool csrfOk() {
  return g_server.hasArg("csrf") && csrfMatches(g_server.arg("csrf"), g_csrf);
}

// --- Pages ------------------------------------------------------------------

String stationOptions(int selected) {
  String s;
  s.reserve(kStationCount * 48);
  for (int i = 0; i < kStationCount; i++) {
    s += "<option value='";
    s += i;
    s += (i == selected) ? "' selected>" : "'>";
    // htmlEscape() is templated (see html_escape.h) so it can also run over
    // std::string on the host; template deduction won't do the implicit
    // const char* -> String conversion the old non-template signature did,
    // so that conversion is spelled out here.
    s += htmlEscape(String(kStations[i].name));
    s += "</option>";
  }
  return s;
}

// Hours of the day as a dropdown, 24-hour to match the display.
String hourOptions(int selected) {
  String s;
  s.reserve(24 * 40);
  for (int h = 0; h < 24; h++) {
    char label[8];
    snprintf(label, sizeof(label), "%02d:00", h);
    s += "<option value='";
    s += h;
    s += (h == selected) ? "' selected>" : "'>";
    s += label;
    s += "</option>";
  }
  return s;
}

// Start an asynchronous scan for nearby networks.
//
// The disconnect is load-bearing, not defensive. In AP_STA mode the STA
// interface is started but has never been associated, and esp_wifi_scan_start()
// rejects a scan in that state with ESP_ERR_WIFI_STATE (0x3006) — measured on
// an ESP32-D0WD, Arduino core 2.0.17. It fails the same way in STA-only mode
// with the AP torn down, so this is not an AP/STA radio conflict: the STA
// simply has to be put into a scannable idle state first.
//
// Without it every scan returns WIFI_SCAN_FAILED forever, the dropdown is
// always empty, and the rescan link cannot help because rescanning hits the
// same refusal. Disconnecting does not disturb the SoftAP — verified on
// hardware: the AP stayed up on 192.168.4.1 with a client attached.
void startScan() {
  esp_wifi_disconnect();   // ignore the result: not-connected is the normal case
  WiFi.scanDelete();
  WiFi.scanNetworks(/*async=*/true);
}

void handleRoot() {
  String nets;
  const int n = WiFi.scanComplete();
  if (n > 0) {
    const int shown = n < MAX_SCAN ? n : MAX_SCAN;
    for (int i = 0; i < shown; i++) {
      const String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      const String esc = htmlEscape(ssid);
      nets += "<option value='" + esc + "'>" + esc + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
  }
  if (!nets.length()) {
    // scanComplete() returns -1 while a scan is running and -2 when one failed
    // or was never started. Collapsing both into "none found" is what made a
    // broken scan indistinguishable from an empty neighbourhood, so say which.
    const char* why = (n == WIFI_SCAN_RUNNING) ? "-- scanning... --"
                    : (n == WIFI_SCAN_FAILED)  ? "-- scan failed, rescan --"
                                               : "-- no networks found, rescan --";
    nets = String("<option value=''>") + why + "</option>";
  }

  String html;
  html.reserve(7000);
  html += F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>Caltrain Notifier setup</title><style>"
            "body{font-family:system-ui,sans-serif;margin:0;padding:20px;background:#111;color:#eee;}"
            "h1{font-size:20px;margin:0 0 4px}p.sub{color:#888;margin:0 0 20px;font-size:14px}"
            "label{display:block;margin:14px 0 4px;font-size:14px;color:#bbb}"
            "input,select{width:100%;box-sizing:border-box;padding:10px;font-size:16px;"
            "background:#222;color:#eee;border:1px solid #444;border-radius:6px}"
            "button{margin-top:22px;width:100%;padding:14px;font-size:17px;border:0;border-radius:6px;"
            "background:#0a7;color:#fff}"
            "a.re{color:#0af;font-size:13px}"
            ".hint{color:#777;font-size:12px;margin-top:4px}</style>");
  html += F("<h1>Caltrain Notifier</h1><p class=sub>One-time setup.</p><form method=POST action='/save'>");
  html += "<input type=hidden name=csrf value='" + String(g_csrf) + "'>";

  html += F("<label>WiFi network</label><select name=ssid>");
  html += nets;
  html += F("</select><div class=hint><a class=re href='/rescan'>rescan</a></div>");

  html += F("<label>WiFi password</label><input name=pass type=password "
            "placeholder='leave blank for an open network'>");
  // Never pre-filled -- see resolveWifiPassword() in wifi_pass_policy.h for why
  // a blank submission still needs to distinguish "open network" from
  // "unchanged" when a password is already stored.
  if (g_cfg->pass[0] != '\0') {
    html += F("<div class=hint>A password is stored for this network &mdash; leave "
              "blank to keep it. Picking a different network above and leaving this "
              "blank means that network is open.</div>");
  }

  // The token is never echoed back — this page is served over plain HTTP,
  // and every load would otherwise retransmit the stored credential
  // device -> browser. `required` is dropped because a blank submission is
  // now a legitimate way to say "leave the stored token unchanged" (see
  // handleSave()), not an error.
  html += F("<label>511.org API token</label><input name=token "
            "placeholder='xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx' value=''>");
  html += F("<div class=hint>Free from 511.org/open-data/token");
  if (g_cfg->token[0] != '\0') {
    html += F(". A token is already stored &mdash; leave blank to keep it");
  }
  html += F(".</div>");

  html += F("<label>From station</label><select name=origin>");
  html += stationOptions(g_cfg->originIdx);
  html += F("</select>");

  html += F("<label>To station</label><select name=dest>");
  html += stationOptions(g_cfg->destIdx);
  html += F("</select>");
  html += F("<div class=hint>Direction is worked out from the two stations.</div>");

  html += F("<hr style='border:0;border-top:1px solid #333;margin:24px 0'>"
            "<label>Full brightness from</label><select name=bstart>");
  html += hourOptions(g_cfg->brightStartHour);
  html += F("</select><label>until</label><select name=bend>");
  html += hourOptions(g_cfg->brightEndHour);
  html += F("</select>");

  html += F("<label style='display:flex;align-items:center;gap:10px'>"
            "<input type=checkbox name=bwkonly value=1 style='width:auto'");
  if (g_cfg->brightWeekdaysOnly) html += F(" checked");
  html += F("> Weekdays only</label>");
  html += F("<div class=hint>Outside these hours the screen dims. Tap the screen — or "
            "press BOOT on the back — to light it up for a minute.</div>");

  html += F("<hr style='border:0;border-top:1px solid #333;margin:24px 0'>"
            "<label>Red border when under (minutes)</label>"
            "<input name=redmin type=number min=1 max=60 step=1 required value='");
  html += String(g_cfg->redUnder);
  html += F("'><label>Yellow border when under (minutes)</label>"
            "<input name=yelmin type=number min=1 max=60 step=1 required value='");
  html += String(g_cfg->yellowUnder);
  html += F("'><div class=hint>");
  // See urgency.h: the exclusive-bound-to-inclusive-band conversion lives there
  // so it can be host-tested (test_urgency), not in this Arduino-only file.
  char bands[128];
  urgencyBandsText(UrgencyThresholds{g_cfg->redUnder, g_cfg->yellowUnder},
                   bands, sizeof(bands));
  html += bands;
  html += F(" Both count down to departure, and both bounds are exclusive. Set "
            "them to the same number for a red-and-green sign with no yellow.</div>");

  html += F("<label style='display:flex;align-items:center;gap:10px'>"
            "<input type=checkbox name=otaauto value=1 style='width:auto'");
  if (g_cfg->otaAutoUpdate) html += F(" checked");
  html += F("> Install firmware updates automatically</label>");
  html += F("<div class=hint>Checks once a day and only installs signed releases. "
            "Turning this off does not affect an update already installed and "
            "waiting to pass its health check.</div>");

  html += F("<button type=submit>Save and restart</button></form>");
  g_server.send(200, "text/html", html);
}

void handleRescan() {
  startScan();
  // Wait for the scan rather than redirecting straight back: an async scan
  // takes a few seconds, so an immediate 303 lands on a page that reads the
  // result before it exists and always renders "scanning...". Bounded, because
  // this blocks the request; the browser shows its own spinner meanwhile.
  const uint32_t deadline = millis() + SCAN_WAIT_MS;
  while (WiFi.scanComplete() == WIFI_SCAN_RUNNING &&
         (int32_t)(millis() - deadline) < 0) {
    delay(100);
  }
  g_server.sendHeader("Location", "/");
  g_server.send(303);
}

void handleSave() {
  if (!csrfOk()) {
    g_server.send(403, "text/plain", "bad csrf token");
    return;
  }

  const String ssid = g_server.arg("ssid");
  const String pass = g_server.arg("pass");
  const String token = g_server.arg("token");
  const int origin = g_server.arg("origin").toInt();
  const int dest = g_server.arg("dest").toInt();

  // toInt() reads a non-numeric field as 0, which the range check below
  // rejects — so a browser that ignores type=number cannot get past this.
  const int redUnder = g_server.arg("redmin").toInt();
  const int yelUnder = g_server.arg("yelmin").toInt();

  // Must be computed against the OLD g_cfg->ssid/pass, before either is
  // overwritten below -- see wifi_pass_policy.h for the disambiguation rule.
  const String resolvedPass =
      resolveWifiPassword(String(g_cfg->ssid), ssid, String(g_cfg->pass), pass);

  // The token field is never pre-filled (see handleRoot()), so a blank
  // submission is ambiguous between "no token" and "didn't touch it" —
  // resolved in favour of "leave the stored token unchanged" whenever a
  // usable token is already on file. That is only a legitimate submission
  // when there IS a stored token to fall back to; a blank field on a device
  // with no stored token is still an error, same as a too-short one.
  const bool tokenBlank = (token.length() == 0);
  const bool keepStoredToken = tokenBlank && strlen(g_cfg->token) >= TOKEN_MIN_USABLE;

  // Validate before writing anything. Reporting the problem beats saving a
  // configuration that will only fail later, on a screen with no keyboard.
  const char* problem = nullptr;
  if (!ssid.length())                       problem = "Pick a WiFi network.";
  else if (!keepStoredToken && token.length() < TOKEN_MIN_USABLE)
                                             problem = "That token looks too short.";
  else if (!routeValid(origin, dest))       problem = "Pick two different stations.";
  else if (redUnder < URGENCY_MIN_MINUTES || redUnder > URGENCY_MAX_MINUTES ||
           yelUnder < URGENCY_MIN_MINUTES || yelUnder > URGENCY_MAX_MINUTES)
                                             problem = "Border timers must be between 1 and 60 minutes.";
  // configSanitise() would silently raise yellow to meet red here. Saying so
  // instead is the better answer on a form the user is looking at: an inverted
  // pair is far more likely a typo than a request for no yellow band.
  else if (yelUnder < redUnder)              problem = "The yellow timer must be at least the red one.";

  if (problem) {
    String html = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                    "<body style='font-family:system-ui;background:#111;color:#eee;padding:20px'>"
                    "<h2>Not saved</h2><p>");
    html += problem;
    html += F("</p><p><a style='color:#0af' href='/'>Go back</a></p>");
    g_server.send(400, "text/html", html);
    return;
  }

  strncpy(g_cfg->ssid, ssid.c_str(), sizeof(g_cfg->ssid) - 1);
  g_cfg->ssid[sizeof(g_cfg->ssid) - 1] = '\0';
  strncpy(g_cfg->pass, resolvedPass.c_str(), sizeof(g_cfg->pass) - 1);
  g_cfg->pass[sizeof(g_cfg->pass) - 1] = '\0';
  // A blank field here means "keep the stored token" (validated above), so
  // g_cfg->token is left untouched rather than overwritten with "".
  if (!tokenBlank) {
    strncpy(g_cfg->token, token.c_str(), sizeof(g_cfg->token) - 1);
    g_cfg->token[sizeof(g_cfg->token) - 1] = '\0';
  }
  g_cfg->originIdx = (int8_t)origin;
  g_cfg->destIdx = (int8_t)dest;

  // The dropdowns only offer 0..23, but a hand-crafted POST can say anything;
  // configSanitise() is the backstop that keeps an out-of-range hour from
  // leaving the sign permanently dim.
  g_cfg->brightStartHour = (uint8_t)g_server.arg("bstart").toInt();
  g_cfg->brightEndHour = (uint8_t)g_server.arg("bend").toInt();
  // Range-checked above; configSanitise() below is still the backstop that
  // keeps a hand-crafted POST from inverting the bands.
  g_cfg->redUnder = (uint8_t)redUnder;
  g_cfg->yellowUnder = (uint8_t)yelUnder;

  // An unchecked box is simply absent from the POST body.
  g_cfg->brightWeekdaysOnly = g_server.hasArg("bwkonly");
  g_cfg->otaAutoUpdate = g_server.hasArg("otaauto");

  configSanitise(*g_cfg);
  configSave(*g_cfg);

  String html = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<body style='font-family:system-ui;background:#111;color:#eee;padding:20px'>"
                  "<h2>Saved</h2><p>Restarting. The sign will join <b>");
  html += htmlEscape(String(g_cfg->ssid));  // see the deduction note in stationOptions()
  html += F("</b> and start showing departures.</p>");
  g_server.send(200, "text/html", html);

  g_saved = true;
}

}  // namespace

namespace portal {

bool run(Config& cfg, void (*pump)(const char*, const char*, const char*)) {
  g_cfg = &cfg;
  g_saved = false;
  makeApIdentity();
  makeCsrf();

  // AP_STA rather than AP: scanning for the user's network requires the station
  // interface to exist alongside the access point.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(g_apSsid, g_apPass);
  delay(200);
  const IPAddress ip = WiFi.softAPIP();

  startScan();

  g_dns.start(DNS_PORT, "*", ip);

  g_server.on("/", handleRoot);
  g_server.on("/save", HTTP_POST, handleSave);
  g_server.on("/rescan", handleRescan);
  g_server.onNotFound(handleRoot);  // captive-portal catch-all
  g_server.begin();

  char url[24];
  snprintf(url, sizeof(url), "http://%s", ip.toString().c_str());

  const uint32_t deadline = millis() + PORTAL_TIMEOUT_MS;
  while (!g_saved) {
    g_dns.processNextRequest();
    g_server.handleClient();
    if (pump) pump(g_apSsid, g_apPass, url);

    // Signed comparison: wrap-safe across the millis() rollover.
    if ((int32_t)(millis() - deadline) >= 0) break;
    delay(2);
  }

  // Give the browser a moment to receive the confirmation page before the
  // radio goes away underneath it.
  if (g_saved) delay(1200);

  g_server.stop();
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  return g_saved;
}

}  // namespace portal
#endif  // ARDUINO
