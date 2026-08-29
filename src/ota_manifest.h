// ota_manifest.h — the release manifest, parsed.
//
// manifest.txt has one line per build environment:
//
//   <env> <version> <sha256-hex> <size-bytes> <filename>
//   caltrain v1.2.0 2d71...4881 1140256 caltrain.bin
//
// Pure logic, host-tested. Nothing here touches the network: the caller has
// already fetched the bytes and verified their signature.
#pragma once
#include <stdint.h>

// One environment's release. Sizes are fixed so this can cross a task boundary
// by value, exactly like FetchResult in net_task.
struct OtaRelease {
  char     version[32];  // e.g. "v1.2.0" or "dev-abc1234"
  char     sha256[65];   // 64 lowercase hex chars + NUL
  uint32_t size;         // bytes, must be non-zero
  char     file[64];     // e.g. "caltrain.bin", relative to OTA_BASE_URL
};

// Find the line for `env` and fill `out`. Returns false if the environment is
// absent or its line is malformed in any way. `out` is untouched on failure.
//
// Matching is exact: "caltrain" must not match "caltrain_v20", or a v2.2 board
// would be handed the v2.0 image.
bool otaManifestFind(const char* manifest, const char* env, OtaRelease* out);

// True if `rel` should be installed over `currentVersion`.
//
// Deliberately string inequality, not ordering: re-publishing an older tag is
// how a bad-but-healthy build gets rolled back remotely.
//
// A "dev-"-prefixed currentVersion NEVER updates, checked explicitly rather
// than relying on inequality alone. FW_VERSION is "dev-local" on every
// USB-flashed working-tree build (fw_version.h) — never empty — so an
// empty-string check here would be unreachable and prove nothing; string
// inequality alone would do the OPPOSITE of what is wanted, since "dev-local"
// never equals a published release tag and would therefore always look due
// for an update. Without this explicit guard, the release channel going live
// would mean every developer build on the bench reflashes itself to the
// published release ~75s after boot, mid bring-up or debugging.
bool otaUpdateApplies(const OtaRelease& rel, const char* currentVersion);

// How long a version stays suppressed after the health gate rejects it and
// the device rolls back, before ota_task offers it again — see C2 in the
// whole-branch review and the design's promise (ota_health.h) that a
// rejected release "will simply be offered again the next day."
constexpr int64_t OTA_SUPPRESS_WINDOW_SEC = 24LL * 60 * 60;

// Below this, an int64_t epoch is not a real wall-clock reading: it is either
// the zero a chip powers up at, or some other pre-2020 value from before this
// project existed. Mirrors main.cpp's local CLOCK_IS_SET_EPOCH sentinel
// (same magnitude, same reasoning); duplicated rather than shared because
// that one lives in main.cpp's anonymous namespace and this module must stay
// free of any Arduino/main.cpp dependency to remain host-testable.
constexpr int64_t OTA_EPOCH_SANE_AFTER = 1600000000;

// True if `candidateVersion` should be skipped this cycle because it is
// exactly the version most recently rolled back by the health gate, and
// fewer than OTA_SUPPRESS_WINDOW_SEC have passed since the rejection.
//
// `suppressedVersion` is empty when nothing is currently suppressed — no
// rollback on record, or the record was cleared because that version was
// later accepted. A rejection suppresses only the ONE version that failed,
// never the whole channel: a different candidateVersion is never skipped by
// this check. `rejectedEpoch` is when the rejection was recorded (wall
// clock); `nowEpoch` is the current wall clock.
//
// Fails CLOSED on an unreliable clock: if either epoch is not sane (see
// OTA_EPOCH_SANE_AFTER above) or the clock has stepped backwards since the
// rejection, elapsed time cannot be proven to be >= the window, so the
// version stays suppressed rather than risk retrying it within seconds of a
// clock that has not (yet) synced — exactly the runaway-retry failure this
// function exists to close.
bool otaVersionSuppressed(const char* suppressedVersion, int64_t rejectedEpoch,
                          int64_t nowEpoch, const char* candidateVersion);
