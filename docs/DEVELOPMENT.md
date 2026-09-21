# Development

Internals of the [Caltrain Notifier](../README.md): how the board is decided,
how the timetable and releases are maintained, how the tests and screenshots
are built, and the traps already found. Bug reports and pull requests:
[CONTRIBUTING.md](../CONTRIBUTING.md). Board-specific detail:
[HARDWARE.md](HARDWARE.md).

- [How it decides what to show — the details](#how-it-decides-what-to-show--the-details)
- [Keeping the timetable current](#keeping-the-timetable-current)
- [Cutting a release](#cutting-a-release)
- [Host tests](#host-tests)
- [Layout](#layout)
- [Footprint](#footprint)
- [Regenerating the screenshots](#regenerating-the-screenshots)
- [Inherited gotchas](#inherited-gotchas)
  - [Traps found building this one](#traps-found-building-this-one)

## How it decides what to show — the details

The README's [How it decides what to show](../README.md#how-it-decides-what-to-show)
covers the principle: the timetable picks the trains, the live feed corrects
their times. This is the evidence behind it.

### Things measured against the live API, not assumed

All verified with `tools/probe_511.py` — first against an intermediate stop on
weekend service, then re-checked against a terminus on weekday service. The
second capture is committed as `test/fixtures/stopmonitoring_70012.json`:

| Question | Answer |
| :--- | :--- |
| Does `api.511.org` serve HTTPS? | Yes. Certificates are validated against the Mozilla root bundle, not `setInsecure()` |
| Does it gzip? | **Only if you ask.** Without `Accept-Encoding: gzip` it returns plain JSON. The client asks for `identity` explicitly |
| UTF-8 BOM? | No, but the parser tolerates one |
| Response size | ~3.0 KB for three departures |
| Departure cap | **3**, always. `MaximumStopVisits` is accepted and ignored |
| Rate limit | **60 requests/hour per token.** Polling every 75 s uses 48 |
| `RecordedAtTime` | **Inconsistent.** Epoch zero on the intermediate-stop/weekend capture; a real time on the terminus/weekday one (`test/fixtures/stopmonitoring_70012.json`). Not dependable as a freshness signal, so staleness is tracked by the device's own clock |
| Schedule endpoint | `stoptimetable` returns a fixed 4-entry rolling window and ignores `StartTime`/`EndTime`, so it cannot back an offline cache |

That last row is why the timetable is compiled into flash rather than fetched.

### Service patterns

Caltrain runs **three** timetables, not two:

| Pattern | Trips | Runs on |
| :--- | :--- | :--- |
| Weekday | 112 | Mon–Fri |
| Weekend | 66 | Sat/Sun, plus Memorial Day, Labor Day, Thanksgiving, Christmas, New Year's |
| Holiday | 79 | President's Day, Day after Thanksgiving, Christmas Eve, MLK Day |

The third is easy to miss — it appears only in `calendar_dates.txt`, never in
`calendar.txt`, and its trains are numbered `M101` rather than `614`. Treating
those four dates as weekends would show trains that are not running.

### The service day is not the calendar day

Caltrain writes a 00:40 train as `24:40`; it belongs to the previous day's
timetable. At 01:00 on a Saturday the trains still running are Friday's. The
rollover is at 03:00 local — after the last scheduled train (02:30) and before
the first (04:37).

Timezone handling uses the POSIX zone `PST8PDT,M3.2.0,M11.1.0` through libc, so
the same code path runs on the device and in the host tests, including the days
that are 23 or 25 hours long.

---

## Keeping the timetable current

The compiled schedule expires with the GTFS feed (currently valid to
2027-01-31). Caltrain reissues a few times a year. Regenerate **both** headers
from the same feed, since they share a station index space:

```bash
python3 tools/gen_stations.py
python3 tools/gen_timetable.py
cd test && make          # the tests assert against real service
```

Then reflash. Both generators print what they skipped and why — a silently
dropped train is how a timetable quietly goes wrong.

This is enforced, not just documented: CI's `timetable-freshness` job fails 90
days before the compiled schedule's earlier validity bound (the last holiday
override, or the GTFS feed's own end date, whichever comes first), and the
board itself shows "TIMETABLE EXPIRED - holidays may differ" once that bound
has actually passed, rather than silently running a full weekday timetable of
trains that are not running.

---

## Cutting a release

```bash
git tag v1.2.0 && git push --tags
```

That triggers `release.yml`, which builds **every** board (`caltrain`,
`caltrain_v20` and `caltrain_es3c28p` — a release missing any one leaves that
board unable to ever update), signs the manifest with the
`OTA_SIGNING_KEY` repository secret, verifies that signature against the
public key actually compiled into the firmware, and publishes the result to
the `gh-pages` branch. `tools/publish_ota.sh` does the same thing from a
workstation, for when CI is not the right path — it looks for the private
key at `~/caltrain-ota-signing-key.pem` (override with
`OTA_SIGNING_KEY_FILE`) and refuses to run without it, for the same reason.

## Host tests

Pure logic — station lookup, direction, schedule queries, JSON parsing, the
board merge, DST — builds and runs on any desktop with no board and no network:

```bash
cd test && make
```

Hardware, WiFi and NVS live behind `#ifdef ARDUINO` seams so the logic stays
testable. Tests are deliberately mutation-checked: several were found to pass
against a deliberately broken implementation and were retargeted until they
failed.

## Layout

```
src/
  stations.h        GENERATED — 30 stations, ordered north to south
  timetable_data.h  GENERATED — 257 trips, 5,414 calls, ~24 KB
  route.*           station lookup, direction, platform stop_id      [pure]
  timetable.*       service patterns, schedule queries               [pure]
  siri_parse.*      511 JSON -> departures                           [pure]
  board_model.*     merge live over schedule, express filter, colour [pure]
  service_day.*     Pacific time, DST, the 03:00 service rollover    [pure]
  urgency.h         the red/yellow/green rule and its bounds         [pure]
  config.*          NVS settings; validation half is pure
  siri_client.*     HTTPS GET                                        [device]
  net_task.*        the fetch, pinned to core 0                      [device]
  ota_manifest.*    parse and version-check the OTA manifest         [pure]
  ota_verify.*      manifest signature check                         [pure]
  ota_health.*      trial-boot health gate and rollback              [pure]
  ota_task.*        the daily OTA check and install                  [device]
  ota_pubkey.h      the public key a device trusts
  csrf_check.h, html_escape.h, wifi_pass_policy.h
                    portal input handling                            [pure]
  layout.h          per-board positions, fonts, splash text          [pure]
  station_label.h   short station names for the 2.8" header         [pure]
  display_hw.*      panel init, backlight PWM, touch                 [device]
  render.*          the screens                                      [device]
  portal.*          SoftAP captive portal                            [device]
  main.cpp          poll loop and 1 Hz tick
tools/
  gen_stations.py   GTFS -> src/stations.h
  gen_timetable.py  GTFS -> src/timetable_data.h
  board_dump.cpp    board model -> JSON, for the screenshot (host build)
  gen_screenshot.py that JSON -> docs/images/*.png
  probe_511.py      measures the live API; run before trusting assumptions
  publish_ota.sh    sign and publish a release from a workstation
  gen_ota_test_vectors.sh, extract_pubkey_pem.py
                    OTA signing test fixtures and key tooling
  package.sh        tarball for transfer to the build machine
  mac_flash.sh      build and flash from macOS
scripts/
  apply-repo-settings.sh  branch protection and required checks
bringup/            standalone panel smoke test
third_party/        ArduinoJson 7.1.0, vendored
```

ArduinoJson is vendored rather than listed in `lib_deps` because plain `g++`
cannot reach into `.pio/libdeps`. Vendoring is what makes the host tests
exercise the same parser the device runs rather than a lookalike.

## Footprint

| | CrowPanel 3.5" | ES3C28P 2.8" |
| :--- | :--- | :--- |
| Flash | 1.10 MB of 3.19 MB app slot | 1.06 MB of 6.25 MB app slot |
| RAM | 61.9 KB of 320.0 KB | 60.7 KB of 320.0 KB |

Two app slots on both — plus 1.5 MB filesystem on the CrowPanel's 8 MB layout,
6.25 MB slots on the ES3C28P's 16 MB layout — so a signed-OTA path stays open.

---

## Regenerating the screenshots

Every image at the top of this page is generated, not mocked up:

```bash
g++ -std=c++17 -Isrc -Ithird_party tools/board_dump.cpp \
    src/board_model.cpp src/timetable.cpp src/route.cpp src/siri_parse.cpp \
    -o /tmp/board_dump
TZ=America/Los_Angeles /tmp/board_dump "San Francisco" "San Jose Diridon" \
    test/fixtures/stopmonitoring_70012.json > /tmp/board.json

python3 tools/gen_screenshot.py < /tmp/board.json                  # 3.5" board
python3 tools/gen_screenshot.py --board es3c28p < /tmp/board.json  # 2.8" board
python3 tools/gen_screenshot.py --splash                           # 3.5" boot screen
python3 tools/gen_screenshot.py --splash --board es3c28p           # 2.8" boot screen
python3 tools/gen_screenshot.py --legend                           # the urgency swatch
```

Each writes into `docs/images/` along with a 2x copy. Add `--out DIR` to write
a preview somewhere else instead, for trying out a layout change.

The files are one image pixel per panel pixel. The README shows them at
**true relative size** instead: the 3.5" panel packs 165 px/in and the 2.8"
only 143, so the 2.8" images are displayed 1.154x wider than their pixel count
(`width="369"` against the 3.5"'s 480). Keep those `width`/`height` attributes
if either image is replaced.

`board_dump.cpp` is a printf around `buildBoard()` — it links the same modules
the firmware does rather than reimplementing them, so the numbers are real. The
geometry in `gen_screenshot.py` is copied from `src/layout.h` and the RGB565
colours from `render.cpp`. Text is drawn with TFT_eSPI's own glyphs, decoded from
the font files a PlatformIO build fetches into `.pio/libdeps`, so after any
`pio run` the image is the panel's pixels. Without a build it falls back to
DejaVu Sans, which is noticeably wider, and prints a note saying so. **If
`layout.h` changes, `gen_screenshot.py` has to be changed with it** — nothing
keeps the two in step.

The splash renderer reads the attribution strings out of `src/layout.h` rather
than restating them, and warns if a line overruns the panel. Two copies of a
legal notice drift apart, and the copy in the picture is the one people quote.

## Inherited gotchas

Carried over from bringing this same board up for `obd-gauge-cluster`. Each cost
real debugging time once already.

- **Do not define `TFT_WIDTH`/`TFT_HEIGHT`.** `ILI9488_Defines.h` sets them to
  portrait 320×480; the app uses its own per-board `SCREEN_W`/`SCREEN_H` from
  `src/layout.h` with a landscape rotation.
- **Pin the platform.** A bare `espressif32` resolves to whatever is installed,
  and a global pioarduino install silently swaps the Arduino core 2.x → 3.x.
  That difference is invisible until something fails to compile — as
  `setCACertBundle()` did here, which takes one argument on core 2.x and two on
  core 3.x.
- **Pin `lib_deps` exactly, no carets.** Dependabot has no PlatformIO ecosystem,
  so nothing proposes bumps; a caret range means the same commit can build a
  different binary later.
- **Keep the HTTP read buffer file-scope `static`.** A 4 KB local plus a TLS
  handshake overflows the ~8 KB `loopTask` stack — that produced a panic-reboot
  with a blank screen once already.
- **`setCACertBundle()` is not automatic on core 2.x.** The pointer must be
  passed explicitly or validation silently does nothing.
- **Use signed `millis()` deltas** — `(int32_t)(now - then)` — or the timers
  stall at the 49-day rollover.
- **Negative `HTTPClient` codes are not HTTP statuses.** `-1` means the
  connection never opened.
- **macOS: prefer the `cu.*` port.** Opening `tty.*` blocks on carrier detect.

### Traps found building this one

Distinct from the list above, which was carried over. Each of these shipped or
nearly shipped.

- **`configTime(0, 0, ...)` silently sets the timezone**, clobbering Pacific and
  making the whole screen read UTC. Use `configTzTime(SERVICE_DAY_TZ, ...)`.
  This shipped once.
- **You cannot erase text by drawing it in the background colour.** TFT_eSPI's
  padding fill is guarded by
  `if ((padX > cwidth) && (textcolor != textbgcolor))`, so a
  background-on-background draw of an empty string does nothing at all, and the
  old pixels survive until the next full repaint. This shipped once, as a
  staleness note frozen at "live data 240s old" through hours of healthy
  fetches. Clear with `fillRect`. Every other field on the board happens to
  erase correctly only because its colour differs from the background.
- **Mid-grey on black fails off-axis.** `COL_DIM` was `0x8410`, a true 50% grey,
  and became unreadable a few tens of degrees off centre — which is how a desk
  sign is actually seen. It is now `0xC618`.
- **`doFetch()` blocks the tick loop** for the length of the HTTPS round trip,
  up to ~19 s measured. The clock and countdowns freeze for that window on every
  poll. The fetch now runs pinned to core 0, which is what makes that tolerable.
- **`intelhex` is easy to have globally and not in a fresh venv.** Its absence
  makes `esptool` fail at flash time, not at build time, so a green build on the
  development machine proved nothing about the machine with the board attached.
- **An ESP32-S3's `Serial` goes nowhere by default.** On Arduino core 2.0.17
  `ARDUINO_USB_CDC_ON_BOOT` defaults to 0, which sends `Serial` to UART0 on
  GPIO43/44 — the ES3C28P's USB-C port shows nothing at all. `caltrain_es3c28p`
  sets it to 1.
- **The ES3C28P's flash is quad and its PSRAM octal: `memory_type = qio_opi`.**
  Guides for similar S3 boards often say `opi_opi`, which is for modules whose
  flash is octal too. `esptool.py flash_id` reports "Flash type set in eFuse:
  quad".
- **GPIO45 and GPIO46 are strapping pins, and are not a problem here.** The
  ES3C28P puts the backlight (45) and panel DC (46) on them, but this chip's
  eFuse fixes the flash voltage ("Flash voltage set by eFuse to 3.3V") and the
  board boots and enters download mode normally.
- **TFT_eSPI takes the backlight pin back from the PWM.** With `TFT_BL` and
  `TFT_BACKLIGHT_ON` defined, `TFT_eSPI::init()` calls `pinMode()` on the pin,
  which turns an LEDC-driven pin back into a plain GPIO — so `ledcWrite()` did
  nothing and night dimming never worked, on either board. Found by reading
  GPIO45's output-select register on the ES3C28P: 256 (plain GPIO) before, 73
  (LEDC channel 0) after. The backlight is now a separate `BACKLIGHT_PIN` flag
  that TFT_eSPI never sees.
