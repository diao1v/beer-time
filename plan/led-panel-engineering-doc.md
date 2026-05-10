# LED Panel Notifier — Engineering Design Document

## Overview

A two-project system that drives a 16×16 HUB75 RGB LED panel connected to an ESP32, displaying celebratory pixel animations when development milestones are reached (e.g., a Jira ticket transitions to "Test OK", or a PR is merged to main).

The system is intentionally split into two independent services to maximise flexibility and replaceability of the trigger source.

---

## Architecture

```
┌──────────────────────────────────────────────────┐
│  Trigger Adapters (one or more)                  │
│                                                  │
│  Project 2: jira-poller                          │
│  - Polls Jira REST API every 30s                 │
│  - Detects status → "Test OK"                    │
│      ↓ POST /trigger (internal HTTP)             │
│                                                  │
│  Future: github-poller, jira-webhook, etc.       │
└──────────────────┬───────────────────────────────┘
                   │ POST /trigger (unified payload)
┌──────────────────▼───────────────────────────────┐
│  Project 1: led-notifier (core service)          │
│                                                  │
│  Hono/Bun HTTP server                            │
│      ↓ validates payload                         │
│  Mosquitto MQTT Broker                           │
│      ↓ publish "led/command"                     │
│  ESP32 (subscribes via LAN)                      │
│      ↓                                           │
│  16×16 HUB75 LED Panel 🎉                        │
└──────────────────────────────────────────────────┘
```

### Design Principles

- **Separation of concerns**: `led-notifier` knows nothing about Jira, GitHub, or any upstream system. It only understands its own trigger protocol.
- **Adapter pattern**: Any new trigger source (GitHub Actions, CI pipeline, Slack command) only needs to POST the unified payload to `led-notifier`. No changes to the core service required.
- **Migration path**: When Jira Webhook admin access becomes available, `jira-poller` can be replaced with a passthrough webhook adapter with zero changes to `led-notifier` or the ESP32 firmware.

---

## Project 1: `led-notifier`

### Responsibilities

- Expose a `/trigger` HTTP endpoint on the local network
- Validate incoming payloads
- Publish MQTT messages to the Mosquitto broker
- Host the Mosquitto broker

### Unified Trigger Payload

All adapters POST to `POST /trigger` with the following JSON body:

```json
{
  "event": "celebrate",
  "source": "jira-poller",
  "message": "PROJ-123 moved to Test OK"
}
```

| Field | Required | Description |
|-------|----------|-------------|
| `event` | Yes | Animation to trigger. Currently: `celebrate` |
| `source` | No | Identifier of the adapter sending the event (for logging) |
| `message` | No | Human-readable description (future: display on panel) |

### Stack

| Component | Technology |
|-----------|------------|
| HTTP server | Bun + Hono |
| MQTT broker | Eclipse Mosquitto 2 (Docker) |
| MQTT client | `mqtt` npm package |

### Security

- The `/trigger` endpoint is **LAN-only** — not exposed to the internet
- No authentication required for internal calls between `jira-poller` and `led-notifier`
- If the endpoint is ever exposed publicly (e.g., via Cloudflare Tunnel for a real Jira Webhook), add HMAC-SHA256 signature verification via `X-Hub-Signature-256` header

### MQTT Topic

| Topic | Publisher | Subscriber | Payload |
|-------|-----------|------------|---------|
| `led/command` | `led-notifier` | ESP32 | `celebrate` |

### Docker Compose Services

```
led-notifier/
├── docker-compose.yml
├── .env
├── app/
│   ├── package.json
│   └── server.ts
└── mosquitto/
    └── config/
        └── mosquitto.conf
```

Services:
- `app` — Bun/Hono server on port 3000 (LAN only, not published externally)
- `mosquitto` — MQTT broker on port 1883 (LAN only)

---

## Project 2: `jira-poller`

### Responsibilities

- Authenticate with the Jira Cloud REST API using a personal API token
- Periodically query for issues that have transitioned to "Test OK"
- Deduplicate results to avoid re-triggering the same issue
- POST to `led-notifier`'s `/trigger` endpoint when a new qualifying issue is found

### Authentication

Generate a Jira API token from `https://id.atlassian.com/manage-profile/security/api-tokens`. No admin permissions required — any account with read access to the project is sufficient.

Requests use HTTP Basic Auth:
```
Authorization: Basic base64(email:api_token)
```

### Polling Strategy

- **Interval**: every 30 seconds
- **JQL query**:
  ```
  status = "Test OK" AND updated >= "-2m" ORDER BY updated DESC
  ```
  The time window (`-2m`) is intentionally larger than the poll interval (30s) to prevent missed events during slow poll cycles.
- **Issue types**: Story, Task (configurable via env var)

### Deduplication

Maintain an in-memory `Set` of issue keys that have already triggered a celebration:

```
seenIssues: Set<string>
  e.g. { "PROJ-123", "PROJ-456" }
```

On each poll:
1. Fetch current "Test OK" issues
2. Filter out keys already in `seenIssues`
3. For each new issue → POST to `/trigger` → add to `seenIssues`
4. Remove keys from `seenIssues` if the issue is no longer in "Test OK" (handles status rollback)

