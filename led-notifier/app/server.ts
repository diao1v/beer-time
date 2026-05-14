import { DEFAULT_ANIMATION, parseAnimationMap } from "./src/animation-map.ts";
import { config } from "./src/config.ts";
import { createMqttClient } from "./src/mqtt-client.ts";
import { createApp } from "./src/routes.ts";

const animationMap = parseAnimationMap(config.animationMapRaw);
console.log(
  `[boot] animation map: ${animationMap.size} entries ` +
    `(default=${animationMap.get("*") ?? DEFAULT_ANIMATION})`,
);

const mqtt = createMqttClient(
  config.mqttUrl,
  config.mqttUsername,
  config.mqttPassword,
);

const app = createApp({
  mqtt,
  mqttTopic: config.mqttTopic,
  animationMap,
  hmacSecret: config.hmacSecret,
});

console.log(`[http] listening on :${config.port}`);

export default {
  port: config.port,
  fetch: app.fetch,
};
