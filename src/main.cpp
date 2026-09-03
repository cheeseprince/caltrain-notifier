// Caltrain Notifier — next departures on a desk sign.
//
// Elecrow CrowPanel 3.5" (ILI9488 480x320 SPI). No touch: setup happens from a
// phone over a captive portal, and holding BOOT at power-on returns to it.
//
// SHAPE OF THE LOOP
//   fetch   every POLL_INTERVAL_MS, well inside 511's 60-requests-per-hour cap
//   tick    every second, recomputing the countdown from the clock so the
//           numbers keep moving between fetches and the border recolours the
//           moment a train crosses 15 or 10 minutes
//
// Recomputing locally rather than only on fetch is what keeps a once-a-minute
// data source looking live, and it means a failed fetch degrades into slightly
// stale predictions rather than a frozen screen.
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "board_model.h"
#include "config.h"
#include "display_hw.h"
#include "fw_version.h"
#include "ota_health.h"
#include "ota_task.h"
#include "portal.h"
#include "render.h"
#include "route.h"
#include "service_day.h"
#include "net_task.h"
#include "siri_client.h"
#include "timetable.h"

namespace {

// 511 allows 60 requests per rolling hour per token. At 75 seconds this uses 48
// of them, leaving headroom for the retries below rather than sitting exactly
// on the limit where one retry storm means rejection.
constexpr uint32_t POLL_INTERVAL_MS = 75000;
constexpr uint32_t POLL_BACKOFF_MAX_MS = 600000;  // 10 min ceiling after failures
constexpr uint32_t TICK_INTERVAL_MS = 1000;

// Held at power-on, this forces the setup portal. It is the only input the
// device has, and it is the standard ESP32 boot-strapping button.
constexpr int BOOT_BUTTON_PIN = 0;

// How long the boot screen waits for a BOOT press before carrying on. Long
// enough to read the prompt and react; short enough not to be a nuisance on
// every power cycle.
constexpr uint32_t SETUP_PROMPT_MS = 5000;

// How long a BOOT press lights the screen fully, outside the bright window.
constexpr uint32_t WAKE_MS = 60000;

// Once a day. The release channel changes a few times a year at most, so a
// tighter interval would only add failed round trips.
constexpr uint32_t OTA_CHECK_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;

uint32_t g_nextOtaCheckAt = 0;
OtaHealth g_health{};

Config      g_cfg;
BoardModel  g_board;
SiriResult  g_lastLive;
bool        g_haveLive = false;
int64_t     g_lastFetchEpoch = 0;
uint32_t    g_nextPollAt = 0;
uint32_t    g_pollInterval = POLL_INTERVAL_MS;
uint32_t    g_nextTickAt = 0;
uint32_t    g_fetchCount = 0;
uint32_t    g_wakeUntil = 0;   // millis deadline for a temporary full-brightness period
bool        g_tapWasDown = false;
bool        g_bootWasDown = false;
bool        g_otaScreenUp = false;  // true while render::updating() owns the panel

void portalPump(const char* ssid, const char* pass, const char* url) {
  render::portal(ssid, pass, url);
}

// Blocking join with a bounded wait. Distinguishes "network not in range" from
// "in range but would not let us in", because the remedies differ and the sign
// has no other way to tell you.
bool joinWifi(const Config& cfg) {
  char detail[64];
  snprintf(detail, sizeof(detail), "joining %s", cfg.ssid);
  render::status("Connecting", detail);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // a sleeping radio adds seconds to each poll
  WiFi.begin(cfg.ssid, cfg.pass);

  const uint32_t deadline = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && (int32_t)(millis() - deadline) < 0) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) return true;

  // Was it even there? A scan after a failed join tells us which message to show.
  int n = WiFi.scanNetworks();
  if (n == WIFI_SCAN_FAILED) {
    // The first scan after a mode change can fail outright; one retry is enough.
    delay(500);
    n = WiFi.scanNetworks();
  }
  bool visible = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == cfg.ssid) visible = true;
  }
  render::status("WiFi failed",
                 visible ? "network found - check the password"
                         : "network not in range");
  delay(4000);
  return false;
}

