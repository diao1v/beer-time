# OTA updates

The panel flashes itself over WiFi — no USB needed after initial setup.

## Release flow

```bash
git tag v0.1.1
git push origin v0.1.1
```

That's it. The `.github/workflows/release.yml` workflow:

1. Builds the firmware with `FIRMWARE_VERSION=v0.1.1` baked in.
2. Uploads `firmware.bin` and `manifest.json` to a private Cloudflare R2 bucket via the S3-compatible API.
3. Publishes an MQTT message to `led/firmware/update` on HiveMQ.

The panel listens for that message, fetches `manifest.json` from the OTA URL with its bearer token, sees the version is newer than its own, downloads `firmware.bin`, applies, reboots — and shows **"OTA OK / v0.1.1"** in green for ~3 seconds.

## Why R2 instead of GitHub Releases?

- The repo can stay **public** — code is transparent.
- The firmware binary is **gated by a bearer token**; only the Worker and the panel have it.
- If the token leaks, blast radius is just the OTA bucket. Source code, MQTT broker, etc. are unaffected.

## One-time Cloudflare setup

### 1. Sign up for Cloudflare (free)

https://dash.cloudflare.com/sign-up

### 2. Create the R2 bucket

Dashboard → R2 → **Create bucket**:
- Name: `led-panel-ota` (or whatever; remember it).
- Location: default.
- Leave public access **disabled**.

### 3. Create R2 API token for CI uploads

R2 → **Manage R2 API Tokens** → **Create API token**:
- Permission: **Object Read & Write**.
- Specify bucket: `led-panel-ota`.
- TTL: pick something reasonable; CI will use this token to upload.

Save the `Access Key ID`, `Secret Access Key`, and the S3 endpoint URL (`https://<account-id>.r2.cloudflarestorage.com`). These go into GitHub secrets.

### 4. Generate the OTA bearer token

```bash
openssl rand -hex 32
```

Save the output — this is the token the panel will send when fetching from R2 (via the Worker).

### 5. Deploy the Worker

Install wrangler if you haven't:
```bash
npm install -g wrangler
wrangler login
```

Then from the worker directory:
```bash
cd led-panel-firmware/tools/cloudflare-worker
wrangler secret put OTA_TOKEN
# Paste the token from step 4 when prompted.
wrangler deploy
```

Wrangler prints the worker URL: `https://led-panel-ota.<your-subdomain>.workers.dev`. Save that — it goes into a GitHub secret.

### 6. Add GitHub secrets and variables

In Settings → Environments → `production`:

**Variables** (`vars.*`):
| Name | Value |
|---|---|
| `WIFI_SSID` | your 2.4 GHz network name |
| `WIFI_PASSWORD` | WiFi password (CF doesn't treat this as a secret since vars are visible to maintainers; if you want it hidden from log diffs, move it to secrets) |
| `R2_BUCKET` | `led-panel-ota` |

**Secrets** (`secrets.*`):
| Name | Value |
|---|---|
| `MQTT_HOST` | HiveMQ cluster URL |
| `MQTT_USER` | `jira-led` |
| `MQTT_PASS` | HiveMQ password |
| `R2_ACCESS_KEY_ID` | from step 3 |
| `R2_SECRET_ACCESS_KEY` | from step 3 |
| `R2_ENDPOINT` | from step 3 (the `https://...r2.cloudflarestorage.com` URL) |
| `OTA_BASE_URL` | the Worker URL from step 5 (no trailing slash) |
| `OTA_TOKEN` | the bearer token from step 4 |

## The manifest format

`manifest.json` is tiny:
```json
{ "version": "v0.1.1" }
```

The panel compares this against its baked-in `FIRMWARE_VERSION`. Different = update. Same = no-op.

If you ever want to add fields (e.g. SHA256, release notes, downgrade flag), the parser is forgiving — extra keys are ignored.

## Manual trigger

If you want the panel to re-check the manifest without pushing a new tag:

```bash
mosquitto_pub \
  -h <hivemq-host> -p 8883 \
  -u jira-led -P '<password>' \
  --cafile /etc/ssl/cert.pem \
  -t led/firmware/update -m ""
```

## Rollback

R2 keeps whatever was last uploaded. To roll back:
- Re-tag an old commit, push the tag → CI rebuilds + uploads the old firmware → panel downgrades.
- Or manually overwrite `manifest.json` + `firmware.bin` in the R2 bucket via the dashboard or `wrangler r2 object put`.

## Token rotation

When the OTA bearer token needs rotation:

```bash
# Generate new token
NEW=$(openssl rand -hex 32)

# Push to Worker
cd led-panel-firmware/tools/cloudflare-worker
echo -n "$NEW" | wrangler secret put OTA_TOKEN
wrangler deploy

# Update GitHub secret OTA_TOKEN with $NEW (via dashboard)

# Tag a release; CI bakes the new token into the firmware
git tag v0.x.y
git push origin v0.x.y
```

Caveat: the in-flight panel still has the OLD token. It receives the MQTT trigger from CI, tries to fetch the manifest with the OLD token, gets 401. Two ways to handle:
- Rotate just before a planned release (panel still has old, valid token, fetches once, updates to new firmware which has the new token).
- Keep both old and new tokens accepted on the Worker temporarily (modify Worker to accept an array).

For a single panel this is rarely an issue.

## Constraints

- **Binary size**: under 2031616 bytes (1984 KB). Custom partition table at `partitions_ota.csv` gives two slots of that size.
- **First flash is still USB**: the initial OTA-capable firmware needs USB. After that, OTA handles everything.
- **TLS validation**: panel uses `setInsecure()` for HTTPS — encrypted but no strict cert check. Acceptable here since the bearer token provides server-side authentication (a MITM with a valid cert would still need the token).

## Failure modes

| Symptom | Cause | What happens |
|---|---|---|
| Workflow fails at R2 upload | Bad R2 creds or bucket name | Release isn't published; panel doesn't update |
| Workflow fails at MQTT step | HiveMQ creds wrong | Release sits in R2 but panel doesn't auto-update — trigger manually |
| Panel logs `[ota] manifest fetch failed: 401` | Token mismatch between panel and Worker | Confirm `OTA_TOKEN` in firmware secrets.h matches Worker's secret |
| Panel logs `[ota] manifest fetch failed: 0` | Network drop / TLS failure | Panel stays on current firmware, retry later |
| `httpUpdate` returns nonzero with checksum error | Corrupt download / partial bytes | Old slot stays active, panel reboots into previous firmware |
| New firmware crashes early on boot | Bug in the new image | No automatic rollback (deferred); push a new fix release |
