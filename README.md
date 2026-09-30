<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# VALORANT Champions Pocket Card

A black-and-gold VALORANT Champions Shanghai 2026 companion for FoloToy AI Passport. Browse the next match, match-day schedule, and tournament stage. During live matches, large numbers show the current map's round score, while dots under each team mark track maps won: two dots for BO3 and three for BO5. Times use China Standard Time (UTC+8).

This is an unofficial fan project based on [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport). Event and team trademarks belong to their owners.

## Quick start

1. On first startup, the screen shows `WIFI SETUP`. Have an internet-connected 2.4 GHz Wi-Fi network ready.
2. Join the displayed `Passport-VCT-*` hotspot on a phone or computer using the displayed password. Open `http://192.168.4.1`, enter the Wi-Fi name and password, and select **Connect**. The hotspot closes and the match card appears after successful connection.
3. Use UP/DOWN to switch pages. Briefly press OK, the third function button from the top on the right, to pause or resume automatic rotation. `LOCK` appears while paused; scores continue updating and UP/DOWN still work. Double-click OK to refresh immediately.
4. Hold OK until `WIFI SETUP` appears to change networks. Saved Wi-Fi reconnects after restart.
5. Optionally save a HenrikDev API key in the same setup portal with **Save API key** to enable the backup source. No API key is required for the primary source.

## Match data

VLR.gg supplies the schedule, match state, and round scores. HenrikDev is an optional backup also based on VLR data. Live round requests wait five seconds after the previous request completes; live schedule requests run every 60 seconds. Network latency and source updates affect the visible refresh rate. Failed requests retain the last successful data and retry with backoff; unavailable round scores show `--:--`.

Wi-Fi credentials and API keys are stored on the device, never compiled into the firmware. See the [application guide](docs/development/champions-card.md) for data sources, controls, and validation details.

## Build and firmware

Use ESP-IDF 5.5.3 with the ESP32-C3 target and 8 MB Flash. Follow the [environment guide](docs/development/engineering/environment-setup.md), then run:

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

Download the merged image from GitHub Releases and write it at `0x0` only with the device owner's approval. A full-image write may reset saved Wi-Fi and API-key settings. Generated firmware is distributed as a release asset rather than committed to source control.

## Validation

The complete ESP-IDF build and host-test gate passed on 2026-10-01. The merged image has SHA-256 `f6154755e45a53fd534c275a31b5cfd05c270b43c28e3accb195ed0f4161070d`. The device-tested release asset is `VALORANT-Champions-v1.1.0-full.bin`; use its accompanying checksums to identify this exact image.

The matching application was flashed while preserving saved settings. The owner confirmed team marks, page lock/resume, and schedule display. Live five-second polling, map transitions, live backup round scores, completed-match restoration after reboot, and double/long button actions remain unverified for this revision. See the [validation record](docs/development/champions-card.md#build-and-device-acceptance).

## Project structure

- `main/champions_*`: application UI, match model, parsers, and packed team marks.
- `main/passport_net.*`: networking and local setup portal.
- `components/bsp`: reusable board support from the upstream project.
- `tests`: host checks; `tools/validate.sh`: validation entry point.
- [Upstream documentation](docs/README.md) and [repository guidelines](AGENTS.md).

Retain the upstream [license](LICENSE). Event and team artwork is attributed in the application guide and remains subject to its owners' rights.
