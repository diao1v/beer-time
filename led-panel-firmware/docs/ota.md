# OTA updates

The panel can flash itself over WiFi — no USB needed after the initial setup.

## Release flow

```bash
# 1. Make your changes locally, commit, push.
# 2. Tag the commit you want to release:
git tag v0.1.1
git push origin v0.1.1
```

That's it. The `.github/workflows/release.yml` workflow runs, builds the firmware with `FIRMWARE_VERSION=v0.1.1` baked in, creates a GitHub Release with `firmware.bin` attached, and publishes an MQTT message to `led/firmware/update`. The panel picks up the message, downloads the new firmware, applies it, and reboots — usually within 30 seconds of the tag push.

On reboot, the panel briefly displays **"OTA OK / v0.1.1"** in green so you have visual confirmation.

## What the panel actually does

1. MQTT message arrives on `led/firmware/update` (any payload triggers a check).
2. Panel fetches `https://api.github.com/repos/diao1v/beer-time/releases/latest`.
3. Parses `tag_name`; compares to its baked-in `FIRMWARE_VERSION`.
4. If different, finds the `firmware.bin` asset URL, downloads it, applies via `httpUpdate`.
5. Reboots into the new firmware; the post-boot banner confirms success.

## Manual trigger

Useful if you want the panel to re-check without pushing a new tag:

```bash
mosquitto_pub \
  -h 7306418854a8415482b46390241b69a5.s1.eu.hivemq.cloud -p 8883 \
  -u jira-led -P '<password>' \
  --cafile /etc/ssl/cert.pem \
  -t led/firmware/update -m ""
```

If the panel is already on the latest release, you'll see `[ota] already on latest` in the serial log and nothing else happens.

## Rollback

To roll back, push a tag pointing at the old commit. The panel then sees the new "latest" tag is different from its current version and updates (down) to it.

```bash
git tag -f v0.1.2 <old-commit-sha>
git push origin v0.1.2 --force
```

(Or simply tag the old commit with a higher version number.)

## Required GitHub secrets

In the repo's **`production` environment** (Settings → Environments → production → Secrets), add:

| Name | Value |
|---|---|
| `MQTT_HOST` | `7306418854a8415482b46390241b69a5.s1.eu.hivemq.cloud` |
| `MQTT_USER` | `jira-led` |
| `MQTT_PASS` | the HiveMQ password |

`GITHUB_TOKEN` is auto-provided to the workflow.

## Constraints

- **Binary size**: must stay under ~2 MB (specifically 2064384 bytes per slot). The custom partition table at `partitions_ota.csv` gives two 2016 KB app slots — SPIFFS is dropped entirely since the firmware doesn't use it. If the build ever overflows, the simplest relief is to swap to a shorter `clock_bg` gif (the 97-frame default is ~776 KB of the binary).
- **Initial flash is still USB**: the very first OTA-capable build must be flashed over USB once. After that, OTA handles everything.
- **TLS validation**: currently uses `setInsecure()` for HTTPS — same as the MQTT TLS setup. Traffic is encrypted but server identity isn't strictly verified. Acceptable for hobby use; would harden later by pinning the Let's Encrypt root.

## Failure modes

| Symptom | Cause | What happens |
|---|---|---|
| Workflow fails at build step | Code didn't compile | Release isn't created; panel doesn't update |
| Workflow fails at MQTT step | HiveMQ creds wrong, broker offline | Release exists but panel doesn't auto-update — trigger manually |
| Panel logs `[ota] http error` | Panel temporarily lost internet or rate-limited (unlikely) | Panel stays on current firmware, no harm |
| Download finishes but image is corrupt | Network glitch mid-write | `httpUpdate` aborts, old slot stays active, panel reboots into old firmware |
| New firmware crashes early | Bug in the new image | Currently no automatic rollback — the panel will reboot-loop into the new (broken) firmware. Push a new fixed release to recover. (Adding `esp_ota_mark_app_valid_cancel_rollback()` after a successful boot would harden this; deferred.) |

## One-time setup checklist

Before the first tagged release:

1. USB-flash the OTA-capable firmware once: `pio run -e esp32dev -t upload`.
2. Confirm `[ota] FIRMWARE_VERSION=dev-local` shows in the boot log.
3. Add the three GitHub secrets above to the `production` environment.
4. Push your first tag: `git tag v0.1.0 && git push origin v0.1.0`.
5. Watch the Actions tab; once the workflow finishes, the panel should reboot into `v0.1.0` and display the green confirmation banner.
