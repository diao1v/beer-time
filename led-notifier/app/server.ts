import { Hono } from "hono";
import mqtt from "mqtt";
import { createHmac, timingSafeEqual } from "node:crypto";

const PORT = Number(process.env.PORT ?? 3000);
const MQTT_URL = process.env.MQTT_URL ?? "mqtt://localhost:1883";
const MQTT_TOPIC = process.env.MQTT_TOPIC ?? "led/command";
const HMAC_SECRET = process.env.TRIGGER_HMAC_SECRET;

const ALLOWED_EVENTS = new Set(["celebrate"]);

const client = mqtt.connect(MQTT_URL, { reconnectPeriod: 2000 });
client.on("connect", () => console.log(`[mqtt] connected ${MQTT_URL}`));
client.on("reconnect", () => console.log("[mqtt] reconnecting..."));
client.on("error", (err) => console.error("[mqtt] error", err.message));

const app = new Hono();

app.get("/health", (c) =>
  c.json({ ok: true, mqtt: client.connected }),
);

app.post("/trigger", async (c) => {
  const raw = await c.req.text();

  if (HMAC_SECRET) {
    const sig = c.req.header("x-hub-signature-256");
    if (!sig || !verifyHmac(raw, sig, HMAC_SECRET)) {
      return c.json({ error: "invalid signature" }, 401);
    }
  }

  let body: unknown;
  try {
    body = JSON.parse(raw);
  } catch {
    return c.json({ error: "invalid json" }, 400);
  }

  const { event, source, message } = (body ?? {}) as {
    event?: unknown;
    source?: unknown;
    message?: unknown;
  };

  if (typeof event !== "string" || !ALLOWED_EVENTS.has(event)) {
    return c.json({ error: `unsupported event: ${String(event)}` }, 400);
  }

  if (!client.connected) {
    return c.json({ error: "mqtt not connected" }, 503);
  }

  await new Promise<void>((resolve, reject) => {
    client.publish(MQTT_TOPIC, event, { qos: 1 }, (err) =>
      err ? reject(err) : resolve(),
    );
  });

  console.log(
    `[trigger] ${event} from ${source ?? "unknown"}: ${message ?? ""}`,
  );
  return c.json({ ok: true });
});

function verifyHmac(raw: string, header: string, secret: string): boolean {
  const expected =
    "sha256=" + createHmac("sha256", secret).update(raw).digest("hex");
  const a = Buffer.from(header);
  const b = Buffer.from(expected);
  return a.length === b.length && timingSafeEqual(a, b);
}

console.log(`[http] listening on :${PORT}`);

export default {
  port: PORT,
  fetch: app.fetch,
};
