// Host tests for OTA manifest signature verification.
//
// This is the only thing standing between a manipulated release channel and
// arbitrary code running on the sign: TLS uses setInsecure(), so anyone able to
// intercept the connection supplies both the manifest and the binary, and the
// SHA-256 then only proves those two agree with each other. The signature is
// what makes them provably ours.
//
// Fixtures come from tools/gen_ota_test_vectors.sh (throwaway keypair).
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include "ota_verify.h"

static int failures = 0;
static void check(bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); failures++; } }

static std::vector<uint8_t> slurp(const char* path) {
  std::vector<uint8_t> v;
  FILE* f = fopen(path, "rb");
  if (!f) { printf("FAIL: cannot open %s\n", path); failures++; return v; }
  uint8_t buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
  fclose(f);
  return v;
}

int main() {
  std::vector<uint8_t> pub = slurp("fixtures/ota/test_pub.pem");
  pub.push_back(0);  // mbedtls needs the PEM NUL-terminated
  const std::vector<uint8_t> man = slurp("fixtures/ota/manifest.txt");
  const std::vector<uint8_t> sig = slurp("fixtures/ota/manifest.sig");
  if (failures) { printf("test_ota_verify FAILED (fixtures missing)\n"); return 1; }

  const char* pem = (const char*)pub.data();

  check(otaVerifyManifest(pem, man.data(), man.size(), sig.data(), sig.size()),
        "a valid signature is accepted");

  {
    std::vector<uint8_t> altered = man;
    altered[0] ^= 0x01;
    check(!otaVerifyManifest(pem, altered.data(), altered.size(), sig.data(), sig.size()),
          "a manifest altered by one bit is rejected");
  }
  {
    // Appending is the realistic attack: keep the real line, add your own.
    std::vector<uint8_t> extra = man;
    const char* add = "caltrain v9.9.9 0000000000000000000000000000000000000000000000000000000000000000 1 evil.bin\n";
    extra.insert(extra.end(), add, add + strlen(add));
    check(!otaVerifyManifest(pem, extra.data(), extra.size(), sig.data(), sig.size()),
          "an appended manifest line is rejected");
  }
  {
    std::vector<uint8_t> bad = sig;
    bad[10] ^= 0x01;
    check(!otaVerifyManifest(pem, man.data(), man.size(), bad.data(), bad.size()),
          "a corrupted signature is rejected");
  }

  check(!otaVerifyManifest(pem, man.data(), man.size(), sig.data(), 0),
        "an empty signature is rejected");
  check(!otaVerifyManifest(pem, man.data(), man.size(), nullptr, 8),
        "a null signature is rejected");

  // The property that distinguishes this project from obd: no transition mode.
  check(!otaVerifyManifest("", man.data(), man.size(), sig.data(), sig.size()),
        "an empty public key is rejected, never treated as 'unenforced'");
  check(!otaVerifyManifest(nullptr, man.data(), man.size(), sig.data(), sig.size()),
        "a null public key is rejected");
  check(!otaVerifyManifest("-----BEGIN PUBLIC KEY-----\nnonsense\n-----END PUBLIC KEY-----\n",
                           man.data(), man.size(), sig.data(), sig.size()),
        "an unparseable public key is rejected");

  {
    // A different key must not verify our manifest.
    std::vector<uint8_t> other = slurp("fixtures/ota/other_pub.pem");
    if (!other.empty()) {
      other.push_back(0);
      check(!otaVerifyManifest((const char*)other.data(), man.data(), man.size(),
                               sig.data(), sig.size()),
            "a signature from a different key is rejected");
    }
  }

  printf(failures ? "test_ota_verify FAILED\n" : "test_ota_verify passed\n");
  return failures != 0;
}
