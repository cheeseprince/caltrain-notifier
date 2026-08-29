#pragma once

// Pure comparison behind the setup portal's CSRF guard: does the token the
// browser supplied match the per-session token minted when the portal came
// up?
//
// Split out of portal.cpp's csrfOk() so the actual comparison is
// host-testable. Whether a "csrf" argument was supplied at all stays in
// portal.cpp, since answering that needs WebServer::hasArg() — dragging that
// type in here would not buy anything, since the interesting logic is the
// string compare, not the presence check.
template <class Str>
bool csrfMatches(const Str& supplied, const char* expected) {
  return supplied == expected;
}
