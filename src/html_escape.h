#pragma once

// HTML-escape untrusted text before it is placed into the setup portal's
// pages.
//
// THE UNTRUSTED INPUT IS A WIFI SSID, AND ANY NEARBY DEVICE CHOOSES IT. The
// portal renders scanned SSIDs into <option value='…'> — a single-quoted
// HTML attribute — so a crafted SSID that escaped this could break out of
// the attribute and inject markup or script into the page the owner opens on
// their phone during setup. This is the F-6 finding from the 2026-08
// adversarial review of portal.cpp.
//
// ALL FIVE characters matter, and the apostrophe is not optional padding:
// the attribute is single-quoted, so `'` is the one that closes it early.
//
// TEMPLATED so the firmware keeps using Arduino String while the host test
// drives std::string. Both provide length(), operator[] and += for char and
// const char*, so the code under test is byte-for-byte the code that ships —
// which is the point of extracting this out of portal.cpp, where it was
// `static` inside an anonymous namespace behind #ifdef ARDUINO and therefore
// unreachable from the host suite.
template <class Str>
Str htmlEscape(const Str& in) {
  Str out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    const char c = in[i];
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:
        // Drop control characters rather than emitting them raw.
        if ((unsigned char)c >= 0x20) out += c;
    }
  }
  return out;
}
