#ifdef ARDUINO
#include "ota_task.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include <time.h>

#include "fw_version.h"
#include "ota_health.h"
#include "ota_manifest.h"
#include "ota_pubkey.h"
#include "ota_verify.h"

// OTA_ENV and OTA_BASE_URL are both build_flags in platformio.ini (see the
// [env:caltrain] / [env:caltrain_v20] sections). OTA_ENV is set once per
// environment — that is what stops a v2.0 board being handed a v2.2 image.
// OTA_BASE_URL is set once, shared, in the [base] section (M2, whole-branch
// review: it used to be duplicated per-env, which risked a channel move
// landing on one environment but not the other) — either way it is a
// build-time constant, not something fetched from anywhere that could be
// spoofed.

namespace ota_task {
namespace {

// Measured frame sizes, not guessed: runAttempt() itself takes 2128 B of
// stack (mbedtls_sha256_context, the streaming buf[CHUNK], WiFiClientSecure,
// HTTPClient, and the manifest/signature locals all live in that one frame),
// and fetchCapped() — called from inside it, so the two frames are live at
// once at the deepest point — takes another 624 B. That is 2752 B together,
// against net_task's siriFetch(), which needs only 576 B of its own frame at
// its deepest point under the same TLS handshake. This task is therefore
// roughly 2.2 KB deeper than net_task on the SAME 12 KB figure this constant
// used to just copy over — an amount that matters right where the TLS
// handshake itself is already the deepest, least-visible part of the whole
// call stack (inside mbedtls, not under this file's control). 12288 was
// "comfortable in practice" reasoning inherited from a shallower caller; it
// is replaced here with the measurement above. 16384 restores a comparable
// margin over the deeper combined frame. RAM is at 19.3% overall, so there is
// ample room to spend the extra 4 KB. logStackHeadroom() below still logs the
// real high-water mark on every run — this number is a starting point to be
// checked against hardware, not a substitute for it.
constexpr uint32_t TASK_STACK = 16384;

// Core 0, same as net_task and for the same reason: core 1's loop() keeps
// painting the "Updating firmware" screen (render::updating(), Task 7)
// throughout, using the done/total this task publishes below.
constexpr BaseType_t TASK_CORE = 0;

// manifest.txt is one short line per build environment. 4 KB is far more
// than that could ever need. The point of capping it is what happens at the
// edge: fetchCapped() below stops reading at MANIFEST_CAP and reports
// failure rather than growing a buffer to fit whatever a hostile or
// misconfigured server sends — an unbounded getString() against a server
// that never stops sending is an out-of-memory reboot, not a parse error.
constexpr size_t MANIFEST_CAP = 4096;

// FILE-SCOPE, not a local — same reasoning as g_body in siri_client.cpp. A
// buffer this size on the stack, sitting on top of a TLS handshake's own
// usage, is how a panic-reboot with nothing on screen happens. Static
// storage costs the same RAM and cannot blow the stack.
uint8_t g_manifest[MANIFEST_CAP + 1];

// A DER ECDSA-P256 signature is 70-72 bytes. 128 bytes is room without being
// a buffer to think about, and — this is the part that matters — anything
// the server sends past it is a refusal inside fetchCapped(), never a silent
// truncation that would go on to be "verified" against a partial signature.
constexpr size_t SIG_CAP = 128;

// The image is streamed in chunks this size: each chunk is written to the
// flash slot and folded into the running SHA-256 before the next one is
// requested, so at most one chunk plus the two fixed buffers above is ever
// live at once.
constexpr size_t CHUNK = 1024;

// How long a single HTTP round trip (manifest, signature, or the connect
// phase of the image fetch) is allowed to take before this task gives up on
// it. Generous, because cellular/hotspot backhaul at a desk is not this
// project's fast path.
constexpr uint32_t IO_DEADLINE_MS = 20000;

SemaphoreHandle_t g_lock = nullptr;  // guards g_busy and g_progress below
SemaphoreHandle_t g_wake = nullptr;  // signals the task that start() was called
bool              g_busy = false;
Progress          g_progress{};
TaskHandle_t      g_task = nullptr;

// Scoped mutex, identical shape to net_task.cpp's Lock.
struct Lock {
  Lock() { xSemaphoreTake(g_lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_lock); }
};

void setPhase(OtaPhase phase) {
  Lock lock;
  g_progress.phase = phase;
}

void setProgressBytes(uint32_t done, uint32_t total) {
  Lock lock;
  g_progress.done = done;
  g_progress.total = total;
}

void setVersion(const char* version) {
  Lock lock;
  strncpy(g_progress.version, version, sizeof(g_progress.version) - 1);
  g_progress.version[sizeof(g_progress.version) - 1] = '\0';
}

// Ends the attempt with a failure. `error` MUST be a string literal — never
// a local buffer or anything computed at runtime — so a Progress copy taken
// after this call stays valid indefinitely. See the THREAD SAFETY note in
// ota_task.h.
void fail(const char* error) {
  Lock lock;
  g_progress.phase = OTA_FAILED;
  g_progress.error = error;
  g_progress.busy = false;
  g_busy = false;
}

// Ends the attempt because the running version already matches the release
// — not a failure. This is the ONLY way a successful attempt reaches this
// function: an install that actually happens ends in ESP.restart() instead,
// per the contract in ota_task.h.
void finishNoUpdate() {
  Lock lock;
  g_progress.phase = OTA_DONE;
  g_progress.error = nullptr;
  g_progress.busy = false;
  g_busy = false;
}

// Logged at the end of every attempt, success or failure, the way
// net_task::stackHeadroom() reports its own high-water mark — a measurement
// this task's stack sizing above can be checked against, rather than a
// number trusted on paper.
void logStackHeadroom() {
  const uint32_t bytesLeft =
      (uint32_t)uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
  Serial.printf("[ota] stack headroom: %u bytes\n", (unsigned)bytesLeft);
}

// GET `path` (appended to OTA_BASE_URL) into `buf`, capped at `cap` bytes.
// Returns the number of bytes read on success, or -1 on ANY failure:
// connection failure, a non-200 status, or a body that reaches `cap` without
// the response having ended — which is treated as "too big" rather than
// risk verifying or parsing a truncated read. `buf` must have room for at
// least `cap` bytes; the caller decides whether to NUL-terminate what comes
// back (the manifest fetch does, for otaManifestFind; the signature fetch
// does not, since it is binary DER).
//
// Shared by the manifest and signature fetches (steps 1 and 2 of the
// ordering documented in runAttempt() below) because both need exactly this
// same "capped GET, refuse rather than truncate" shape.
int fetchCapped(const char* path, uint8_t* buf, size_t cap) {
  WiFiClientSecure net;
  // Deliberately setInsecure(), not certificate pinning. See ota_verify.h:
  // authenticity comes from the manifest signature checked in runAttempt(),
  // integrity from the SHA-256 checked before the image slot is activated.
  // TLS here only has to keep the transfer private, which setInsecure()
  // still does — it skips validating who is on the other end, not the
  // encryption itself.
  net.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);

