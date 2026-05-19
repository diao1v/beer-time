#include "bell.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

// ============================================================================
// PARAMS — mirror of tools/bell_synth.py. After tweaking the Python script,
// listening to tools/preview/bell.wav, and being happy with the result, copy
// the new values here (and rebuild + OTA).
// ============================================================================

namespace {

constexpr int   SAMPLE_RATE             = 44100;
constexpr float FUNDAMENTAL_HZ          = 740.0f;
constexpr float STRIKE_INTERVAL_S       = 0.30f;
constexpr float STRIKE_DURATION_S       = 1.20f;
constexpr float LAST_STRIKE_DURATION_S  = 2.80f;
constexpr float LAST_STRIKE_DECAY_MULT  = 0.55f;
constexpr int   RING_COUNT              = 6;
constexpr float ATTACK_S                = 0.003f;   // 3 ms
constexpr float MASTER_VOLUME           = 0.85f;
constexpr float PEAK_NORMALIZE          = 3.160417f; // run `python3 tools/bell_synth.py` to recompute

struct Partial {
  float ratio;
  float amp;
  float decay;   // per second
};

// Inharmonic bell partials — non-integer ratios are what makes it sound like
// struck metal rather than an electronic chime.
constexpr Partial PARTIALS[] = {
  // ratio,  amp,   decay
  { 0.50f,   0.45f,  2.5f },
  { 1.00f,   1.00f,  4.0f },
  { 1.183f,  0.65f,  5.5f },   // tierce — the inharmonic minor third (bell character)
  { 1.506f,  0.45f,  6.5f },
  { 2.00f,   0.55f,  5.5f },
  { 2.61f,   0.30f,  9.0f },
  { 3.84f,   0.18f, 13.0f },
  { 5.42f,   0.10f, 20.0f },
};
constexpr int N_PARTIALS = sizeof(PARTIALS) / sizeof(PARTIALS[0]);

constexpr float TAU = 6.28318530717958647692f;   // 2π. Arduino.h already #defines TWO_PI.

// Total length of the bell sequence in seconds.
constexpr float TOTAL_DURATION_S =
  (RING_COUNT - 1) * STRIKE_INTERVAL_S + LAST_STRIKE_DURATION_S;
constexpr uint32_t TOTAL_FRAMES = (uint32_t)(TOTAL_DURATION_S * SAMPLE_RATE);

// Shared state between Bell::trigger() (writer) and Bell::fillFrames (reader).
// Two volatile word-sized fields = atomic on ESP32. Writer sets framesEmitted
// first, then playing — reader checks playing first, so it never sees a stale
// frame counter.
volatile uint32_t framesEmitted = 0;
volatile bool     playing       = false;

inline float synthSampleAt(float t_global) {
  float sample = 0.0f;
  for (int s = 0; s < RING_COUNT; s++) {
    float onset = s * STRIKE_INTERVAL_S;
    if (t_global < onset) continue;
    float t_strike = t_global - onset;
    bool isFinal = (s == RING_COUNT - 1);
    float strike_dur = isFinal ? LAST_STRIKE_DURATION_S : STRIKE_DURATION_S;
    if (t_strike >= strike_dur) continue;
    float decay_mult = isFinal ? LAST_STRIKE_DECAY_MULT : 1.0f;

    // Accumulate this strike's partials, then apply its attack envelope.
    float strike_sum = 0.0f;
    for (int p = 0; p < N_PARTIALS; p++) {
      float freq = FUNDAMENTAL_HZ * PARTIALS[p].ratio;
      strike_sum += PARTIALS[p].amp
                  * sinf(TAU * freq * t_strike)
                  * expf(-PARTIALS[p].decay * decay_mult * t_strike);
    }
    float attack = (t_strike < ATTACK_S) ? (t_strike / ATTACK_S) : 1.0f;
    sample += strike_sum * attack;
  }
  return sample;
}

} // anonymous namespace

namespace Bell {

void trigger() {
  // Reset counter BEFORE flipping the flag — reader observes a consistent state.
  framesEmitted = 0;
  playing       = true;
}

int32_t fillFrames(Frame *frame, int32_t frame_count) {
  if (!playing) {
    memset(frame, 0, frame_count * sizeof(Frame));
    return frame_count;
  }

  const uint32_t startIdx = framesEmitted;
  constexpr float INV_SR = 1.0f / SAMPLE_RATE;

  for (int32_t i = 0; i < frame_count; i++) {
    uint32_t frameIdx = startIdx + i;
    if (frameIdx >= TOTAL_FRAMES) {
      frame[i].channel1 = 0;
      frame[i].channel2 = 0;
      continue;
    }
    float t = frameIdx * INV_SR;
    float s = synthSampleAt(t) / PEAK_NORMALIZE * MASTER_VOLUME;
    if (s >  1.0f) s =  1.0f;
    if (s < -1.0f) s = -1.0f;
    int16_t v = (int16_t)(s * 32767.0f);
    frame[i].channel1 = v;   // L
    frame[i].channel2 = v;   // R (mono → both channels)
  }

  framesEmitted = startIdx + frame_count;
  if (framesEmitted >= TOTAL_FRAMES) {
    playing = false;
  }
  return frame_count;
}

} // namespace Bell
