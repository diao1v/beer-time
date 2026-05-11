export const config = {
  port: Number(process.env.PORT ?? 3000),
  mqttUrl: process.env.MQTT_URL ?? "mqtt://localhost:1883",
  mqttTopic: process.env.MQTT_TOPIC ?? "led/command",
  hmacSecret: process.env.TRIGGER_HMAC_SECRET,
  animationMapRaw: process.env.DEVELOPER_ANIMATION_MAP ?? "",
} as const;