  char url[256];
  // snprintf's return is the length it WOULD have written; comparing that
  // against the buffer size is what catches a silent truncation. Without
  // this check, a truncated URL is fetched as-is — whatever object happens
  // to live at that shorter path — rather than refused. Failing closed here
  // costs nothing: OTA_BASE_URL and every `path` this is called with are
  // build-time constants or manifest-derived filenames capped well under
  // this budget in ordinary operation.
  const int urlLen = snprintf(url, sizeof(url), "%s%s", OTA_BASE_URL, path);
  if (urlLen < 0 || (size_t)urlLen >= sizeof(url)) return -1;
  if (!http.begin(net, url)) return -1;

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return -1;
  }

  // Content-Length when the server sent one; -1 otherwise, in which case the
  // loop below falls back to the connection-closed / deadline exits, same as
  // siri_client.cpp's fetch loop.
  const int declared = http.getSize();

  WiFiClient* stream = http.getStreamPtr();
  size_t n = 0;
  const uint32_t deadline = millis() + IO_DEADLINE_MS;
  while (n < cap) {
    if (declared > 0 && n >= (size_t)declared) break;
    const size_t avail = stream->available();
    if (avail) {
      size_t room = cap - n;
      if (declared > 0 && (size_t)declared - n < room) room = (size_t)declared - n;
      const size_t got = stream->readBytes(buf + n, min(avail, room));
      if (got == 0) break;
      n += got;
      continue;
    }
    if (!http.connected()) break;
    if ((int32_t)(millis() - deadline) >= 0) break;
    delay(5);
  }
  http.end();

