# Third-party notices

This project's own code is **MIT** — see [`LICENSE`](LICENSE). Transit data is a
separate matter and is covered in [`ATTRIBUTION.md`](ATTRIBUTION.md); nothing in
this file relicenses it.

**This repository distributes no compiled binary.** There is no release asset, no
OTA image, and no `gh-pages` deployment. Everything below is therefore a
*source* obligation only — the binary-redistribution clauses that several of
these licences carry are not triggered, because no binary is redistributed.

## The LGPL question, decided in advance

The Arduino core for ESP32 is **LGPL-2.1-or-later** and is statically linked
into any firmware built from this repository. Source-only distribution carries no
obligation from that. **Attaching a `firmware.bin` to a GitHub release does**, and
the obligation is created at *build* time rather than at publish time — the same
reasoning that governs the `-ffile-prefix-map` flags in `platformio.ini`. So the
position is recorded now, before there is a release to argue about.

**Decision: if a binary is ever published from this repository, the LGPL-2.1
§6 relinking obligation is met by the published-source route, not by shipping
object files.**

LGPL-2.1 §6 requires that a recipient be able to modify the library and relink it
into a working executable. The licence contemplates shipping linkable object
files. This project can satisfy it more completely than that, and already does
everything the route requires:

| §6 requirement | How it is already met |
| :--- | :--- |
| The recipient can modify the library | The Arduino core is public and unmodified here |
| …and relink it into a working executable | The **entire** work that uses the library is published as source under MIT — not just linkable objects |
| …with the same versions that were linked | `platformio.ini` pins the platform and every library **exactly**, with no caret ranges |
| …using a documented, repeatable procedure | `pio run -e caltrain` — one command, no private toolchain |

**Two conditions attach to any such release**, and neither is optional:

1. **This file ships alongside the binary**, as a release asset in its own right.
   Several of the licences above require their notices to be reproduced *in
   binary redistributions specifically*; a notices file that exists only in the
   source tree does not satisfy that for someone who downloads only the `.bin`.
2. **The release records the exact resolved versions** of the Arduino core and
   ESP-IDF that the pinned platform produced, since the table below defers to
   the platform pin rather than naming them.

If neither condition can be met, the answer is to keep distributing source only,
which remains a complete and honest option — `pio run` takes about nine seconds.

## Vendored — shipped inside this repository

| Component | Version | Licence | Text | Source |
| :--- | :--- | :--- | :--- | :--- |
| ArduinoJson | 7.1.0 | MIT | [`third_party/ArduinoJson-LICENSE.txt`](third_party/ArduinoJson-LICENSE.txt) | <https://arduinojson.org> |

`third_party/ArduinoJson.h` is the upstream single-header amalgamation, copied in
unmodified. Copyright © 2014-2024 Benoit BLANCHON.

It is vendored rather than declared in `lib_deps` on purpose: the parser is used
by both the firmware **and** the host tests, and the host tests build with a
plain `g++` that has no PlatformIO library resolver. One file in the tree is what
lets `cd test && make` work with no toolchain beyond a compiler.

## Pinned dependencies — fetched at build time, not redistributed

| Component | Version | Licence | Source |
| :--- | :--- | :--- | :--- |
| TFT_eSPI | 2.5.43 | FreeBSD/BSD-2 (Bodmer), with retained Adafruit MIT and Adafruit_GFX BSD notices | <https://github.com/Bodmer/TFT_eSPI> |
| platform-espressif32 | 7.0.1 | Apache-2.0 | <https://github.com/platformio/platform-espressif32> |
| Arduino core for ESP32 | as resolved by the pinned platform | **LGPL-2.1-or-later** | <https://github.com/espressif/arduino-esp32> |
| ESP-IDF | as resolved by the pinned platform | Apache-2.0 | <https://github.com/espressif/esp-idf> |

PlatformIO downloads these into `.pio/`, which is gitignored. Their licence texts
ship with the packages it fetches.

Versions are pinned **exactly**, without caret ranges. Dependabot has no
PlatformIO ecosystem, so nothing here proposes bumps automatically; a range would
mean the same commit could produce a different binary later.
