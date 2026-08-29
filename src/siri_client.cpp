#ifdef ARDUINO
#include "siri_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

// The response measured 3,027 bytes for three departures. 8 KB is generous
// headroom without being wasteful.
constexpr size_t RESPONSE_CAP = 8192;

// FILE-SCOPE, not a local. A buffer this size on the stack, on top of a TLS
// handshake, overflows the ~8 KB loopTask stack — that combination produced a
// panic-reboot with nothing on screen in the obd-gauge-cluster project. Static
// storage costs the same RAM and cannot blow the stack.
uint8_t g_body[RESPONSE_CAP + 1];

// The Mozilla root CA bundle is already inside the prebuilt libmbedtls.a that
// ships with the Arduino core. Referencing this symbol pulls it into the image
// (~64 KB) so certificates are validated properly rather than with
// setInsecure().
//
// Arduino core 2.x does NOT wire this up automatically — the pointer has to be
// handed to setCACertBundle() explicitly or validation silently does nothing.
//
// Note the signature differs by core version: 2.x takes only the start pointer
// (the bundle is self-describing), while 3.x added a length parameter. This
// build pins core 2.x in platformio.ini, so the one-argument form is correct;
// a platform bump will break this line loudly, which is the intent.
extern "C" {
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
}

}  // namespace

FetchResult siriFetch(const char* token, uint32_t stopCode, SiriProgressFn progress) {
  // Local shim so the call sites below need no null checks.
  auto report = [progress](SiriPhase phase, uint32_t done, uint32_t total) {
    if (progress) progress(phase, done, total);
  };

  FetchResult out{};
  out.siri = SiriResult{};
  out.transportOk = false;
  out.httpCode = 0;
  out.error = nullptr;
  out.bytes = 0;

  if (!token || !*token) {
    out.error = "no API token configured";
    return out;
  }

  char url[192];
  snprintf(url, sizeof(url),
           "https://api.511.org/transit/StopMonitoring"
           "?api_key=%s&agency=CT&stopcode=%lu&format=json",
           token, (unsigned long)stopCode);

  WiFiClientSecure net;
  net.setCACertBundle(rootca_crt_bundle_start);
  net.setTimeout(15);

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  if (!http.begin(net, url)) {
    out.error = "could not open connection";
    return out;
  }

  // Do NOT advertise gzip. 511 honours Accept-Encoding and returns a compressed
  // body, which HTTPClient hands over without inflating — it would arrive as
  // binary and fail to parse. Asking for identity explicitly makes that
  // dependence visible instead of relying on the default.
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Accept", "application/json");

  // Everything expensive happens inside this one call, and it offers no hook to
  // report from. The most that can be said is that it has started.
  report(SIRI_PHASE_CONNECT, 0, 0);
  const int code = http.GET();
  out.httpCode = code;

  if (code <= 0) {
    // A negative code is an HTTPClient error, not an HTTP status: -1 means the
    // connection never opened at all. Worth distinguishing, because the usual
    // cause is the network rather than the API.
    out.error = "connection failed";
    http.end();
    return out;
  }
  if (code != HTTP_CODE_OK) {
    // 401 here almost always means a bad or unregistered token.
    out.error = (code == 401 || code == 403) ? "token rejected by 511"
                                             : "unexpected HTTP status";
    http.end();
    return out;
  }

  // Content-Length when the server sent one; getSize() returns -1 otherwise, in
  // which case the screen shows bytes so far with no total rather than a
  // percentage it would have to invent.
  const int declared = http.getSize();
  const uint32_t total = declared > 0 ? (uint32_t)declared : 0;

  WiFiClient* stream = http.getStreamPtr();
  size_t n = 0;
  const uint32_t deadline = millis() + 15000;

  // How long to keep waiting after the last byte, when the response length is
  // unknown. Only reached on a chunked or length-less reply, which 511 does not
  // currently send.
  constexpr uint32_t IDLE_GIVE_UP_MS = 1500;
  uint32_t lastByteAt = millis();

  report(SIRI_PHASE_DOWNLOAD, 0, total);
  while (n < RESPONSE_CAP) {
    // STOP AT CONTENT-LENGTH. Without this the loop has no idea the response
    // has finished: 511 keeps the connection open, so http.connected() stays
    // true and available() returns 0 forever, and the only exit left is the
    // 15 second deadline below. Every fetch paid that in full — the body
    // arrived in about a second and the loop then spun for fourteen more,
    // which is where "took=15000ms" and the long boot wait came from.
    if (total > 0 && n >= total) break;

    const size_t avail = stream->available();
    if (avail) {
      // Read no further than the declared body. Anything past it belongs to the
      // keep-alive connection, not to this response.
      size_t room = RESPONSE_CAP - n;
      if (total > 0 && total - n < room) room = total - n;
      const size_t got = stream->readBytes(g_body + n, min(avail, room));
      if (got == 0) break;
      n += got;
      lastByteAt = millis();
      report(SIRI_PHASE_DOWNLOAD, (uint32_t)n, total);
      continue;
    }

    // The connection dropping is a legitimate end of response.
    if (!http.connected()) break;

    // No length header: treat a gap after real data as the end rather than
    // waiting out the full timeout.
    if (total == 0 && n > 0 && (int32_t)(millis() - lastByteAt) >= (int32_t)IDLE_GIVE_UP_MS) break;

    // Signed comparison so the check is wrap-safe at the 49-day millis rollover.
    if ((int32_t)(millis() - deadline) >= 0) break;
    delay(5);
  }
  g_body[n] = '\0';
  out.bytes = n;
  http.end();

  if (n == 0) {
    out.error = "empty response body";
    return out;
  }
  if (n >= RESPONSE_CAP) {
    // Truncated JSON would fail to parse anyway; saying so is more useful than
    // reporting a generic malformed-document error.
    out.error = "response larger than buffer";
    return out;
  }

  out.transportOk = true;
  report(SIRI_PHASE_PARSE, (uint32_t)n, (uint32_t)n);
  out.siri = siriParse((const char*)g_body, n);
  if (!out.siri.ok) out.error = out.siri.error ? out.siri.error : "parse failed";
  report(SIRI_PHASE_DONE, (uint32_t)n, (uint32_t)n);
  return out;
}
#endif  // ARDUINO
