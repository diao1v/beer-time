#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
#include "fonts/Jersey25.h"
#include <time.h>

#include "secrets.h"
#include "animations/panda2.h"
#include "animations/marcel.h"
#include "animations/david.h"
#include "animations/richard.h"
#include "animations/taylor.h"
#include "animations/clock_bg.h"

// Auckland NZ timezone: NZST = UTC+12, NZDT = UTC+13.
// DST: starts last Sun Sep at 02:00 → 03:00, ends first Sun Apr at 03:00 → 02:00.
static const char *TZ_AUCKLAND = "NZST-12NZDT,M9.5.0/2,M4.1.0/3";

// ---------- panel (P2.5-6464-2121-32S, 64x64, 1/32 scan) ----------
#define PANEL_WIDTH  64
#define PANEL_HEIGHT 64
#define PANEL_CHAIN  1

MatrixPanel_I2S_DMA *panel = nullptr;

// Off-screen canvas for the clock text. Rendered once per minute, then
// composited over the bg during idle so we only "reserve" the lit pixels.
// Press Start 2P 5pt is ~6 wide × 7 tall per glyph, line-height 10.
static const uint8_t CLOCK_TEXT_W = 64;
static const uint8_t CLOCK_TEXT_H = 17;
GFXcanvas16 *clockCanvas = nullptr;

// ---------- network ----------
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ---------- celebration timing ----------
const uint32_t CELEBRATE_MS = 10000;       // total time on each celebration
const uint16_t FRAME_MS     = 60;          // ~16 fps

uint32_t lastReconnectAttempt = 0;
const uint32_t RECONNECT_BACKOFF_MS = 5000;

// ---------- helpers ----------
static uint16_t hsvToRgb565(uint8_t h, uint8_t s, uint8_t v) {
  uint8_t region = h / 43;
  uint8_t rem    = (h - (region * 43)) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  uint8_t r, g, b;
  switch (region) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Forward decl — drawGifFrame is defined further down with the gif animations.
static void drawGifFrame(uint32_t elapsed, const uint16_t *frames,
                         const uint16_t *delaysMs, uint16_t frameCount,
                         uint16_t totalMs, uint8_t w, uint8_t h, int16_t &lastIdx);

// ---------- idle: clock composited over animated background ----------
// We render "HH:MM" into a tiny in-RAM canvas using the Tom Thumb 3x5 font,
// then during the bg blit we sample the canvas: for non-black canvas pixels,
// write the text color; otherwise write the bg gif pixel. Each panel pixel
// is written exactly once per redraw, so there's no race vs the DMA scan
// (no flicker), and only the actual lit text pixels are reserved.

static void renderClockToCanvas(const char *timeText) {
  clockCanvas->fillScreen(0);
  clockCanvas->setTextSize(1);
  clockCanvas->setTextColor(0xFFFF);
  clockCanvas->setFont(&Jersey25);
  clockCanvas->setCursor(3, 16);
  clockCanvas->print(timeText);
}

static void drawIdleFrame(uint32_t elapsed) {
  // Pick current bg frame. With double-buffering we draw every loop iteration
  // because the back buffer holds stale content from two flips ago.
  uint32_t t = elapsed % CLOCK_BG_TOTAL_MS;
  uint16_t idx = 0;
  uint32_t acc = 0;
  for (uint16_t i = 0; i < CLOCK_BG_FRAMES; i++) {
    uint16_t d = pgm_read_word(&clock_bgDelaysMs[i]);
    acc += d;
    if (t < acc) { idx = i; break; }
  }
  const uint16_t *frame = &clock_bgFrames[idx][0];

  for (uint8_t y = 0; y < CLOCK_BG_H; y++) {
    for (uint8_t x = 0; x < CLOCK_BG_W; x++) {
      uint16_t bg_px = pgm_read_word(&frame[y * CLOCK_BG_W + x]);
      uint16_t text_px = (y < CLOCK_TEXT_H && x < CLOCK_TEXT_W)
                            ? clockCanvas->getPixel(x, y)
                            : 0;
      panel->drawPixel(x, y, text_px ? text_px : bg_px);
    }
  }
}

static void drawIdle() {
  static int lastMin = -1;

  time_t now_s = time(nullptr);
  if (now_s < 1700000000) {
    if (lastMin != -2) {
      renderClockToCanvas("....");
      lastMin = -2;
    }
  } else {
    struct tm t;
    localtime_r(&now_s, &t);
    if (t.tm_min != lastMin) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
      renderClockToCanvas(buf);
      lastMin = t.tm_min;
    }
  }

  drawIdleFrame(millis());
}

