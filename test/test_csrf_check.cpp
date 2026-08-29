// Host coverage for csrfMatches() — the pure comparison behind the setup
// portal's CSRF guard (portal.cpp's csrfOk()). Split into ../src/csrf_check.h
// so this string compare is testable without a WebServer; the "was a csrf
// argument even supplied" question stays in portal.cpp, since that needs
// WebServer::hasArg().
#include <cstdio>
#include <string>

#include "../src/csrf_check.h"

namespace {
int failures = 0;
void check(bool cond, const char* what) {
  if (!cond) { std::printf("FAIL: %s\n", what); failures++; }
}
}  // namespace

int main() {
  check(csrfMatches(std::string("0123456789abcdef"), "0123456789abcdef"),
        "csrf: exact match succeeds");
  check(!csrfMatches(std::string("0123456789abcdee"), "0123456789abcdef"),
        "csrf: single differing character mismatches");
  check(!csrfMatches(std::string(""), "0123456789abcdef"),
        "csrf: empty supplied token does not match");
  check(!csrfMatches(std::string("0123"), "0123456789abcdef"),
        "csrf: too-short supplied token does not match");
  check(!csrfMatches(std::string("0123456789abcdef00"), "0123456789abcdef"),
        "csrf: too-long supplied token does not match");

  if (failures) { std::printf("%d FAILED\n", failures); return 1; }
  std::printf("test_csrf_check: ALL PASS\n");
  return 0;
}