  // Reaching the cap is only a refusal when we cannot prove the body is
  // complete. With a Content-Length we can: n == declared means the server
  // declared exactly this many bytes and every one of them was read, so a
  // body that happens to be exactly `cap` bytes (a manifest that grows to
  // fill it, say, as board revisions are added) is genuinely done, not
  // truncated. Without a Content-Length (declared <= 0), a body that fills
  // the buffer is indistinguishable from one truncated at it, so that case
  // stays a refusal — same as before.
  if (n >= cap && !(declared > 0 && n == (size_t)declared)) return -1;
  return (int)n;
}

// Lowercase-hex-encodes a 32-byte SHA-256 digest into `out`, which must hold
// at least 65 bytes (64 hex chars + NUL) — the same shape as OtaRelease::sha256
// so the two can be compared with strcmp().
void hexEncodeSha256(const uint8_t hash[32], char out[65]) {
  static const char* kDigits = "0123456789abcdef";
  for (size_t i = 0; i < 32; i++) {
    out[i * 2]     = kDigits[hash[i] >> 4];
    out[i * 2 + 1] = kDigits[hash[i] & 0x0F];
  }
  out[64] = '\0';
}

// One full check-and-install attempt. Returns normally on every outcome
// except a successful install, which restarts the device from inside this
// function and never returns at all — see the contract in ota_task.h.
//
// THE ORDER BELOW IS A SECURITY PROPERTY, NOT A STYLE CHOICE. The signature
// is verified over the manifest before ANY field of the manifest is read for
// a decision, and before any binary is fetched. Nothing past step 3 acts on
// a byte that has not been authenticated. Reordering this — for instance,
// looking up the release before verifying, "just to decide whether it's
// worth fetching the signature" — would let an attacker who controls the
// release channel (or merely a MITM on an unauthenticated TLS connection,
// which is exactly what setInsecure() allows) choose what this device
// installs.
void runAttempt() {
  // --- Step 1: fetch manifest.txt, capped. ---------------------------------
  setPhase(OTA_MANIFEST);
  const int manifestLen = fetchCapped("manifest.txt", g_manifest, MANIFEST_CAP);
  if (manifestLen < 0) {
    fail("manifest fetch failed");
    return;
  }
  // NUL-terminate for otaManifestFind's C-string parsing below. This byte is
  // added AFTER manifestLen was captured and is never included in what gets
  // signature-checked or hashed — the length passed to otaVerifyManifest is
  // manifestLen, the exact byte count received, not strlen() of this buffer.
  g_manifest[manifestLen] = '\0';

  // --- Step 2: fetch manifest.sig, capped. ---------------------------------
  setPhase(OTA_VERIFY);
  uint8_t sig[SIG_CAP];
  const int sigLen = fetchCapped("manifest.sig", sig, SIG_CAP);
  if (sigLen < 0) {
    // Absent, non-200, or oversized are all the same refusal: an update
    // channel that cannot produce a signature is not a channel this device
    // trusts, full stop.
    fail("release is not signed");
    return;
  }

  // --- Step 3: verify BEFORE reading the manifest for anything else. ------
  // Signed over the EXACT bytes received — g_manifest, manifestLen — with no
  // trimming, no re-encoding, no normalisation of line endings. Any of those
  // would let the verified byte sequence differ from the one otaManifestFind
  // parses next, which is exactly the kind of gap a signature check exists
  // to close.
  if (!otaVerifyManifest(OTA_PUBKEY_PEM, g_manifest, (size_t)manifestLen, sig,
                        (size_t)sigLen)) {
    fail("bad signature");
    return;
  }

  // --- Step 4: only now, select this environment's release and decide. ----
  // Everything from here on trusts the manifest's contents, because step 3
  // just proved they came from the holder of the release signing key.
  OtaRelease rel{};
  if (!otaManifestFind((const char*)g_manifest, OTA_ENV, &rel)) {
    fail("no release for this environment");
    return;
  }
  if (!otaUpdateApplies(rel, FW_VERSION)) {
    // Not a failure — either the device is already running this version, or
    // FW_VERSION is "dev-"-prefixed (a local developer build), which
    // otaUpdateApplies() refuses to update by explicit check, never by
    // string inequality alone. See the header comment on otaUpdateApplies()
    // in ota_manifest.h.
    finishNoUpdate();
    return;
  }

  // A version that was just rolled back by the health gate is not retried
  // immediately (C2, whole-branch review): without this check, a rejected
  // release cycles back through this exact code path on every ~75s check
  // interval, forever — see otaHealthRecordInstalling()'s write, just
  // before ESP.restart() in step 7 below, and ota_health.h's promise that a
  // rejected release "will simply be offered again the next day."
  if (otaVersionSuppressed(otaSuppressedVersion(), otaSuppressedEpoch(),
                           (int64_t)time(nullptr), rel.version)) {
    Serial.printf("[ota] %s was rejected recently; not retrying yet\n", rel.version);
    finishNoUpdate();
    return;
  }
  setVersion(rel.version);
  // Announce the decision. Between here and "install verified" the task spends
  // tens of seconds pulling ~1.1 MB, and without this line the console shows
  // nothing at all for that whole window -- which reads as a hang rather than
  // as work in progress, especially on a weak link.
  Serial.printf("[ota] %s -> %s, downloading %u bytes\n",
                FW_VERSION, rel.version, (unsigned)rel.size);

  // --- Step 5: download and install. ---------------------------------------
  setPhase(OTA_DOWNLOAD);

  WiFiClientSecure net;
  net.setInsecure();  // see the note in fetchCapped(): deliberate, not an oversight.
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);

  char url[256];
  // Same fail-closed truncation check as fetchCapped() above: rel.file comes
  // from an already-signature-verified manifest, but a truncated URL would
  // still fetch whatever the shorter path happens to resolve to rather than
  // the release actually named.
  const int urlLen = snprintf(url, sizeof(url), "%s%s", OTA_BASE_URL, rel.file);
  if (urlLen < 0 || (size_t)urlLen >= sizeof(url)) {
    fail("url too long");
    return;
  }
  if (!http.begin(net, url)) {
    fail("could not open connection");
    return;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    fail("firmware fetch failed");
    return;
  }

  // The declared size must match the manifest's size EXACTLY before a single
  // byte is written to the flash slot. A mismatch here is refused up front
  // rather than discovered 1.1 MB later as a hash failure, and it also
  // catches a server handing back the wrong object entirely (e.g. a
  // redirect to an HTML error page, which would have some other length).
  const int declared = http.getSize();
  if (declared <= 0 || (uint32_t)declared != rel.size) {
    http.end();
    fail("size mismatch");
    return;
  }

  if (!Update.begin(rel.size)) {
    http.end();
    fail("could not begin update");
    return;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, /*is224=*/0);

  WiFiClient* stream = http.getStreamPtr();
  uint8_t     buf[CHUNK];
  uint32_t    done = 0;
  uint32_t    deadline = millis() + IO_DEADLINE_MS;
  bool        streamFailed = false;

  setProgressBytes(0, rel.size);
  while (done < rel.size) {
    const size_t avail = stream->available();
    if (avail) {
      size_t room = rel.size - done;
      const size_t want = min(avail, min(sizeof(buf), room));
      const size_t got = stream->readBytes(buf, want);
      if (got == 0) {
        streamFailed = true;
        break;
      }
      // Write and hash the SAME bytes that were just read, in the SAME
      // order they arrived — the running digest must reflect exactly what
      // landed in the flash slot for the comparison in step 6 to mean
      // anything.
      if (Update.write(buf, got) != got) {
        streamFailed = true;
        break;
      }
      mbedtls_sha256_update_ret(&sha, buf, got);
      done += (uint32_t)got;
      deadline = millis() + IO_DEADLINE_MS;  // reset the idle timeout, not the whole-transfer one
      setProgressBytes(done, rel.size);
      continue;
    }
    if (!http.connected()) {
      streamFailed = true;
      break;
    }
    if ((int32_t)(millis() - deadline) >= 0) {
      streamFailed = true;
      break;
    }
    delay(5);
  }
  http.end();

  if (streamFailed || done != rel.size) {
    mbedtls_sha256_free(&sha);
    Update.abort();
    fail("download failed");
    return;
  }

  // --- Step 6: check the hash BEFORE activating the slot. ------------------
  setPhase(OTA_INSTALL);
  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);

  char digestHex[65];
  hexEncodeSha256(digest, digestHex);

  if (strcmp(digestHex, rel.sha256) != 0) {
    // The slot MUST NOT be activated on a mismatch. Update.abort() discards
    // it; Update.end() — which sets the boot partition — is never called on
    // this path.
    Update.abort();
    fail("bad image hash");
    return;
  }

  if (!Update.end(true)) {
    fail("could not finalize update");
    return;
  }

  // --- Step 7: reboot into the new slot. ------------------------------------
  // No further Progress update is published here on purpose: the contract in
  // ota_task.h is that a successful install has no observable "done, about
  // to restart" state, because nothing is left running long enough to poll
  // for it.
  logStackHeadroom();

  // Record what is about to boot BEFORE restarting (C2, whole-branch
  // review). If this image fails its health gate, otaHealthReport() rolls
  // back to the slot that is about to be replaced, and that slot's own
  // otaHealthBegin() — on the very next boot — needs this NVS record to
  // recognise "I was just rolled back from this version" and suppress
  // retrying it for OTA_SUPPRESS_WINDOW_SEC (see ota_health.h and
  // ota_manifest.h). time(nullptr) is wall-clock, not millis(), because it
  // has to be compared against "now" on a LATER — possibly much later —
  // boot; NTP has long since synced by this point (ota_task::begin() is
  // never called until setup() has already synced the clock).
  otaHealthRecordInstalling(rel.version);

  Serial.println("[ota] install verified, restarting");
  Serial.flush();
  ESP.restart();
}

