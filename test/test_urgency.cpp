// Host coverage for urgency.h — the border-colour rule, its bounds, and the
// sentence the setup portal shows back.
//
// urgencyBandsText() is here for the same reason htmlEscape() and
// csrfMatches() are (F-6, 2026-08 adversarial review): it was written inside
// portal.cpp behind #ifdef ARDUINO, where nothing on the host could reach it.
// It turns two exclusive "under N" bounds into three inclusive bands, which is
// exactly the sort of off-by-one that is invisible on a phone screen.
#include <cstdio>
#include <cstring>

#include "../src/urgency.h"

namespace {
int failures = 0;
void check(bool cond, const char* what) {
  if (!cond) { std::printf("FAIL: %s\n", what); failures++; }
}

// The bands sentence for a pair, as the portal would render it.
const char* bands(uint8_t red, uint8_t yellow) {
  static char buf[128];
  urgencyBandsText(UrgencyThresholds{red, yellow}, buf, sizeof(buf));
  return buf;
}
}  // namespace

int main() {
  // --- The bands sentence ---------------------------------------------------
  // The defaults must read back as the rule the README documents: the yellow
  // band ENDS at 15, one below the exclusive bound of 16.
  {
    const char* s = bands(10, 16);
    check(strstr(s, "red under 10 min") != nullptr, "defaults: names the red bound");
    check(strstr(s, "yellow 10") != nullptr, "defaults: yellow starts at the red bound");
    check(strstr(s, "15") != nullptr, "defaults: yellow ends at 15, not 16");
    check(strstr(s, "green from 16") != nullptr, "defaults: green starts at the yellow bound");
  }

  // A one-minute yellow band is the narrowest one that still exists, and the
  // place an off-by-one would show up first: yellow 10-10, not 10-9.
  {
    const char* s = bands(10, 11);
    check(strstr(s, "yellow 10&ndash;10") != nullptr, "a one-minute band reads as 10-10");
    check(strstr(s, "green from 11") != nullptr, "and green picks up at 11");
  }

  // Collapsed: no yellow band at all. Printing "yellow 10-9" here would be
  // worse than useless, so the sentence has to say what actually happens.
  {
    const char* s = bands(10, 10);
    check(strstr(s, "no yellow band") != nullptr, "a collapsed pair says so");
    check(strstr(s, "yellow 10") == nullptr, "and does not print an empty band");
  }

  // An inverted pair reaches this only from a hand-crafted POST, but the
  // sentence must not print a nonsense range if it ever does.
  {
    const char* s = bands(30, 5);
    check(strstr(s, "no yellow band") != nullptr, "an inverted pair reads as collapsed");
  }

  // The whole sentence must fit the buffer the portal gives it, at the widest
  // numbers the form allows.
  {
    char small[128];
    urgencyBandsText(UrgencyThresholds{URGENCY_MAX_MINUTES, URGENCY_MAX_MINUTES}, small, sizeof(small));
    check(strlen(small) < sizeof(small) - 1, "the widest collapsed sentence is not truncated");
    urgencyBandsText(UrgencyThresholds{1, URGENCY_MAX_MINUTES}, small, sizeof(small));
    check(strlen(small) < sizeof(small) - 1, "nor is the widest three-band one");
    check(strstr(small, "green from 60") != nullptr, "and it still ends correctly");
  }

  // --- Bounds ---------------------------------------------------------------
  {
    UrgencyThresholds t{0, 0};
    urgencySanitise(t);
    check(t.redUnder == URGENCY_MIN_MINUTES, "zero is lifted to the minimum");

    t = UrgencyThresholds{255, 255};
    urgencySanitise(t);
    check(t.redUnder == URGENCY_MAX_MINUTES && t.yellowUnder == URGENCY_MAX_MINUTES,
          "both are capped at the maximum");

    t = UrgencyThresholds{30, 5};
    urgencySanitise(t);
    check(t.redUnder == 30 && t.yellowUnder == 30, "an inverted pair collapses at red");

    t = kUrgencyDefaults;
    urgencySanitise(t);
    check(t.redUnder == kUrgencyDefaults.redUnder &&
          t.yellowUnder == kUrgencyDefaults.yellowUnder,
          "the shipped defaults survive sanitising unchanged");
  }

  if (failures == 0) std::printf("test_urgency: ALL PASS\n");
  return failures ? 1 : 0;
}
