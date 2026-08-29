#!/usr/bin/env bash
# gen_ota_test_vectors.sh — regenerate the OTA signature test fixtures.
#
#   ./tools/gen_ota_test_vectors.sh
#
# The keypair written here is a THROWAWAY used only by the host tests. Its
# public half and the signature it produces are committed; the private half
# is generated, used to sign the fixture manifest, and then discarded below —
# the tests only ever verify, so they never need it. It must never be the
# release key: the release private key lives only in the OTA_SIGNING_KEY
# GitHub secret and an off-machine backup, and is never written into this
# repository.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=test/fixtures/ota
mkdir -p "$OUT"

openssl ecparam -genkey -name prime256v1 -noout -out "$OUT/test_key.pem"
openssl ec -in "$OUT/test_key.pem" -pubout -out "$OUT/test_pub.pem" 2>/dev/null

cat > "$OUT/manifest.txt" <<'EOF'
caltrain v1.2.0 2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881 1140256 caltrain.bin
caltrain_v20 v1.2.0 9f2c8ab1d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f 1140256 caltrain_v20.bin
EOF

openssl dgst -sha256 -sign "$OUT/test_key.pem" -out "$OUT/manifest.sig" "$OUT/manifest.txt"
openssl dgst -sha256 -verify "$OUT/test_pub.pem" -signature "$OUT/manifest.sig" "$OUT/manifest.txt"

# The signing key is deliberately NOT kept. The tests only ever verify, so they
# need the public key, the manifest and the signature — never the private half.
# Discarding it means no private key is committed, and .gitleaks.toml keeps its
# "allowlist by value, never by path" invariant intact.
rm -f "$OUT/test_key.pem"

# A second, unrelated key. The test asserts its public half does NOT verify a
# signature made by the first — otherwise "verified" could mean "parsed".
openssl ecparam -genkey -name prime256v1 -noout -out "$OUT/other_key.pem"
openssl ec -in "$OUT/other_key.pem" -pubout -out "$OUT/other_pub.pem" 2>/dev/null
rm -f "$OUT/other_key.pem"

echo "wrote $OUT (signature $(stat -c%s "$OUT/manifest.sig") bytes)"