// Anything after 2020 means a real NTP answer arrived rather than the epoch the
// chip powers up at.
constexpr int64_t CLOCK_IS_SET_EPOCH = 1600000000;

bool syncClock() {
  // Every stage on screen at once. A single "contacting a time server" line
  // looks identical on a healthy boot and on one that is about to give up, so
  // it says nothing at the moment you most want it to.
  char replyVal[16] = "";
  char zoneVal[32] = "";
  char checkVal[48] = "";

  render::Step rows[render::STEP_ROWS] = {
      {"server", "pool.ntp.org", render::STEP_ACTIVE},
      {"reply",  replyVal,       render::STEP_PENDING},
      {"zone",   zoneVal,        render::STEP_PENDING},
      {"check",  checkVal,       render::STEP_PENDING},
  };
  auto paint = [&]() { render::steps("Setting the clock", rows, render::STEP_ROWS); };
  paint();

  // configTzTime, NOT configTime. The offset-taking configTime(gmtOffset,
  // dstOffset, ...) builds its own TZ string and calls setenv("TZ", ...),
  // which silently replaces the Pacific zone set at start-up — the screen then
  // shows UTC, seven hours out, with nothing to indicate why.
  configTzTime(SERVICE_DAY_TZ, "pool.ntp.org", "time.nist.gov");
  rows[0].state = render::STEP_DONE;
  rows[1].state = render::STEP_ACTIVE;
  paint();

  // Count the wait up on screen. Tenths are assembled from integers rather than
  // printed with %f, which not every newlib build here is configured for.
  const uint32_t started = millis();
  bool answered = false;
  for (int i = 0; i < 100; i++) {  // up to 10 s
    if (time(nullptr) > CLOCK_IS_SET_EPOCH) { answered = true; break; }
    const uint32_t ms = millis() - started;
    snprintf(replyVal, sizeof(replyVal), "%lu.%lus", (unsigned long)(ms / 1000),
             (unsigned long)((ms % 1000) / 100));
    paint();
    delay(100);
  }

  const uint32_t elapsed = millis() - started;
  snprintf(replyVal, sizeof(replyVal), "%lu.%lus", (unsigned long)(elapsed / 1000),
           (unsigned long)((elapsed % 1000) / 100));

  if (!answered) {
    rows[1].state = render::STEP_FAILED;
    snprintf(replyVal, sizeof(replyVal), "no answer");
    paint();
    Serial.println("[TIME]  no NTP answer within 10s");
    delay(4000);  // long enough to read before the caller restarts the device
    return false;
  }
  rows[1].state = render::STEP_DONE;
  rows[2].state = render::STEP_ACTIVE;
  paint();

  // Re-assert the zone regardless. Any Arduino time API that takes offsets can
  // clobber TZ, and this costs nothing next to being wrong by seven hours.
  serviceDayInitTimezone();
  snprintf(zoneVal, sizeof(zoneVal), "%s", SERVICE_DAY_TZ);
  rows[2].state = render::STEP_DONE;
  rows[3].state = render::STEP_ACTIVE;
  paint();

  // Self-check, shown rather than assumed. The zone row above reports the rule
  // that was requested; this one reports what the clock actually reads, which
  // is the only thing that catches the rule failing to take. Pacific is -28800
  // in winter and -25200 on DST; a zero means the zone did not stick.
  const int64_t now = (int64_t)time(nullptr);
  const int32_t offset = localUtcOffset(now);
  const time_t tt = (time_t)now;
  struct tm lt;
  localtime_r(&tt, &lt);

  char abbrev[8], utcOff[12];
  strftime(abbrev, sizeof(abbrev), "%Z", &lt);
  formatUtcOffset(offset, utcOff, sizeof(utcOff));
  snprintf(checkVal, sizeof(checkVal), "%02d:%02d:%02d %s, %s", lt.tm_hour, lt.tm_min,
           lt.tm_sec, abbrev, utcOff);
  rows[3].state = offset == 0 ? render::STEP_FAILED : render::STEP_DONE;
  paint();

  Serial.printf("[TIME]  UTC epoch %lld, local %04d-%02d-%02d %02d:%02d:%02d, %s\n",
                (long long)now, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour,
                lt.tm_min, lt.tm_sec, utcOff);
  if (offset == 0) {
    Serial.println("[TIME]  WARNING: timezone did not apply — the clock will read UTC");
  }

  // Hold briefly. Without it a good boot paints the finished checklist and
  // replaces it in the same breath, which is no more readable than the single
  // line it replaced.
  delay(offset == 0 ? 4000 : 900);
  return true;
}

