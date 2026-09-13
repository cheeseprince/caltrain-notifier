// Host tests for manifest parsing: which release line this build is offered,
// and whether it constitutes an update.
//
// This runs before anything is downloaded, so a parser that accepts a
// malformed or wrong-environment line is how a v2.0 panel ends up flashing a
// v2.2 binary. Every rejection case below is a real way that can happen.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include "ota_manifest.h"

static int failures = 0;
static void check(bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); failures++; } }

static const char* kSha22 =
    "2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881";
static const char* kSha20 =
    "9f2c8ab1d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f";

static const char kGood[] =
    "caltrain v1.2.0 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881 1140256 caltrain.bin\n"
    "caltrain_v20 v1.2.0 9f2c8ab1d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f 1140256 caltrain_v20.bin\n";

int main() {
  // --- Selecting the right line ---------------------------------------------
  {
    OtaRelease r{};
    check(otaManifestFind(kGood, "caltrain", &r), "finds the v2.2 line");
    check(strcmp(r.version, "v1.2.0") == 0, "v2.2 version");
    check(strcmp(r.sha256, kSha22) == 0, "v2.2 sha");
    check(r.size == 1140256u, "v2.2 size");
    check(strcmp(r.file, "caltrain.bin") == 0, "v2.2 file");

    OtaRelease r20{};
    check(otaManifestFind(kGood, "caltrain_v20", &r20), "finds the v2.0 line");
    check(strcmp(r20.file, "caltrain_v20.bin") == 0, "v2.0 file");
    check(strcmp(r20.sha256, kSha20) == 0, "v2.0 sha");
  }

  // A prefix must not match. "caltrain" is a prefix of "caltrain_v20", so a
  // sloppy comparison hands a v2.2 board the v2.0 image or vice versa.
  {
    const char only20[] =
        "caltrain_v20 v1.2.0 9f2c8ab1d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f 1140256 caltrain_v20.bin\n";
    OtaRelease r{};
    check(!otaManifestFind(only20, "caltrain", &r), "prefix does not match a longer env");
  }

  // caltrain_v20 deliberately FIRST. With the `line[envLen] == ' '` guard
  // removed, the "caltrain" lookup prefix-matches this v2.0 line, misparses it
  // and returns false — so this assertion is what actually pins the guard.
  {
    const char v20First[] =
        "caltrain_v20 v1.2.0 9f2c8ab1d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f 1140256 caltrain_v20.bin\n"
        "caltrain v1.2.0 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881 1140256 caltrain.bin\n";
    OtaRelease r{};
    check(otaManifestFind(v20First, "caltrain", &r),
          "finds caltrain even when caltrain_v20 is listed first");
    check(strcmp(r.file, "caltrain.bin") == 0,
          "a v2.2 board is never handed the v2.0 image");
  }

  // A third environment: caltrain_es3c28p, the QDtech 2.8" ESP32-S3 board.
  // "caltrain" is a prefix of it just as it is of caltrain_v20, and this board
  // is a different chip entirely — handing it a CrowPanel image, or the
  // reverse, flashes firmware that cannot run. Every listing order of the
  // three lines is tried, because the guard is only exercised when a longer
  // name precedes the shorter one it starts with.
  {
    const char* line[3] = {
        "caltrain v1.2.0 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881 1140256 caltrain.bin\n",
        "caltrain_v20 v1.2.0 9f2c8ab1d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f 1140256 caltrain_v20.bin\n",
        "caltrain_es3c28p v1.2.0 5b0e3c7a9d1f24680ace13579bdf02468ace13579bdf02468ace13579bdf0246 1098393 caltrain_es3c28p.bin\n",
    };
    const char* env[3] = {"caltrain", "caltrain_v20", "caltrain_es3c28p"};
    const char* file[3] = {"caltrain.bin", "caltrain_v20.bin", "caltrain_es3c28p.bin"};

    int order[3] = {0, 1, 2};
    int orders = 0;
    do {
      std::string manifest;
      for (int i : order) manifest += line[i];
      for (int e = 0; e < 3; e++) {
        OtaRelease r{};
        const bool found = otaManifestFind(manifest.c_str(), env[e], &r);
        char what[160];
        snprintf(what, sizeof what, "%s finds only its own line (order %d%d%d)", env[e],
                 order[0], order[1], order[2]);
        check(found && strcmp(r.file, file[e]) == 0, what);
      }
      orders++;
    } while (std::next_permutation(order, order + 3));
    check(orders == 6, "all six listing orders were exercised");

    // A manifest carrying only one board's line offers nothing to the others.
    OtaRelease r{};
    check(!otaManifestFind(line[2], "caltrain", &r),
          "caltrain does not prefix-match a caltrain_es3c28p line");
    check(!otaManifestFind(line[2], "caltrain_v20", &r),
          "caltrain_v20 does not match a caltrain_es3c28p line");
    check(!otaManifestFind(line[0], "caltrain_es3c28p", &r),
          "caltrain_es3c28p does not match a caltrain line");
  }

  // --- Malformed input ------------------------------------------------------
  {
    OtaRelease r{};
    check(!otaManifestFind("", "caltrain", &r), "empty manifest");
    check(!otaManifestFind(kGood, "nosuchenv", &r), "unknown environment");
    check(!otaManifestFind("caltrain v1.2.0\n", "caltrain", &r), "too few fields");
    check(!otaManifestFind("caltrain v1.2.0 abc 1140256 caltrain.bin\n", "caltrain", &r),
          "sha must be 64 hex chars");
    check(!otaManifestFind(
              "caltrain v1.2.0 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a488Z 1140256 caltrain.bin\n",
              "caltrain", &r),
          "sha must be hex, not merely 64 long");
    check(!otaManifestFind(
              "caltrain v1.2.0 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881 0 caltrain.bin\n",
              "caltrain", &r),
          "zero size rejected");
    // A truncated download is the common real failure: the last line has no \n.
    check(!otaManifestFind(
              "caltrain v1.2.0 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db0225871792",
              "caltrain", &r),
          "truncated line rejected");
    // Trailing content after the filename. The last field must terminate the
    // line, or a manifest with junk appended reads as well-formed.
    check(!otaManifestFind(
              "caltrain v1.2.0 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881 1140256 caltrain.bin EXTRAJUNK\n",
              "caltrain", &r),
          "trailing content after the filename is rejected");
  }

  // Over-long fields must be rejected, never silently truncated: a truncated
  // filename fetches the wrong object.
  {
    char big[512];
    snprintf(big, sizeof big,
             "caltrain %.*s 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881 1140256 caltrain.bin\n",
             80, "vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv");
    OtaRelease r{};
    check(!otaManifestFind(big, "caltrain", &r), "over-long version rejected");
  }

  // Exactly sizeof(OtaRelease::version) == 32 chars. This is the only width at
  // which `n >= cap` and `n > cap` differ: the mutation accepts it and then
  // writes 33 bytes (32 + NUL) into a 32-byte field.
  {
    const char exact32[] =
        "caltrain 12345678901234567890123456789012 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881 1140256 caltrain.bin\n";
    OtaRelease r{};
    check(!otaManifestFind(exact32, "caltrain", &r),
          "a field of exactly the buffer size is rejected, not truncated");
  }

  // --- The update decision --------------------------------------------------
  {
    OtaRelease r{};
    otaManifestFind(kGood, "caltrain", &r);
    check(!otaUpdateApplies(r, "v1.2.0"), "identical version is not an update");
    // A real (non-dev) version is the ordinary case: still updates on any
    // difference, including an older tag — that is the deliberate remote
    // rollback lever this design relies on, not something I1's dev-build fix
    // is allowed to touch.
    check(otaUpdateApplies(r, "v1.1.0"), "a real version updates on a newer release");
    check(otaUpdateApplies(r, "v9.9.9"), "a real version updates on an older release too");
    check(!otaUpdateApplies(r, ""), "unknown current version never updates");

    // I1, whole-branch review: a "dev-" build must NEVER be superseded by a
    // published release, checked explicitly rather than relying on string
    // inequality. Before this fix otaUpdateApplies() asserted the opposite —
    // "a dev build is superseded" — which is exactly backwards: inequality
    // alone means "dev-local" (never equal to a real tag) always looks due
    // for an update, so every USB-flashed working-tree build would silently
    // reflash itself to the published release ~75s after boot.
    check(!otaUpdateApplies(r, "dev-local"), "a dev build is never superseded");
    check(!otaUpdateApplies(r, "dev-abc1234"), "any dev- build is never superseded");
    // Only the "dev-" PREFIX is exempt — a real tag that merely contains
    // "dev" elsewhere in the string must still update normally, or the guard
    // is broader than intended.
    check(otaUpdateApplies(r, "develop-1.0"), "a version merely containing dev still updates");

    // The mirror image of the guard above: a "dev-" version must never be
    // OFFERED to a real release either. release.yml's workflow_dispatch path
    // stamps and signs a "dev-<sha>" build exactly like a tag; if that ever
    // reaches the channel, string inequality alone would have every v-tagged
    // unit in the field install it -- and, per the guard above, a unit running
    // a dev- build then refuses every future update, so the whole fleet is
    // stranded until each unit is USB-reflashed. Refusing an offered dev-
    // version in the firmware closes that regardless of what the workflow does.
    OtaRelease dev = r;
    strcpy(dev.version, "dev-abc1234");
    check(!otaUpdateApplies(dev, "v1.2.0"), "a dev- release is never offered to a real build");
    check(!otaUpdateApplies(dev, "v1.1.0"), "a dev- release is never offered, even as a 'newer' one");
    check(!otaUpdateApplies(dev, "dev-local"), "a dev- release is never offered to a dev build either");
    // Same prefix rule as the running-version guard: only "dev-" is refused.
    OtaRelease devish = r;
    strcpy(devish.version, "develop-1.0");
    check(otaUpdateApplies(devish, "v1.2.0"), "an offered version merely containing dev still applies");
  }

  // --- Suppressing a just-rejected version (C2) ------------------------------
  // otaVersionSuppressed() is the pure decision behind "a rejected release is
  // offered again the next day" (ota_health.h): ota_task consults it before
  // installing a candidate, using the version/epoch a prior rejection left in
  // NVS (read back by otaHealthBegin(), written by
  // otaHealthRecordInstalling() — both in ota_health.cpp, untestable on the
  // host). This function is the host-testable core of that decision.
  {
    constexpr int64_t DAY = 24LL * 60 * 60;
    constexpr int64_t REJECTED_AT = 1700000000;  // an arbitrary, clock-sane epoch

    check(!otaVersionSuppressed("", REJECTED_AT, REJECTED_AT + 10, "v1.3.0"),
          "nothing suppressed when the record is empty");
    check(!otaVersionSuppressed("v1.3.0", REJECTED_AT, REJECTED_AT + 10, "v1.4.0"),
          "a different candidate version is never suppressed by someone else's rejection");
    check(otaVersionSuppressed("v1.3.0", REJECTED_AT, REJECTED_AT + 10, "v1.3.0"),
          "the exact rejected version is suppressed immediately after rejection");

    // The 24h boundary, both sides, mirroring the otaHealthExpired boundary
    // tests in test_ota_health.cpp.
    check(otaVersionSuppressed("v1.3.0", REJECTED_AT, REJECTED_AT + DAY - 1, "v1.3.0"),
          "still suppressed one second before the 24h window elapses");
    check(!otaVersionSuppressed("v1.3.0", REJECTED_AT, REJECTED_AT + DAY, "v1.3.0"),
          "no longer suppressed exactly at the 24h boundary");
    check(!otaVersionSuppressed("v1.3.0", REJECTED_AT, REJECTED_AT + DAY + 1000, "v1.3.0"),
          "no longer suppressed well past the 24h window");

    // The clock-unset case: time() reads 0 (chip just booted, NTP has not
    // answered yet) or some other pre-2020 value. Neither epoch can be
    // trusted, so the function fails CLOSED — stays suppressed — rather than
    // risk retrying a just-rejected build within seconds of a clock that has
    // not synced.
    check(otaVersionSuppressed("v1.3.0", 0, REJECTED_AT + DAY + 1000, "v1.3.0"),
          "clock-unset rejectedEpoch (0) fails closed: still suppressed");
    check(otaVersionSuppressed("v1.3.0", REJECTED_AT, 0, "v1.3.0"),
          "clock-unset nowEpoch (0) fails closed: still suppressed");
    check(otaVersionSuppressed("v1.3.0", 1, 100000, "v1.3.0"),
          "a pre-2020 rejectedEpoch fails closed even with a plausible nowEpoch");

    // A clock that stepped backwards since the rejection (NTP re-sync, say)
    // must not be trusted to prove 24h have passed either.
    check(otaVersionSuppressed("v1.3.0", REJECTED_AT, REJECTED_AT - 5, "v1.3.0"),
          "a clock that stepped backwards fails closed: still suppressed");
  }

  printf(failures ? "test_ota_manifest FAILED\n" : "test_ota_manifest passed\n");
  return failures != 0;
}
