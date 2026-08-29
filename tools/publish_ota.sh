#!/usr/bin/env bash
# publish_ota.sh — local fallback: build both board revisions, sign the
# manifest, and publish an OTA release to gh-pages without going through CI.
#
# Releases live on this repo's `gh-pages` branch, served by GitHub Pages at
# OTA_BASE_URL (platformio.ini's [base] section). Once a day, ota_task.cpp
# fetches manifest.txt from there, verifies its signature against the key
# compiled into the running firmware (src/ota_pubkey.h), and self-updates
# over WiFi if it verifies and names a newer version for this board revision.
#
# UNLIKE obd-gauge-cluster's script of the same name, this one REFUSES to run
# without a signing key -- there is no unsigned/transition mode here. Every
# device rejects a manifest with no valid signature (src/ota_verify.h), so
# publishing one unsigned would ship a channel every device silently ignores
# forever: nothing on screen would say why, because the sign never even gets
# far enough to show an error for it (ota_task fails closed, quietly, on the
# next scheduled check).
#
#   ./tools/publish_ota.sh
#
# Requires: a clean git tree (the release is stamped with HEAD's short hash
# or, on a tagged commit, the tag itself), a signing key at
# $OTA_SIGNING_KEY_FILE (default ~/caltrain-ota-signing-key.pem), push access
# to this repo's `origin`, and `pio`/`openssl`/`python3` on PATH. Run from
# anywhere -- it cds to the repo root itself.
set -euo pipefail
cd "$(dirname "$0")/.."

ENVS=(caltrain caltrain_v20)
PAGES_BRANCH="gh-pages"
REMOTE_URL="$(git remote get-url origin)"
KEYFILE="${OTA_SIGNING_KEY_FILE:-$HOME/caltrain-ota-signing-key.pem}"

# --- Refuse up front: no key, no release. -----------------------------------
if [ ! -f "$KEYFILE" ]; then
  echo "ERROR: no signing key at $KEYFILE."
  echo "Every device refuses an unsigned release, so publishing one would do nothing."
  echo "Set OTA_SIGNING_KEY_FILE to point elsewhere, or restore the key from its"
  echo "off-machine backup first (see the MANUAL STEP note in"
  echo "scripts/apply-repo-settings.sh)."
  exit 1
fi

# --- Refuse a dirty tree: the release is stamped with HEAD. -----------------
if [ -n "$(git status --porcelain)" ]; then
  echo "ERROR: git tree is dirty — commit first (the release is stamped with HEAD)."
  exit 1
fi

GIT=$(git rev-parse --short HEAD)
# HEAD is a tagged release (vX.Y.Z) -> stamp that tag as the version;
# otherwise fall back to a dev-<hash> form -- matches release.yml's
# tag-push-vs-workflow_dispatch fallback and the FW_VERSION placeholder
# convention documented in src/fw_version.h.
VERSION=$(git describe --tags --exact-match --match 'v*' 2>/dev/null || echo "dev-$GIT")
echo "== $VERSION ($GIT) =="

# Matches src/fw_version.h's actual spacing exactly (single space after each
# macro name) -- see the same note in release.yml. Stamp now, restore on
# every exit path via the trap below, whether this script succeeds, fails
# partway through a later step, or is interrupted.
sed -i "s/#define FW_GIT \".*\"/#define FW_GIT \"$GIT\"/" src/fw_version.h
sed -i "s/#define FW_VERSION \".*\"/#define FW_VERSION \"$VERSION\"/" src/fw_version.h

TMP=$(mktemp -d)
trap 'git checkout -q -- src/fw_version.h; rm -rf "$TMP"; git worktree prune' EXIT

# --- Build both board revisions. Every release must carry both, or a v2.0 --
# unit can never update (src/ota_manifest.h: environment matching is exact,
# with no fallback from one name to another).
for e in "${ENVS[@]}"; do
  echo "== building $e"
  pio run -e "$e" >/dev/null
done

# --- Assemble the manifest, in the EXACT format otaManifestFind() parses ---
# (src/ota_manifest.cpp): "<env> <version> <sha256hex> <sizebytes> <filename>",
# single spaces, each line newline-terminated by `echo`. The parser rejects
# any trailing content after the filename, so nothing here appends past it.
mkdir -p "$TMP/out"
: > "$TMP/out/manifest.txt"
for e in "${ENVS[@]}"; do
  BIN=".pio/build/$e/firmware.bin"
  SHA=$(sha256sum "$BIN" | cut -d' ' -f1)
  SIZE=$(stat -c%s "$BIN")
  cp "$BIN" "$TMP/out/$e.bin"
  echo "$e $VERSION $SHA $SIZE $e.bin" >> "$TMP/out/manifest.txt"
  echo "   $e: $VERSION ($GIT) $SIZE bytes"
done
cat "$TMP/out/manifest.txt"

# --- Sign. Not optional -- see the header comment above and ota_verify.h. --
openssl dgst -sha256 -sign "$KEYFILE" -out "$TMP/out/manifest.sig" "$TMP/out/manifest.txt"
echo "manifest.sig written ($(stat -c%s "$TMP/out/manifest.sig") bytes)"

# --- Verify the signature just made against the SAME public key this repo's
# firmware ships with, before publishing it anywhere. Catches a stale or
# mismatched key at $KEYFILE here, rather than on a desk sign that fetches a
# manifest it can never verify and simply stops updating with nothing on
# screen to explain why. src/ota_pubkey.h holds the key as a C string
# literal, not a bare .pem file -- see tools/extract_pubkey_pem.py for why a
# small purpose-built parser, not a one-line sed, extracts it reliably.
python3 tools/extract_pubkey_pem.py src/ota_pubkey.h > "$TMP/ota_pubkey.pem"
openssl dgst -sha256 -verify "$TMP/ota_pubkey.pem" \
  -signature "$TMP/out/manifest.sig" "$TMP/out/manifest.txt"
echo "manifest.sig verified against src/ota_pubkey.h"

# --- Publish to gh-pages via a detached worktree (keeps binaries out of the
# source history entirely -- nothing under $TMP/pages ever touches the
# branch this script was run from).
if git ls-remote --exit-code --heads "$REMOTE_URL" "$PAGES_BRANCH" >/dev/null 2>&1; then
  git fetch -q origin "$PAGES_BRANCH"
  git worktree add -q "$TMP/pages" "origin/$PAGES_BRANCH"
  git -C "$TMP/pages" checkout -q -B "$PAGES_BRANCH"
else
  git worktree add -q --detach "$TMP/pages"
  git -C "$TMP/pages" checkout -q --orphan "$PAGES_BRANCH"
  git -C "$TMP/pages" rm -rq --cached . 2>/dev/null || true
fi

# gh-pages holds only the CURRENT release (matches release.yml's
# keep_files: false) -- wipe whatever was checked out above before copying
# the new files in.
find "$TMP/pages" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +

cp "$TMP/out/manifest.txt" "$TMP/out/manifest.sig" "$TMP/pages/"
for e in "${ENVS[@]}"; do
  cp "$TMP/out/$e.bin" "$TMP/pages/"
done

git -C "$TMP/pages" add -A
git -C "$TMP/pages" commit -q -m "release $VERSION ($GIT)"
git -C "$TMP/pages" push -q origin "$PAGES_BRANCH"
echo "published $VERSION ($GIT) -> gh-pages (GitHub Pages serves it in ~1 min)"