// ---------- animations ----------
static void drawRainbow(uint32_t elapsed) {
  uint8_t frame = elapsed / FRAME_MS;
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    for (int x = 0; x < PANEL_WIDTH; x++) {
      uint8_t h = (x + y) * 4 + frame * 6;
      panel->drawPixel(x, y, hsvToRgb565(h, 255, 255));
    }
  }
}

static void drawFireworks(uint32_t elapsed) {
  // Up to 5 concurrent burst origins; each ring expands over ~600ms then fades.
  struct Burst { int x, y; uint32_t t0; uint8_t hue; };
  static Burst bursts[5];
  static bool   inited = false;
  static uint32_t lastSpawn = 0;
  if (!inited) {
    for (int i = 0; i < 5; i++) bursts[i].t0 = 0;
    inited = true;
  }
  uint32_t now = millis();
  if (now - lastSpawn > 250) {
    lastSpawn = now;
    for (int i = 0; i < 5; i++) {
      if (now - bursts[i].t0 > 700) {
        bursts[i].x  = 8 + (rand() % (PANEL_WIDTH - 16));
        bursts[i].y  = 8 + (rand() % (PANEL_HEIGHT - 16));
        bursts[i].t0 = now;
        bursts[i].hue = rand() & 0xFF;
        break;
      }
    }
  }
  panel->fillScreenRGB888(0, 0, 0);
  for (int i = 0; i < 5; i++) {
    uint32_t age = now - bursts[i].t0;
    if (age > 700) continue;
    int radius = age / 30;      // expands ~1 px / 30ms
    if (radius > 18) continue;
    uint8_t fade = 255 - (age * 255 / 700);
    uint16_t c = hsvToRgb565(bursts[i].hue, 255, fade);
    // ring (Bresenham-ish, coarse but fast)
    for (int a = 0; a < 360; a += 8) {
      float rad = a * 0.01745329f;
      int px = bursts[i].x + (int)(cosf(rad) * radius);
      int py = bursts[i].y + (int)(sinf(rad) * radius);
      if (px >= 0 && px < PANEL_WIDTH && py >= 0 && py < PANEL_HEIGHT)
        panel->drawPixel(px, py, c);
    }
  }
  (void)elapsed;
}

static void drawHearts(uint32_t elapsed) {
  // 8x8 heart sprite, pulsing in size + brightness
  static const uint8_t heart8[8] = {
    0b01100110,
    0b11111111,
    0b11111111,
    0b11111111,
    0b01111110,
    0b00111100,
    0b00011000,
    0b00000000,
  };
  panel->fillScreenRGB888(20, 0, 0);
  uint8_t phase = (elapsed / 40) & 0xFF;
  // triangle wave 0..127..0 → pulse 180..243
  uint8_t tri   = (phase < 128) ? phase : (uint8_t)(255 - phase);
  uint8_t pulse = 180 + (tri >> 1);
  // Tile center heart with subtle pulse around the center.
  int cx = PANEL_WIDTH / 2 - 4;
  int cy = PANEL_HEIGHT / 2 - 4;
  // background scattered small hearts
  for (int gy = 4; gy < PANEL_HEIGHT - 8; gy += 16) {
    for (int gx = 4; gx < PANEL_WIDTH - 8; gx += 16) {
      if (gx == cx && gy == cy) continue;
      for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 8; i++) {
          if (heart8[j] & (0x80 >> i))
            panel->drawPixelRGB888(gx + i, gy + j, 120, 0, 30);
        }
      }
    }
  }
  // pulsing big heart in the center
  for (int j = 0; j < 8; j++) {
    for (int i = 0; i < 8; i++) {
      if (heart8[j] & (0x80 >> i)) {
        panel->drawPixelRGB888(cx + i, cy + j, pulse, 30, 30);
      }
    }
  }
}

