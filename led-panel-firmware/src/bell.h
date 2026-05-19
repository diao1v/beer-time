#pragma once

// Bell synthesis + A2DP audio callback.
//
// Source of truth for the sound design lives in tools/bell_synth.py — this
// file mirrors those parameters at the top so the C++ side stays in sync.
// After tweaking the Python script and finding new params + peak, paste the
// values back into the PARAMS section below.

#include <stdint.h>
#include "BluetoothA2DPSource.h"   // for Frame { int16_t channel1, channel2; }

namespace Bell {
  // Call from any task (e.g. MQTT onMessage) to start a single bell playback.
  // Resets the synth to t=0 and lets the A2DP callback take over.
  void trigger();

  // A2DP data callback. Hand this to BluetoothA2DPSource::set_data_callback_in_frames.
  // Returns frame_count; fills `frame[]` with stereo int16 PCM at 44100 Hz.
  int32_t fillFrames(Frame *frame, int32_t frame_count);
}