uint8_t brightnessFor(const ServiceDay& day) {
  // A press wins over the schedule for a minute, so the sign can be woken to
  // read it outside its usual hours without changing the configuration.
  if (g_wakeUntil && (int32_t)(millis() - g_wakeUntil) < 0) return g_cfg.brightnessDay;
  return inBrightWindow(g_cfg, day.localHour, day.dayOfWeek) ? g_cfg.brightnessDay
                                                             : g_cfg.brightnessNight;
}

void wakeScreen(const char* why) {
  g_wakeUntil = millis() + WAKE_MS;
  if (g_wakeUntil == 0) g_wakeUntil = 1;  // 0 is the "no wake pending" sentinel
  Serial.printf("[UI]    %s: full brightness for %lus\n", why,
                (unsigned long)(WAKE_MS / 1000));
}

// Two ways to wake the screen, both edge-detected so that holding a finger or
// the button down does not retrigger every loop.
//
// The tap is the everyday one. BOOT is the backstop: resistive touch on this
// panel is the least reliable thing on the board, and a sign that cannot be
// lit has no way to tell you why.
void pollWake() {
  const bool tap = display::touched();
  if (tap && !g_tapWasDown) wakeScreen("tap");
  g_tapWasDown = tap;

  const bool boot = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (boot && !g_bootWasDown) wakeScreen("BOOT");
  g_bootWasDown = boot;
}

// Show the setup prompt and watch for a press. Returns true if BOOT was held or
// tapped during the window.
//
// The previous behaviour required the button to be down at the instant of boot,
// which is invisible unless you already know about it. This announces itself
// and counts down, which is the only discoverable path back into setup on a
// device with no other input.
bool waitForSetupPrompt() {
  const uint32_t deadline = millis() + SETUP_PROMPT_MS;
  int lastShown = -1;
  while ((int32_t)(millis() - deadline) < 0) {
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) return true;

    const int remain = (int)((deadline - millis()) / 1000) + 1;
    if (remain != lastShown) {
      lastShown = remain;
      char detail[48];
      snprintf(detail, sizeof(detail), "press BOOT for setup   %ds", remain);
      render::splash(detail);
    }
    delay(20);
  }
  return false;
}

uint32_t currentStopId() {
  const Direction dir = routeDirection(g_cfg.originIdx, g_cfg.destIdx);
  return stationStopId(g_cfg.originIdx, dir);
}

// Hand the request to the net task and return immediately. The blocking HTTPS
// round trip now happens on core 0, so loop() keeps painting throughout —
// which is what stops the clock freezing for seconds on every poll.
bool startFetch() {
  if (!net_task::start(g_cfg.token, currentStopId())) return false;
  g_nextPollAt = millis() + g_pollInterval;  // provisional; reset on the result
  return true;
}

