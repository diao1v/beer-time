import mqtt, { type MqttClient } from "mqtt";

export interface MqttPublisher {
  client: MqttClient;
  connected(): boolean;
  publish(topic: string, payload: string): Promise<void>;
}

export function createMqttClient(
  url: string,
  username?: string,
  password?: string,
): MqttPublisher {
  const client = mqtt.connect(url, {
    reconnectPeriod: 2000,
    username,
    password,
    rejectUnauthorized: false,
  });
  client.on("connect", () => console.log(`[mqtt] connected ${url}`));
  client.on("reconnect", () => console.log("[mqtt] reconnecting..."));
  client.on("error", (err) => console.error("[mqtt] error", err.message));

  return {
    client,
    connected: () => client.connected,
    publish: (topic, payload) =>
      new Promise<void>((resolve, reject) =>
        client.publish(topic, payload, { qos: 1 }, (err) =>
          err ? reject(err) : resolve(),
        ),
      ),
  };
}
