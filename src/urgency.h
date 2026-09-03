// urgency.h — the border-colour rule, and the user-adjustable bounds it reads.
//
// This lives in its own header because two otherwise unrelated halves of the
// firmware need the same numbers and must never disagree about them: config.h
// stores the pair in NVS and normalises it, board_model.cpp applies it to a
// countdown. A literal 10 written in both places would drift the first time one
// of them changed.
//
// Header-only and free of Arduino, so the rule is exercised on the host by
// test_board_model and test_config rather than only discovered on the panel.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Border colour, from the urgency rule this project was built around.
enum Urgency {
  URGENCY_GREEN,   // no hurry
  URGENCY_YELLOW,  // start moving
  URGENCY_RED,     // go now
};

// Both bounds are EXCLUSIVE minute counts, and both are read the same way, so
// the setup page can label them identically ("red when under N minutes").
//
//   minutesAway < redUnder                 -> red
//   redUnder <= minutesAway < yellowUnder  -> yellow
//   minutesAway >= yellowUnder             -> green
//
// yellowUnder == redUnder collapses the yellow band, giving a red/green-only
// sign. That is a deliberate setting, not an error — the same reading config.h
// gives an equal bright-window start and end.
struct UrgencyThresholds {
  uint8_t redUnder;
  uint8_t yellowUnder;
};

// The shipped defaults, and the values an existing device gets when it takes an
// update that adds these keys to NVS. 10 and 16 reproduce exactly the rule the
// sign had when the thresholds were compiled in: under 10 red, 10 to 15
// inclusive yellow, over 15 green. Changing these changes what a *new* device
// does out of the box, and nothing else.
inline constexpr UrgencyThresholds kUrgencyDefaults{10, 16};

// Bounds enforced by both the setup form and configSanitise().
//
// Zero is excluded because a red band of "under 0 minutes" is one no countdown
// can ever fall inside — the colour would simply never appear, with no visible
// cause. 60 is the far end of useful: this sign shows the next three departures
// and Caltrain's headways make an hour of warning the whole board already.
inline constexpr uint8_t URGENCY_MIN_MINUTES = 1;
inline constexpr uint8_t URGENCY_MAX_MINUTES = 60;

// The border colour for a whole-minute countdown. Exposed so the display, the
// portal's preview text and the tests all share one definition.
inline Urgency urgencyFor(int32_t minutesAway, const UrgencyThresholds& t) {
  // Red is tested first, so an inverted pair that somehow escaped sanitising
  // degrades to "more red" rather than to a colour that never appears.
  if (minutesAway < (int32_t)t.redUnder) return URGENCY_RED;
  if (minutesAway < (int32_t)t.yellowUnder) return URGENCY_YELLOW;
  return URGENCY_GREEN;
}

// Write the three bands this pair produces into `out`, as a sentence for the
// setup form. Two exclusive "under N" numbers are easy to type and hard to
// picture, and the sign has no other way to show what was just set.
//
// The conversion from exclusive bounds to an inclusive band is the reason this
// is here rather than inline in portal.cpp: yellow runs to yellowUnder - 1, and
// that subtraction is unreachable from any host test behind #ifdef ARDUINO.
// Entities are HTML — the only caller is the portal page.
inline void urgencyBandsText(const UrgencyThresholds& t, char* out, size_t cap) {
  if (t.yellowUnder <= t.redUnder) {
    // Naming the collapsed case beats printing a band that runs backwards.
    snprintf(out, cap, "Now: red under %u min, green from %u on &mdash; no yellow band.",
             (unsigned)t.redUnder, (unsigned)t.redUnder);
  } else {
    snprintf(out, cap, "Now: red under %u min, yellow %u&ndash;%u, green from %u on.",
             (unsigned)t.redUnder, (unsigned)t.redUnder,
             (unsigned)(t.yellowUnder - 1), (unsigned)t.yellowUnder);
  }
}

// Clamp a pair into range and normalise an inverted one. Applied on config load
// and save, so a corrupt NVS record or a hand-crafted POST cannot put the
// display into a state the colour rule does not expect.
inline void urgencySanitise(UrgencyThresholds& t) {
  if (t.redUnder < URGENCY_MIN_MINUTES) t.redUnder = URGENCY_MIN_MINUTES;
  if (t.redUnder > URGENCY_MAX_MINUTES) t.redUnder = URGENCY_MAX_MINUTES;
  if (t.yellowUnder < URGENCY_MIN_MINUTES) t.yellowUnder = URGENCY_MIN_MINUTES;
  if (t.yellowUnder > URGENCY_MAX_MINUTES) t.yellowUnder = URGENCY_MAX_MINUTES;

  // Raise yellow to meet red rather than lowering red to meet yellow: the user
  // asked for a red band of a particular width, and honouring that while losing
  // the yellow one is closer to the request than quietly shrinking red.
  if (t.yellowUnder < t.redUnder) t.yellowUnder = t.redUnder;
}