// Called from loop() once the net task has a result.
void onFetchResult(const FetchResult& fr) {
  const uint32_t stop = currentStopId();
  const net_task::Progress p = net_task::progress();
  g_fetchCount++;

  // Local date and time on every fetch line, so the console reads as a
  // timeline. Without it a log pulled off the device days later cannot be lined
  // up against anything, and a wrong clock is invisible.
  char stamp[24];
  {
    const time_t tt = time(nullptr);
    struct tm lt;
    localtime_r(&tt, &lt);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &lt);
  }

  // took= is the wall time the fetch spent on core 0. It no longer stalls the
  // display, but it is still the number that says whether 511 or the TLS
  // handshake is being slow. stack= is the net task's worst-case headroom,
  // measured rather than assumed, since the handshake is the deep part.
  Serial.printf("[FETCH] %s #%lu stop=%lu http=%d bytes=%u trains=%d "
                "took=%lums (connect %lu, download %lu) stack=%luB heap=%u min=%u %s\n",
                stamp, (unsigned long)g_fetchCount, (unsigned long)stop, fr.httpCode,
                (unsigned)fr.bytes, fr.siri.count, (unsigned long)p.elapsedMs,
                (unsigned long)p.phaseMs[SIRI_PHASE_CONNECT],
                (unsigned long)p.phaseMs[SIRI_PHASE_DOWNLOAD],
                (unsigned long)net_task::stackHeadroom(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                fr.error ? fr.error : "ok");

  if (fr.transportOk && fr.siri.ok) {
    g_lastLive = fr.siri;
    g_haveLive = true;
    g_health.fetchParsed = true;
    g_lastFetchEpoch = (int64_t)time(nullptr);
    g_pollInterval = POLL_INTERVAL_MS;  // recovered: back to the normal cadence
  } else {
    // Back off on failure so a 511 outage does not burn the hourly quota, and
    // keep showing the timetable in the meantime.
    g_pollInterval = g_pollInterval * 2;
    if (g_pollInterval > POLL_BACKOFF_MAX_MS) g_pollInterval = POLL_BACKOFF_MAX_MS;
    Serial.printf("[FETCH] backing off to %lus\n", (unsigned long)(g_pollInterval / 1000));
  }
  g_nextPollAt = millis() + g_pollInterval;
}

// The boot progress screen, painted from loop-side snapshots while the net task
// works. Only the download row can show a real proportion; connect is one
// opaque library call and says so by counting its own elapsed seconds.
void renderFetchProgress() {
  const net_task::Progress p = net_task::progress();

  char connectVal[24] = "";
  char downloadVal[32] = "";
  char parseVal[24] = "";

  // Each row shows its OWN phase's time. Using the whole-fetch elapsed here
  // left the connect row counting upward long after it had gone green, which
  // read as though it were still working.
  const uint32_t connectMs = p.phaseMs[SIRI_PHASE_CONNECT];
  snprintf(connectVal, sizeof(connectVal), "api.511.org  %lu.%lus",
           (unsigned long)(connectMs / 1000), (unsigned long)((connectMs % 1000) / 100));

  if (p.phase >= SIRI_PHASE_DOWNLOAD) {
    if (p.total > 0) {
      snprintf(downloadVal, sizeof(downloadVal), "%lu / %lu bytes  %lu%%",
               (unsigned long)p.done, (unsigned long)p.total,
               (unsigned long)(p.done * 100 / p.total));
    } else {
      snprintf(downloadVal, sizeof(downloadVal), "%lu bytes", (unsigned long)p.done);
    }
  }
  if (p.phase >= SIRI_PHASE_PARSE) snprintf(parseVal, sizeof(parseVal), "%lu bytes",
                                            (unsigned long)p.done);

  auto stateFor = [&](SiriPhase mine) {
    if (p.phase > mine) return render::STEP_DONE;
    if (p.phase == mine) return render::STEP_ACTIVE;
    return render::STEP_PENDING;
  };

  render::Step rows[3] = {
      {"connect",  connectVal,  stateFor(SIRI_PHASE_CONNECT)},
      {"download", downloadVal, stateFor(SIRI_PHASE_DOWNLOAD)},
      {"parse",    parseVal,    stateFor(SIRI_PHASE_PARSE)},
  };
  render::steps("Loading departures", rows, 3);
}

