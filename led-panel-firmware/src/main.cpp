#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "secrets.h"
#include "animations/celebrate.h"

// ---------- panel (P2.5-6464-2121-32S, 64x64, 1/32 scan) ----------
#define PANEL_WIDTH  64
#define PANEL_HEIGHT 64
#define PANEL_CHAIN  1

MatrixPanel_I2S_DMA *panel = nullptr;

// ---------- network ----------
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ---------- state ----------
enum State { IDLE, CELEBRATING };
State    state              = IDLE;
uint32_t celebrateStartedAt = 0;

const uint8_t  LOOPS    = 3;
const uint16_t FRAME_MS = 80;
const uint16_t LOOP_MS  = FRAME_MS * CELEBRATE_FRAMES;

uint32_t lastReconnectAttempt = 0;
const uint32_t RECONNECT_BACKOFF_MS = 5000;

// ---------- helpers ----------
static uint16_t hsvToRgb565(uint8_t h, uint8_t s, uint8_t v) {
  // h: 0-255, s: 0-255, v: 0-255 -> RGB565
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

static void drawIdle() {
  // gentle slow hue wash — keeps the panel visibly alive without being noisy
  uint8_t base = (millis() / 60) & 0xFF;
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    for (int x = 0; x < PANEL_WIDTH; x++) {
      uint8_t h = base + (x + y) * 4;
      panel->drawPixelRGB888(x, y, (h >> 1) & 0x3F, 0, ((255 - h) >> 1) & 0x3F);
    }
  }
}

static void drawCelebrateFrame(uint8_t frame) {
  // Diagonal rainbow sweep — placeholder until real pixel art lands.
  for (int y = 0; y < PANEL_HEIGHT; y++) {
    for (int x = 0; x < PANEL_WIDTH; x++) {
      uint8_t h = (x + y) * 8 + frame * 32;
      uint16_t c = hsvToRgb565(h, 255, 255);
      panel->drawPixel(x, y, c);
    }
  }
}

// ---------- mqtt ----------
static void onMessage(char *topic, byte *payload, unsigned int len) {
  String msg;
  msg.reserve(len);
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  Serial.printf("[mqtt] rx %s: %s\n", topic, msg.c_str());

  if (msg == "celebrate") {
    state              = CELEBRATING;
    celebrateStartedAt = millis();
    Serial.println("[state] CELEBRATING");
  }
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
  Serial.println("\n[boot] led-panel-firmware");

  HUB75_I2S_CFG::i2s_pins pins = {
    /* r1  */ 25, /* g1  */ 26, /* b1  */ 27,
    /* r2  */ 14, /* g2  */ 33, /* b2  */ 13,   // G2 remapped 12 -> 33 (avoid boot-strap)
    /* a   */ 23, /* b   */ 19, /* c   */  5,
    /* d   */ 17, /* e   */ 32,
    /* lat */  4, /* oe  */ 15, /* clk */ 16,
  };
  HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN, pins);
  mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
  panel = new MatrixPanel_I2S_DMA(mxconfig);
  panel->begin();
  panel->setBrightness8(80);  // ~30% — easy on the eyes; tune later
  panel->clearScreen();

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
  Serial.println("[state] IDLE");
}

void loop() {
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

  // draw
  if (state == CELEBRATING) {
    uint32_t elapsed = millis() - celebrateStartedAt;
    uint8_t  frame   = (elapsed / FRAME_MS) % CELEBRATE_FRAMES;
    drawCelebrateFrame(frame);

    if (elapsed >= (uint32_t)LOOP_MS * LOOPS) {
      state = IDLE;
      panel->clearScreen();
      Serial.println("[state] IDLE");
    }
  } else {
    drawIdle();
  }
}
