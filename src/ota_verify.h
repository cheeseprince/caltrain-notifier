// ota_verify.h — is this manifest really ours?
//
// Authenticity comes from a detached ECDSA-P256/SHA-256 signature over the
// exact bytes of manifest.txt, checked against a public key compiled into the
// firmware. The release channel is also fetched over TLS with the server
// certificate validated (ota_task.cpp), but that is a separate control with a
// separate job: the certificate proves the manifest came from the release host
// just now, the signature proves it was published by the holder of the signing
// key. Neither replaces the other — a signature never expires, so certificate
// validation is what stops an on-path attacker replaying an old, still-valid
// manifest as a downgrade; and a valid certificate says nothing about who
// signed the bytes behind it.
//
// There is no unsigned mode. An empty, null or unparseable key, an empty
// signature, or a failed check all return false. obd-gauge-cluster ships an
// opt-in "transition mode" where an empty key disables checking; that is safe
// only because a human triggers each of its updates. This device installs
// unattended, so the same setting would be a silent remote-code-execution path.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Verify `sig` over `manifest` using the PEM public key `pubkeyPem`.
// Returns true only on a cryptographically valid signature.
bool otaVerifyManifest(const char* pubkeyPem,
                       const uint8_t* manifest, size_t manifestLen,
                       const uint8_t* sig, size_t sigLen);