> **Note**: `seenIssues` is in-memory only. A service restart will re-trigger all currently "Test OK" issues once. This is acceptable for this use case; persistence can be added later if needed.

### Stack

| Component | Technology |
|-----------|------------|
| Runtime | Bun |
| HTTP client | `fetch` (built-in) |
| Scheduler | `setInterval` |

### Docker Compose Services

Single service: `poller` — a Bun container running the polling loop.

```
jira-poller/
├── docker-compose.yml
├── .env
└── app/
    ├── package.json
    └── poller.ts
```

### Environment Variables

| Variable | Description |
|----------|-------------|
| `JIRA_BASE_URL` | e.g. `https://your-org.atlassian.net` |
| `JIRA_EMAIL` | Your Atlassian account email |
| `JIRA_API_TOKEN` | Token from id.atlassian.com |
| `JIRA_PROJECT_KEY` | e.g. `PROJ` (optional filter) |
| `JIRA_TARGET_STATUS` | Default: `Test OK` |
| `LED_NOTIFIER_URL` | e.g. `http://led-notifier-app:3000/trigger` |
| `POLL_INTERVAL_MS` | Default: `30000` |

---

## ESP32 Firmware

### Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 (ESP32-S3 recommended for larger RAM) |
| Display | 16×16 HUB75 RGB LED Matrix Panel |
| Connection | WiFi (same LAN as the server) |

### Libraries

| Library | Purpose |
|---------|---------|
| `ESP32-HUB75-MatrixPanel-DMA` | HUB75 panel driver (DMA-based, low CPU overhead) |
| `PubSubClient` | MQTT client |
| `WiFi.h` | WiFi connection (built-in) |

### Firmware Behaviour

| State | Display | Trigger |
|-------|---------|---------|
| Idle | Static standby image (e.g., team logo or colour scan) | — |
| Celebrating | Panda dance animation (N frames, loop 3×) | MQTT message `celebrate` on topic `led/command` |
| Post-celebrate | Returns to idle | Automatically after animation completes |

### MQTT Reconnect Strategy

The `loop()` function checks connection state on every iteration. If disconnected, it attempts reconnection with a 5-second backoff. This ensures the panel recovers automatically from network interruptions without requiring a reboot.

### Pixel Animation Format

Animations are stored as `uint16_t` arrays in RGB565 format, compiled directly into the firmware as C header files (`.h`). Each frame is a flat array of 256 values (16×16 pixels).

```
Frame size: 16 × 16 × 2 bytes = 512 bytes
8 frames:   512 × 8 = 4 KB
```

Well within ESP32 flash capacity.

### Animation Authoring Workflow

1. Draw animation in **Piskel** (free, browser-based) at 16×16 canvas size, 6–10 frames
2. Export as PNG sprite sheet (all frames in a single horizontal strip)
3. Run conversion script (`tools/png_to_rgb565.py`) to generate a `.h` file
4. Include the `.h` file in the Arduino sketch

---

## Deployment

### Infrastructure Overview

Both projects run on the same LAN server as Docker containers. There is no public internet exposure required.

```
Server (LAN)
├── led-notifier/   → docker compose up -d
│   ├── app         (port 3000, LAN only)
│   └── mosquitto   (port 1883, LAN only)
└── jira-poller/    → docker compose up -d
    └── poller      (no exposed ports)
```

The two projects communicate over Docker's default bridge network. If placed in separate Compose projects, `LED_NOTIFIER_URL` should use the server's LAN IP (e.g., `http://192.168.1.x:3000/trigger`) rather than a Docker service name.

### Startup Order

1. Start `led-notifier` first (Mosquitto + Bun server must be up)
2. Start `jira-poller` (will start polling and POSTing immediately)
3. Flash ESP32 firmware, configure WiFi and MQTT broker IP, power on

### Future: Adding a Real Jira Webhook

When Jira admin access is obtained:

1. Create a new adapter project (e.g., `jira-webhook-adapter`)
2. Expose it via Cloudflare Tunnel
3. Receive Jira POST, verify HMAC-SHA256 signature (`X-Hub-Signature-256`)
4. Transform payload → POST to `led-notifier /trigger`
5. Optionally retire `jira-poller`

Zero changes to `led-notifier` or ESP32 firmware required.

---

## Future Enhancements

- **Multiple animations**: different `event` values (`celebrate`, `ship`, `hotfix`) trigger different pixel animations
- **Scrolling text**: display the issue key or PR title on the panel (requires font rendering at 16×16)
- **Buzzer**: connect a passive buzzer to ESP32 for an audio cue on celebration
- **Persistence**: store `seenIssues` to a JSON file so restarts don't re-trigger
- **GitHub adapter**: poll GitHub API for PRs merged to `main`, same POST interface
- **Web UI**: a small dashboard to preview animations and manually trigger events

---

## Open Questions

| Question | Status |
|----------|--------|
| HUB75 panel scan type (1/8 or 1/16)? | Needs hardware confirmation before writing firmware |
| Jira project key and exact status name ("Test OK" vs "Test ok")? | Confirm with team — JQL is case-sensitive for status names |
| Should `jira-poller` filter by assignee or just all tickets? | TBD |
| Standby animation for idle state? | TBD |
