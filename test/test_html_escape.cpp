// Host coverage for htmlEscape() — F-6 from the 2026-08 adversarial review of
// portal.cpp. htmlEscape() sanitises attacker-controlled WiFi SSIDs into HTML
// attribute values, but it lived `static` inside an anonymous namespace
// behind #ifdef ARDUINO and was unreachable from any host test. It is now
// templated in ../src/html_escape.h so this test drives it with std::string
// while the device drives the exact same code with Arduino String.
#include <cstdio>
#include <string>

#include "../src/html_escape.h"

namespace {
int failures = 0;
void check(bool cond, const char* what) {
  if (!cond) { std::printf("FAIL: %s\n", what); failures++; }
}
std::string esc(const std::string& s) { return htmlEscape(s); }
}  // namespace

int main() {
  // ---- each escapable character individually -----------------------------
  check(esc("a&b")  == "a&amp;b",  "escape: ampersand");
  check(esc("a<b")  == "a&lt;b",   "escape: less-than");
  check(esc("a>b")  == "a&gt;b",   "escape: greater-than");
  check(esc("a\"b") == "a&quot;b", "escape: double quote");
  check(esc("a'b")  == "a&#39;b",  "escape: apostrophe");

  // ---- boundary cases ------------------------------------------------------
  check(esc("plain text, no escapables") == "plain text, no escapables",
        "escape: untouched when nothing needs escaping");
  check(esc("") == "", "escape: empty string");
  check(esc("&<>\"'") == "&amp;&lt;&gt;&quot;&#39;",
        "escape: all five escapables back to back");

  // ---- realistic hostile SSIDs ---------------------------------------------
  // The escaped SSID is rendered into <option value='…'> — a SINGLE-quoted
  // attribute — so the apostrophe is the character that matters most: it is
  // the one that closes the attribute early and lets an attacker add a bare
  // event handler.
  {
    const std::string ssid = "' onfocus=alert(1) x='";
    const std::string out = esc(ssid);
    check(out.find('\'') == std::string::npos,
          "escape: hostile SSID apostrophe cannot break out of a single-quoted attribute");
    check(out == "&#39; onfocus=alert(1) x=&#39;", "escape: hostile SSID (onfocus) full payload");
  }
  {
    const std::string ssid = "\"><script>alert(1)</script>";
    const std::string out = esc(ssid);
    check(out.find('<') == std::string::npos, "escape: hostile SSID no raw '<' survives");
    check(out.find('>') == std::string::npos, "escape: hostile SSID no raw '>' survives");
    check(out.find('"') == std::string::npos, "escape: hostile SSID no raw '\"' survives");
    check(out == "&quot;&gt;&lt;script&gt;alert(1)&lt;/script&gt;",
          "escape: hostile SSID (script tag) full payload");
  }

  if (failures) { std::printf("%d FAILED\n", failures); return 1; }
  std::printf("test_html_escape: ALL PASS\n");
  return 0;
}