static void drawConfetti(uint32_t elapsed) {
  // Falling colored particles. Re-seeded periodically for randomness.
  struct P { int x; int y; int8_t vy; uint8_t hue; };
  const int N = 40;
  static P parts[N];
  static bool inited = false;
  if (!inited) {
    for (int i = 0; i < N; i++) {
      parts[i].x   = rand() % PANEL_WIDTH;
      parts[i].y   = rand() % PANEL_HEIGHT;
      parts[i].vy  = 1 + (rand() % 2);
      parts[i].hue = rand() & 0xFF;
    }
    inited = true;
  }
  panel->fillScreenRGB888(0, 0, 0);
  static uint32_t lastTick = 0;
  uint32_t now = millis();
  bool step = (now - lastTick) > 50;
  if (step) lastTick = now;
  for (int i = 0; i < N; i++) {
    if (step) {
      parts[i].y += parts[i].vy;
      if (parts[i].y >= PANEL_HEIGHT) {
        parts[i].y   = 0;
        parts[i].x   = rand() % PANEL_WIDTH;
        parts[i].hue = rand() & 0xFF;
      }
    }
    panel->drawPixel(parts[i].x, parts[i].y, hsvToRgb565(parts[i].hue, 255, 255));
  }
  (void)elapsed;
}

// ---------- gif playback ----------
// Blits the current gif frame to the panel. With double-buffering enabled the
// back buffer is stale across flips, so we redraw every loop iteration.
static void drawGifFrame(
    uint32_t elapsed,
    const uint16_t *frames,
    const uint16_t *delaysMs,
    uint16_t frameCount,
    uint16_t totalMs,
    uint8_t w, uint8_t h,
    int16_t &lastIdx) {
  (void)lastIdx;
  if (totalMs == 0) return;
  uint32_t t = elapsed % totalMs;
  uint16_t idx = 0;
  uint32_t acc = 0;
  for (uint16_t i = 0; i < frameCount; i++) {
    uint16_t d = pgm_read_word(&delaysMs[i]);
    acc += d;
    if (t < acc) { idx = i; break; }
  }
  const uint16_t *frame = frames + (uint32_t)idx * w * h;
  for (uint8_t y = 0; y < h; y++) {
    for (uint8_t x = 0; x < w; x++) {
      uint16_t c = pgm_read_word(&frame[y * w + x]);
      panel->drawPixel(x, y, c);
    }
  }
}

static void drawPanda2(uint32_t elapsed) {
  static int16_t lastIdx = -1;
  drawGifFrame(elapsed, &panda2Frames[0][0], panda2DelaysMs,
               PANDA2_FRAMES, PANDA2_TOTAL_MS, PANDA2_W, PANDA2_H, lastIdx);
}

static void drawMarcel(uint32_t elapsed) {
  static int16_t lastIdx = -1;
  drawGifFrame(elapsed, &marcelFrames[0][0], marcelDelaysMs,
               MARCEL_FRAMES, MARCEL_TOTAL_MS, MARCEL_W, MARCEL_H, lastIdx);
}

static void drawDavid(uint32_t elapsed) {
  static int16_t lastIdx = -1;
  drawGifFrame(elapsed, &davidFrames[0][0], davidDelaysMs,
               DAVID_FRAMES, DAVID_TOTAL_MS, DAVID_W, DAVID_H, lastIdx);
}

static void drawRichard(uint32_t elapsed) {
  static int16_t lastIdx = -1;
  drawGifFrame(elapsed, &richardFrames[0][0], richardDelaysMs,
               RICHARD_FRAMES, RICHARD_TOTAL_MS, RICHARD_W, RICHARD_H, lastIdx);
}

static void drawTaylor(uint32_t elapsed) {
  static int16_t lastIdx = -1;
  drawGifFrame(elapsed, &taylorFrames[0][0], taylorDelaysMs,
               TAYLOR_FRAMES, TAYLOR_TOTAL_MS, TAYLOR_W, TAYLOR_H, lastIdx);
}

