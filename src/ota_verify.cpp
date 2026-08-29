#include "ota_verify.h"

#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <string.h>

bool otaVerifyManifest(const char* pubkeyPem,
                       const uint8_t* manifest, size_t manifestLen,
                       const uint8_t* sig, size_t sigLen) {
  // Belt and braces, with one exception. mbedtls_pk_parse_public_key also
  // rejects an empty PEM, so removing the manifest/sig checks below would not
  // currently break the "no unsigned mode" property — which means no test can
  // pin them individually, and they stay anyway because they state the
  // contract locally and do not depend on the behaviour of a library version
  // we do not control.
  //
  // The !pubkeyPem check is NOT in that same category (Deferred #3, whole-
  // branch review): removing it specifically does not degrade gracefully to
  // "unenforced" — it hands strlen(nullptr) to the mbedtls_pk_parse_public_key
  // call below, which is undefined behaviour, not a refusal. Precision matters
  // here: this guard is closing a crash, not merely doubling up a check the
  // library would have made anyway.
  //
  // Every one of these is a refusal, never a fall-through to "unenforced".
  if (!pubkeyPem || !*pubkeyPem) return false;
  if (!manifest || manifestLen == 0) return false;
  if (!sig || sigLen == 0) return false;

  uint8_t hash[32];
  if (mbedtls_sha256_ret(manifest, manifestLen, hash, 0) != 0) return false;

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);

  // For PEM input mbedtls requires the length to INCLUDE the terminating NUL.
  // Passing strlen() alone fails to parse, which would look identical to a
  // tampered key and silently refuse every update.
  const int rc = mbedtls_pk_parse_public_key(
      &pk, (const unsigned char*)pubkeyPem, strlen(pubkeyPem) + 1);

  bool ok = false;
  if (rc == 0) {
    ok = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof hash,
                           sig, sigLen) == 0;
  }

  mbedtls_pk_free(&pk);
  return ok;
}