void doTick() {
  const int64_t now = (int64_t)time(nullptr);
  const ServiceDay day = serviceDayFor(now);
  const uint8_t service = serviceForDate(day.date, day.dayOfWeek);

  display::setBacklight(brightnessFor(day));

  // Live data more than a few minutes old is no longer a prediction. Dropping
  // it back to the schedule is better than presenting stale times as current.
  SiriResult live = g_lastLive;
  int32_t age = -1;
  if (g_haveLive) {
    age = (int32_t)(now - g_lastFetchEpoch);
    if (age > 600) live = SiriResult{};  // older than ten minutes: discard
  } else {
    live = SiriResult{};
  }

  g_board = buildBoard(live, g_cfg.originIdx, g_cfg.destIdx, service, now, day.startEpoch,
                       UrgencyThresholds{g_cfg.redUnder, g_cfg.yellowUnder});

  const char* originName = kStations[g_cfg.originIdx].name;
  const char* destName = kStations[g_cfg.destIdx].name;

  // FINDING F-1 (2026-08 adversarial review): has the compiled schedule run
  // past the last date it can vouch for? Checked once per tick against the
  // CURRENT service day, not wall-clock "today" — the two disagree between
  // midnight and the 03:00 service-day rollover (see service_day.h).
  const bool expired =
      timetableExpired(day.date, kTimetableLastOverride, kTimetableFeedEnd);

  if (g_board.count > 0) {
    render::board(g_board, originName, destName, now, age, expired);
    g_health.boardPainted = true;
    return;
  }

  // Nothing left today: show the first train of the next service day.
  const ServiceDay tomorrow = nextServiceDay(day);
  const uint8_t tomorrowService = serviceForDate(tomorrow.date, tomorrow.dayOfWeek);
  ScheduledDeparture first{};
  const bool have =
      firstScheduled(g_cfg.originIdx, g_cfg.destIdx, tomorrowService, &first);
  // Checked against tomorrow.date, not the `expired` computed above for
  // TODAY's service day: this screen shows TOMORROW's first train, and on the
  // one night a year the expiry threshold falls between the two dates, today
  // being still-valid must not suppress a warning about data that is about
  // tomorrow. Same function, same constants, same source of truth as board()
  // — only the date argument differs, because it is the date being displayed.
  const bool expiredTomorrow =
      timetableExpired(tomorrow.date, kTimetableLastOverride, kTimetableFeedEnd);
  render::overnight(originName, destName, have, first, "tomorrow", expiredTomorrow);

  // C1 (whole-branch review): the overnight screen satisfies boardPainted too,
  // when it actually has something to show. The health gate's rationale for
  // this condition (ota_health.h) is "the render path and timetable lookup
  // work" — and that is exactly what this branch just exercised: firstScheduled()
  // is the timetable lookup, render::overnight() is the render path. An
  // empty-but-correct board (no trains left tonight) is still proof both work;
  // requiring a LIVE departure specifically would roll back a perfectly good
  // build every single night, and all day on routes/windows with no service at
  // all (Gilroy/San Martin/Morgan Hill/Blossom Hill/Capitol on weekends, the
  // entire weekday for Broadway, 01:29-03:00 every night network-wide) — a
  // freshly installed image could never pass the gate during those windows.
  // Guarded on `have`, not merely on reaching this branch: a station with
  // genuinely no next-day service (have == false) has not proven the
  // timetable lookup actually found anything, so it should not count.
  if (have) g_health.boardPainted = true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Caltrain Notifier ===");
#ifdef FW_DATE
  Serial.printf("[BUILD] %s\n", FW_DATE);
#endif
  // A self-updating fleet with no way to tell which build a unit is running
  // is a bad diagnostic position — this is the only place either value was
  // ever printed before M3 (whole-branch review). FW_GIT in particular was
  // referenced by nothing at all prior to this line.
  Serial.printf("[BUILD] %s (%s)\n", FW_VERSION, FW_GIT);
  Serial.printf("[CHIP]  %s rev %d, flash %u bytes, heap %u\n", ESP.getChipModel(),
                ESP.getChipRevision(), (unsigned)ESP.getFlashChipSize(),
                (unsigned)ESP.getFreeHeap());
  Serial.printf("[DATA]  %d stations, %d trips, %d service patterns\n", kStationCount,
                kTripCount, kServiceCount);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  otaHealthBegin();

  display::begin();
  serviceDayInitTimezone();
  render::splash("starting up");
  display::setBacklight(100);

  configLoad(g_cfg);

  // Announce the way back into setup rather than requiring it to be known.
  const bool forced = waitForSetupPrompt();
  if (forced) Serial.println("[SETUP] BOOT pressed: forcing the setup portal");

  if (forced || !configComplete(g_cfg)) {
    render::invalidate();
    if (portal::run(g_cfg, portalPump)) {
      render::status("Saved", "restarting");
      delay(800);
      ESP.restart();
    }
    // Timed out. If there is a usable config, carry on with it; otherwise there
    // is nothing to show, so restart into the portal again rather than sit on a
    // dead screen.
    if (!configComplete(g_cfg)) {
      render::status("Setup timed out", "restarting");
      delay(2000);
      ESP.restart();
    }
  }

  render::invalidate();
  if (!joinWifi(g_cfg)) {
    render::status("No WiFi", "opening setup");
    delay(1500);
    render::invalidate();
    if (portal::run(g_cfg, portalPump)) ESP.restart();
    ESP.restart();
  }
  g_health.wifiJoined = true;

  if (!syncClock()) {
    // Without a clock every countdown would be wrong, and a confidently wrong
    // departure board is worse than none. Restart rather than guess.
    render::status("No time source", "restarting");
    delay(3000);
    ESP.restart();
  }

  Serial.printf("[NET]   %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  {
    const ServiceDay day = serviceDayFor((int64_t)time(nullptr));
    Serial.printf("[TIME]  service day %lu, dow %d, %s\n", (unsigned long)day.date,
                  day.dayOfWeek, kServiceNames[serviceForDate(day.date, day.dayOfWeek)]);
  }

  net_task::begin();

  // The first fetch is the slow one — it pays for the whole TLS handshake — so
  // it gets its own progress screen rather than a frozen panel. This is a wait
  // loop only because there is nothing to show until it finishes; the net task
  // is on core 0 and the screen keeps repainting the entire time.
  render::invalidate();
  startFetch();
  FetchResult first{};
  const uint32_t firstFetchDeadline = millis() + 45000;
  bool arrived = false;
  while (!(arrived = net_task::take(&first))) {
    // Bounded. siriFetch has its own timeouts and should always return, but a
    // boot that hangs forever on a progress screen would be worse than one that
    // falls back to the compiled timetable — which is the whole point of having
    // the timetable in flash.
    if ((int32_t)(millis() - firstFetchDeadline) >= 0) {
      Serial.println("[FETCH] first fetch still running; showing the schedule");
      break;
    }
    renderFetchProgress();
    delay(50);
  }
  if (arrived) onFetchResult(first);
  doTick();

  otaHealthReport(g_health);

  // The first check waits a minute: on a pending-verify boot the gate must
  // settle first, and on any boot there is no reason to compete with the first
  // fetch for the radio.
  g_nextOtaCheckAt = millis() + 60000;
  ota_task::begin();
}