// ---------- star border overlay ----------
// A rotating-color 3x3 sparkle border that frames whatever's underneath.
// Inspired by Circus Charlie's title screen.
static void drawStarBorder(uint32_t elapsed) {
  static const uint16_t palette[] = {
    0x07FF,   // cyan
    0xFFE0,   // yellow
    0xF800,   // red
    0x07E0,   // green
  };
  const uint8_t palN = sizeof(palette) / sizeof(palette[0]);

  // 3x3 plus/sparkle sprite. Bit 0..8 → (x,y) = (i%3, i/3).
  static const uint8_t sprite[9] = {
    0,1,0,
    1,1,1,
    0,1,0,
  };

  const uint8_t slotOffset = (elapsed / 250) & 0xFF;
  // Black out the 3-pixel ring before drawing sprites, so old sprites don't smear.
  for (int x = 0; x < PANEL_WIDTH; x++) {
    for (int t = 0; t < 3; t++) {
      panel->drawPixel(x, t,                       0);
      panel->drawPixel(x, PANEL_HEIGHT - 1 - t,    0);
    }
  }
  for (int y = 3; y < PANEL_HEIGHT - 3; y++) {
    for (int t = 0; t < 3; t++) {
      panel->drawPixel(t,                    y, 0);
      panel->drawPixel(PANEL_WIDTH - 1 - t,  y, 0);
    }
  }

  auto stamp = [&](int sx, int sy, uint16_t color) {
    for (int dy = 0; dy < 3; dy++) {
      for (int dx = 0; dx < 3; dx++) {
        if (sprite[dy * 3 + dx]) panel->drawPixel(sx + dx, sy + dy, color);
      }
    }
  };

  // Walk the perimeter clockwise starting from top-left, giving each star a
  // continuous index. Then color = palette[(idx + slotOffset) % palN] — as
  // slotOffset advances, each star inherits its predecessor's color, making
  // the colors appear to rotate clockwise around the frame.
  const int SPACING = 5;
  int idx = 0;
  // Top row (left → right)
  for (int x = 0; x < PANEL_WIDTH - 2; x += SPACING) {
    stamp(x, 0, palette[(idx + slotOffset) % palN]);
    idx++;
  }
  // Right column (top → bottom), skip the corner we already drew
  for (int y = SPACING; y < PANEL_HEIGHT - 2; y += SPACING) {
    stamp(PANEL_WIDTH - 3, y, palette[(idx + slotOffset) % palN]);
    idx++;
  }
  // Bottom row (right → left), skip the corner already drawn
  for (int x = PANEL_WIDTH - 3 - SPACING; x >= 0; x -= SPACING) {
    stamp(x, PANEL_HEIGHT - 3, palette[(idx + slotOffset) % palN]);
    idx++;
  }
  // Left column (bottom → top), skip both corners
  for (int y = PANEL_HEIGHT - 3 - SPACING; y >= SPACING; y -= SPACING) {
    stamp(0, y, palette[(idx + slotOffset) % palN]);
    idx++;
  }
}

// ---------- animation registry ----------
typedef void (*AnimFn)(uint32_t);
struct Animation { const char *name; AnimFn draw; bool withBorder; };
static const Animation ANIMATIONS[] = {
  { "rainbow",   drawRainbow,   false },
  { "fireworks", drawFireworks, false },
  { "hearts",    drawHearts,    false },
  { "confetti",  drawConfetti,  false },
  { "panda2",    drawPanda2,    true  },
  { "marcel",    drawMarcel,    true  },
  { "david",     drawDavid,     true  },
  { "richard",   drawRichard,   true  },
  { "taylor",    drawTaylor,    true  },
};
static const size_t ANIM_COUNT = sizeof(ANIMATIONS) / sizeof(ANIMATIONS[0]);

static const Animation *lookupAnimation(const String &name) {
  for (size_t i = 0; i < ANIM_COUNT; i++) {
    if (name.equalsIgnoreCase(ANIMATIONS[i].name)) return &ANIMATIONS[i];
  }
  return &ANIMATIONS[0];  // unknown name → first entry (rainbow)
}

// ---------- state ----------
enum State { IDLE, CELEBRATING };
State              state              = IDLE;
uint32_t           celebrateStartedAt = 0;
const Animation *  currentAnim        = &ANIMATIONS[0];
bool               borderOverride     = false;   // set by "-ws" suffix on trigger name

