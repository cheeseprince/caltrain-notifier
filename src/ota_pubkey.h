// ota_pubkey.h — the OTA release signing PUBLIC key (ECDSA P-256, PEM).
//
// Every release manifest must carry a valid signature from the matching
// private key or the update is refused. There is no unsigned mode and no
// transition mode: see ota_verify.h for why this project does not inherit the
// opt-in signing that obd-gauge-cluster ships. This device installs updates
// unattended, so an unenforced signature would be a silent remote-code-
// execution path rather than a convenience.
//
// The PRIVATE key is never in this repository and never on the build machine's
// path. It lives in the repository's OTA_SIGNING_KEY Actions secret and in an
// off-machine backup. Recreating the repository (see
// scripts/apply-repo-settings.sh) destroys Actions secrets, so that off-machine
// copy is the only thing that keeps OTA alive across that step.
//
// This key is deliberately DISTINCT from obd-gauge-cluster's signing key.
// Sharing one would mean a single compromise signs firmware for both devices,
// a rotation would require USB-reflashing both, and an obd manifest would be
// cryptographically valid here — leaving only an OTA_ENV string comparison
// between the two channels instead of a failed signature.
//
// Replacing this key strands every already-deployed unit: a device trusts only
// the key compiled into the build it is running, and would refuse every release
// signed by a new one. A key change therefore means a USB re-flash of every
// device that is already in the field.
#pragma once

static const char OTA_PUBKEY_PEM[] =
  "-----BEGIN PUBLIC KEY-----\n"
  "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEP5Y+jkyD5Sm4utzrpc03MfKNIa4M\n"
  "weMiGuNzxGliQHmf8nT3koQnDFCAPmwwxfxdgfgqGPGfVnqEzFnGqPvbtQ==\n"
  "-----END PUBLIC KEY-----\n";
