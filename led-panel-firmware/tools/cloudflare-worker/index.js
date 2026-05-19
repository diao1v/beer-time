// OTA file proxy for the LED panel.
//
// Sits in front of a private R2 bucket and gates access with a bearer token.
// Deployed via wrangler; bind R2 bucket as `BUCKET` and set `OTA_TOKEN` as a secret.
//
// Routes:
//   GET /<key>   → Bearer-checked, returns the R2 object at <key>
//                  (e.g. /manifest.json, /firmware.bin)
//
// Anything else → 404.

export default {
  async fetch(request, env) {
    const auth = request.headers.get("Authorization") || "";
    if (auth !== `Bearer ${env.OTA_TOKEN}`) {
      return new Response("unauthorized", { status: 401 });
    }
    if (request.method !== "GET") {
      return new Response("method not allowed", { status: 405 });
    }
    const key = new URL(request.url).pathname.slice(1);
    if (!key) return new Response("not found", { status: 404 });

    const obj = await env.BUCKET.get(key);
    if (!obj) return new Response("not found", { status: 404 });

    return new Response(obj.body, {
      headers: {
        "content-type": obj.httpMetadata?.contentType || "application/octet-stream",
        "cache-control": "no-store",
      },
    });
  },
};
