#include "ota_manifest.h"

#include <string.h>

namespace {

// Copy one space- or newline-delimited field into a fixed buffer.
// Returns the character after the field, or nullptr if it is empty or too long
// to fit. Refusing to truncate is the point: a truncated filename would fetch
// a different object, and a truncated hash would never match.
const char* takeField(const char* p, char* dst, size_t cap) {
  if (!p) return nullptr;
  size_t n = 0;
  while (p[n] && p[n] != ' ' && p[n] != '\n' && p[n] != '\r') n++;
  if (n == 0 || n >= cap) return nullptr;
  memcpy(dst, p, n);
  dst[n] = '\0';
  return p + n;
}

bool isHex64(const char* s) {
  size_t n = 0;
  for (; s[n]; n++) {
    const char c = s[n];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!hex) return false;
  }
  return n == 64;
}

// Parse an unsigned decimal. Returns false on empty, non-digit, or overflow.
bool parseSize(const char* s, uint32_t* out) {
  if (!*s) return false;
  uint64_t v = 0;
  for (size_t i = 0; s[i]; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (uint64_t)(s[i] - '0');
    if (v > 0xFFFFFFFFull) return false;
  }
  *out = (uint32_t)v;
  return true;
}

}  // namespace

bool otaManifestFind(const char* manifest, const char* env, OtaRelease* out) {
  if (!manifest || !env || !*env || !out) return false;

  for (const char* line = manifest; *line;) {
    const char* eol = strchr(line, '\n');
    // No terminating newline means a truncated download. Refuse the last line
    // rather than parse a partial one.
    if (!eol) break;

    const size_t envLen = strlen(env);
    // Exact match on the first field, so a prefix cannot select a longer name.
    if (strncmp(line, env, envLen) == 0 && line[envLen] == ' ') {
      OtaRelease r{};
      const char* p = line + envLen + 1;

      p = takeField(p, r.version, sizeof r.version);
      if (!p || *p != ' ') return false;
      p = takeField(p + 1, r.sha256, sizeof r.sha256);
      if (!p || *p != ' ' || !isHex64(r.sha256)) return false;

      char sizeBuf[16];
      p = takeField(p + 1, sizeBuf, sizeof sizeBuf);
      if (!p || *p != ' ' || !parseSize(sizeBuf, &r.size) || r.size == 0) return false;

      p = takeField(p + 1, r.file, sizeof r.file);
      // The last field must end the line. Every other field enforces its
      // trailing delimiter; without this one, arbitrary trailing content is
      // silently discarded and a malformed manifest reads as well-formed.
      if (!p || (*p != '\n' && !(*p == '\r' && p[1] == '\n'))) return false;

      *out = r;
      return true;
    }
    line = eol + 1;
  }
  return false;
}

bool otaUpdateApplies(const OtaRelease& rel, const char* currentVersion) {
  if (!currentVersion || !*currentVersion) return false;
  if (!rel.version[0]) return false;

  // A developer build must never be silently replaced by a published
  // release — see the header comment above and fw_version.h. Checked
  // explicitly: string inequality alone would have the OPPOSITE effect,
  // since "dev-local" never equals a real release tag.
  if (strncmp(currentVersion, "dev-", 4) == 0) return false;

  return strcmp(rel.version, currentVersion) != 0;
}

bool otaVersionSuppressed(const char* suppressedVersion, int64_t rejectedEpoch,
                          int64_t nowEpoch, const char* candidateVersion) {
  if (!suppressedVersion || !suppressedVersion[0]) return false;
  if (!candidateVersion || strcmp(suppressedVersion, candidateVersion) != 0) return false;

  if (rejectedEpoch <= OTA_EPOCH_SANE_AFTER || nowEpoch <= OTA_EPOCH_SANE_AFTER) return true;

  const int64_t elapsed = nowEpoch - rejectedEpoch;
  if (elapsed < 0) return true;  // clock stepped backwards: do not trust it either
  return elapsed < OTA_SUPPRESS_WINDOW_SEC;
}
