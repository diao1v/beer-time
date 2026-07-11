#pragma once

// I2S celebration chime for a MAX98357A-class amplifier.
//
// Wiring (ESP32 -> amp module):
//   BCLK      -> GPIO18
//   LRCLK/WS  -> GPIO21
//   DIN (SDA) -> GPIO22
//
// Uses I2S peripheral 0 (I2S_NUM_0). The HUB75 panel driver uses I2S_NUM_1 on
// the classic ESP32, so the two peripherals do not conflict.
//
// A background task feeds I2S continuously: silence when idle (keeps the amp
// quiet — valid clock + zero data — instead of amplifying floating-input hum),
// and a short synthesized chime when triggered.

namespace Bell {
  // Install the I2S driver and start the audio task. Call once from setup().
  void begin();

  // (Re)start the celebration chime from the beginning. Safe to call from the
  // MQTT callback; the audio task picks it up on its next buffer.
  void trigger();
}
