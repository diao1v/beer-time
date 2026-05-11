import { Hono } from "hono";
import { pickAnimation } from "./animation-map.ts";
import { verifyHmac } from "./hmac.ts";
import type { MqttPublisher } from "./mqtt-client.ts";

const ALLOWED_EVENTS = new Set(["celebrate"]);

export interface RouteDeps {
  mqtt: MqttPublisher;
  mqttTopic: string;
  animationMap: Map<string, string>;
  hmacSecret: string | undefined;
}

export function createApp(deps: RouteDeps): Hono {
  const { mqtt, mqttTopic, animationMap, hmacSecret } = deps;
  const app = new Hono();

  app.get("/health", (c) =>
    c.json({
      ok: true,
      mqtt: mqtt.connected(),
      animations: Object.fromEntries(animationMap),
    }),
  );

  app.post("/trigger", async (c) => {
    const raw = await c.req.text();

    if (hmacSecret) {
      const sig = c.req.header("x-hub-signature-256");
      if (!sig || !verifyHmac(raw, sig, hmacSecret)) {
        return c.json({ error: "invalid signature" }, 401);
      }
    }

    let body: unknown;
    try {
      body = JSON.parse(raw);
    } catch {
      return c.json({ error: "invalid json" }, 400);
    }

    const { event, source, message, developer } = (body ?? {}) as {
      event?: unknown;
      source?: unknown;
      message?: unknown;
      developer?: unknown;
    };

    if (typeof event !== "string" || !ALLOWED_EVENTS.has(event)) {
      return c.json({ error: `unsupported event: ${String(event)}` }, 400);
    }

    if (!mqtt.connected()) {
      return c.json({ error: "mqtt not connected" }, 503);
    }

    const dev = typeof developer === "string" ? developer : undefined;
    const animation = pickAnimation(animationMap, dev);

    await mqtt.publish(mqttTopic, animation);

    console.log(
      `[trigger] ${event} from ${source ?? "unknown"} ` +
        `dev=${dev ?? "-"} → animation=${animation} : ${message ?? ""}`,
    );
    return c.json({ ok: true, animation });
  });

  return app;
}
