#pragma once

// Bell synthesis was for the A2DP path — currently disabled.
// Keeping this file as a stub so the build still references a known
// header; once we move to DFPlayer Mini or another audio approach,
// the trigger() API can be re-purposed (trigger an MP3 play, etc.).
//
// Source-of-truth sound design lives in tools/bell_synth.py.

namespace Bell {
  inline void trigger() {}   // no-op until audio backend is re-wired
}
