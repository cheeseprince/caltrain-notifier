// Firmware version and git SHA
//
// Placeholders ("dev-local" and "0000000") report what a developer build contains.
// A release build stamps real values in at compile time and restores placeholders
// afterwards, so a working-tree build always reports "dev-local".
//
// otaUpdateApplies() (ota_manifest.cpp) treats ANY "dev-"-prefixed FW_VERSION as
// never updatable, regardless of what the manifest offers. That check — not
// string inequality — is what stops a USB-flashed developer build being
// silently replaced by a published release: inequality alone would do the
// opposite, since "dev-local" never equals a real release tag and would
// therefore always look like an update is due.

#pragma once

#define FW_VERSION "dev-local"
#define FW_GIT "0000000"