// ---------- mqtt ----------
static void onMessage(char *topic, byte *payload, unsigned int len) {
  String msg;
  msg.reserve(len);
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  Serial.printf("[mqtt] rx %s: %s\n", topic, msg.c_str());

  // "-ws" suffix on the trigger name forces the rotating-star border on,
  // overriding the animation's registry default.
  String baseName = msg;
  borderOverride = false;
  if (baseName.endsWith("-ws")) {
    borderOverride = true;
    baseName = baseName.substring(0, baseName.length() - 3);
  }

  currentAnim        = lookupAnimation(baseName);
  state              = CELEBRATING;
  celebrateStartedAt = millis();
  Serial.printf("[state] CELEBRATING (%s%s)\n", currentAnim->name,
                (borderOverride || currentAnim->withBorder) ? " +border" : "");
}

static bool reconnectMQTT() {
  Serial.printf("[mqtt] connecting to %s:%d...\n", MQTT_HOST, MQTT_PORT);
  if (mqtt.connect(MQTT_CLIENT_ID)) {
    mqtt.subscribe(MQTT_TOPIC);
    Serial.printf("[mqtt] connected, subscribed %s\n", MQTT_TOPIC);
    return true;
  }
  Serial.printf("[mqtt] connect failed, rc=%d\n", mqtt.state());
  return false;
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(200);
#ifdef DISCOVERY_MODE
  Serial.println("\n[boot] led-panel-firmware (DISCOVERY MODE - panda loop)");
#else
  Serial.println("\n[boot] led-panel-firmware");
#endif

  HUB75_I2S_CFG::i2s_pins pins = {
    /* r1  */ 25, /* g1  */ 26, /* b1  */ 27,
    /* r2  */ 14, /* g2  */ 33, /* b2  */ 13,   // G2 remapped 12 -> 33 (avoid boot-strap)
    /* a   */ 23, /* b   */ 19, /* c   */  5,
    /* d   */ 17, /* e   */ 32,
    /* lat */  4, /* oe  */ 15, /* clk */ 16,
  };
  HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN, pins);
  mxconfig.clkphase = false;
  mxconfig.double_buff = true;  // safer with FM6126A init
  panel = new MatrixPanel_I2S_DMA(mxconfig);
  panel->begin();
  clockCanvas = new GFXcanvas16(CLOCK_TEXT_W, CLOCK_TEXT_H);
  panel->setBrightness8(50);
  panel->clearScreen();

#ifdef DISCOVERY_MODE
  Serial.println("[discovery] skipping WiFi/MQTT; looping panda");
  return;
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[wifi] connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.printf("\n[wifi] connected, ip=%s\n", WiFi.localIP().toString().c_str());

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  reconnectMQTT();

  // NTP — configTzTime is non-blocking; first valid time arrives ~1-3s later.
  configTzTime(TZ_AUCKLAND, "pool.ntp.org", "time.google.com");
  Serial.println("[ntp] sync requested");
  Serial.println("[state] IDLE");
}

void loop() {
#ifdef DISCOVERY_MODE
  uint32_t t = millis();
  drawDavid(t);
  drawStarBorder(t);
  panel->flipDMABuffer();
  return;
#endif

  // network maintenance — non-blocking, max one attempt every RECONNECT_BACKOFF_MS
  if (WiFi.status() != WL_CONNECTED) {
    uint32_t now = millis();
    if (now - lastReconnectAttempt > RECONNECT_BACKOFF_MS) {
      lastReconnectAttempt = now;
      Serial.println("[wifi] reconnecting...");
      WiFi.reconnect();
    }
  } else if (!mqtt.connected()) {
    uint32_t now = millis();
    if (now - lastReconnectAttempt > RECONNECT_BACKOFF_MS) {
      lastReconnectAttempt = now;
      reconnectMQTT();
    }
  } else {
    mqtt.loop();
  }

  if (state == CELEBRATING) {
    uint32_t elapsed = millis() - celebrateStartedAt;
    currentAnim->draw(elapsed);
    if (currentAnim->withBorder || borderOverride) drawStarBorder(elapsed);
    if (elapsed >= CELEBRATE_MS) {
      state = IDLE;
      panel->clearScreen();
      Serial.println("[state] IDLE");
    }
  } else {
    drawIdle();
  }
  panel->flipDMABuffer();
}