void loop() {
  const uint32_t now = millis();

  pollWake();
  otaHealthReport(g_health);

  // Signed deltas throughout: unsigned subtraction wraps at the 49-day millis()
  // rollover and would stall both timers until a reboot.
  // Collect first, start second. onFetchResult() reads the progress snapshot to
  // log how long the fetch took, and starting a new one before consuming the
  // old result would overwrite that snapshot with a fetch that has just begun.
  // Nothing here blocks: with the net task still working, take() returns false
  // and the board keeps ticking underneath it.
  FetchResult fr{};
  if (net_task::take(&fr)) onFetchResult(fr);

  // Kick off a poll when one is due. net_task::start() refuses while a fetch is
  // still running, so a slow one simply delays the next rather than stacking up.
  if ((int32_t)(now - g_nextPollAt) >= 0 && !net_task::busy() && !ota_task::busy()) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[NET] link lost, reconnecting");
      WiFi.reconnect();
      g_nextPollAt = now + 5000;
    } else {
      startFetch();
    }
  }

  // An update is never started while the running image is still on trial: an
  // image that cannot prove itself must roll back to the previous build, not
  // paper over the problem by fetching a third one.
  //
  // !otaHealthPending() is LOAD-BEARING and THE SOLE GUARD against that (M1,
  // whole-branch review) — nothing in the platform independently enforces it.
  // Arduino's UpdateClass bypasses esp_ota_begin() entirely: it writes
  // straight through esp_partition_* calls, and only _verifyEnd() (called
  // from Update.end()) goes on to call esp_ota_set_boot_partition(), which
  // itself has no pending-verify check of its own. So there is nothing at the
  // ESP-IDF/Arduino layer that would refuse a second concurrent update just
  // because the currently-running image has not yet cleared its own trial.
  // Remove this condition and a broken image on trial would be free to start
  // fetching a THIRD build the moment its OTA_CHECK_INTERVAL_MS came due,
  // chasing an ever-newer image instead of rolling back to the last known
  // good one.
  // g_cfg.otaAutoUpdate only ever gates ota_task::start() below — never
  // otaHealthReport() or otaHealthPending() above. Those two calls run
  // unconditionally regardless of this setting: an image that is still
  // ESP_OTA_IMG_PENDING_VERIFY must get to mark itself valid (or roll back on
  // its own merits) even if the owner disables auto-update in the meantime.
  // Gating the health report too would let a perfectly good, already-installed
  // build get abandoned in pending-verify and reverted by the bootloader —
  // exactly the outcome this setting is not supposed to cause.
  if ((int32_t)(now - g_nextOtaCheckAt) >= 0 && !otaHealthPending() &&
      !net_task::busy() && !ota_task::busy() && WiFi.status() == WL_CONNECTED &&
      g_cfg.otaAutoUpdate) {
    g_nextOtaCheckAt = now + OTA_CHECK_INTERVAL_MS;
    ota_task::start();
  }

  if ((int32_t)(now - g_nextTickAt) >= 0) {
    g_nextTickAt = now + TICK_INTERVAL_MS;
    const ota_task::Progress op = ota_task::progress();
    if (op.busy) {
      // The update screen owns the panel for the duration. render::invalidate()
      // on the way in and out, because each screen caches its own fields.
      if (!g_otaScreenUp) { render::invalidate(); g_otaScreenUp = true; }
      const int pct = op.total ? (int)((op.done * 100ULL) / op.total) : -1;
      const char* step =
          op.phase == OTA_MANIFEST ? "checking for updates" :
          op.phase == OTA_VERIFY   ? "verifying signature"  :
          op.phase == OTA_DOWNLOAD ? "downloading"          :
          op.phase == OTA_INSTALL  ? "installing"           : "working";
      render::updating(FW_VERSION, op.version, step, pct);
    } else {
      if (g_otaScreenUp) { render::invalidate(); g_otaScreenUp = false; }
      doTick();
    }
  }

  delay(10);
}