void taskMain(void*) {
  for (;;) {
    // Sleep until start() posts a request. No polling, no spinning — same
    // shape as net_task's taskMain.
    xSemaphoreTake(g_wake, portMAX_DELAY);
    runAttempt();
    // Only reached on a non-restarting outcome (finishNoUpdate() or a fail()
    // along the way); a successful install exits the process from inside
    // runAttempt() instead. Both of those already cleared g_busy under the
    // lock, so there is nothing further to do here except log and go back to
    // sleep for the next start().
    //
    // Log the outcome as well. Every fail() path already records a specific
    // reason in Progress::error -- "manifest fetch failed", "bad signature",
    // "release is not signed", "bad image hash" and so on -- but nothing ever
    // printed it, so on hardware a failed check produced only the stack line
    // below and every distinct failure looked identical from the console. That
    // is a poor position for a device whose whole premise is that it updates
    // itself unattended, with nobody watching to notice it has stopped.
    {
      const Progress p = progress();
      if (p.phase == OTA_FAILED) {
        Serial.printf("[ota] update check failed: %s\n",
                      p.error ? p.error : "unknown");
      } else {
        // Deliberately not "up to date": the suppression path in runAttempt()
        // has already printed its own more specific line, and following it
        // with "up to date" would contradict it.
        Serial.println("[ota] no update installed");
      }
    }
    logStackHeadroom();
  }
}

}  // namespace

void begin() {
  if (g_task) return;  // idempotent: a second call must not create a second task

  g_lock = xSemaphoreCreateMutex();
  g_wake = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(taskMain, "ota", TASK_STACK, nullptr,
                          // Same priority as net_task: below loopTask's
                          // default, yielding whenever it blocks on the
                          // network, which is nearly all of its life.
                          1, &g_task, TASK_CORE);
}

bool start() {
  if (!g_task) return false;

  {
    Lock lock;
    if (g_busy) return false;  // one update in flight at a time — see ota_task.h
    g_busy = true;
    g_progress = Progress{};
    g_progress.phase = OTA_MANIFEST;
    g_progress.busy = true;
  }

  xSemaphoreGive(g_wake);
  return true;
}

bool busy() {
  if (!g_task) return false;
  Lock lock;
  return g_busy;
}

Progress progress() {
  if (!g_task) return Progress{};
  Lock lock;
  return g_progress;
}

}  // namespace ota_task
#endif  // ARDUINO
