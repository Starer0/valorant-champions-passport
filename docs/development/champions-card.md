<p align="right"><a href="champions-card.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Champions Shanghai card

This derivative application replaces the baseline hardware-test menu with a black-and-gold, three-page VALORANT Champions Shanghai 2026 display for the 240 × 320 Passport screen. It shows the next match, up to two matches today, and the tournament stage. A live match takes priority on the home page; its large score shows the current map's rounds, while filled dots beneath each team show maps won. A BO3 uses two dots per team and a BO5 uses three. Times are shown in China Standard Time (UTC+8).

The match card pairs each team abbreviation with its mark; today's rows use smaller marks. The header and tournament page use the 2026 Champions mark. The 16 participating team marks are packed into Flash as RGB565A8 images, so displaying them does not require a separate image download. Unknown or future teams fall back to their abbreviation. The marks are sourced from the [official Champions Shanghai roster](https://valorantesports.com/en-US/tournament/115576361459045501/overview) and [official event artwork](https://valorantesports.com/en-US/news/champions-shanghai-everything-you-need-to-know); trademarks belong to their owners. This personal schedule display is not an official Riot or team product. To regenerate the packed file, run `python tools/generate_champions_assets.py` with Pillow installed.

The `SCHEDULE` page keeps today's completed matches and shows their series scores. When today has no matches, it displays the earliest future match day and its date. Team marks retain their original colours and dark outlines; tiles use a light background for predominantly dark marks instead of recolouring them.

## Data and controls

- Schedule: VLR.gg's [event calendar subscription](https://www.vlr.gg/event/ical/2766), which supplies UTC start times and teams. The calendar removes completed matches. Known matches from today and yesterday are retained, and recent results are recovered from the match list. After a restart, missing start times come from the exact `data-utc-ts` field on the match details page. A schedule refresh makes at most four extra date-recovery requests; cached dates avoid repeating them. Failed recovery keeps the previous complete snapshot and uses the existing retry path.
- Match state and series score: VLR.gg's [event match list](https://www.vlr.gg/event/matches/2766/valorant-champions-2026/). Current map rounds and BO3/BO5 format come from the corresponding public [match details page](https://www.vlr.gg/753444/). This is an unofficial source whose HTML and update timing may change. Until round data is available, the live card displays `--:--` and `ROUNDS SYNCING` instead of a fabricated score. If the match list fails, the last complete card remains visible and the footer shows `DATA ERROR`; a failed page never replaces the card with partial data.
- Optional backup: HenrikDev's [VLR match details API](https://docs.henrikdev.xyz/api-reference/valorant/get-vlr-match-details-v2) is queried when the direct VLR match list or live details request fails. It is based on VLR data, so it does not provide an independent timing guarantee. The user can save or remove their API key through the Passport's local setup portal; the key stays in device NVS and is never built into the firmware. Without a key, the device simply retries VLR. On-device testing confirmed a completed match's 2:0 series score through HenrikDev; live map-round freshness remains unverified. Direct VLR remains primary because its map-round detail was observed while the HenrikDev response for that completed match had no round scores.
- Updates: the schedule and series state refresh every ten minutes normally, every two minutes when the next match is within an hour, and every 60 seconds during a live match. Live map rounds refresh separately, five seconds after the preceding request completes. All requests share one worker, so schedule downloads, network latency, and source updates can extend the visible interval. The two refresh paths back off independently after failures (15, 30, 60, then 120 seconds), keeping prior data. A changed series score clears the preceding map's cached rounds. Double-clicking OK requests an immediate refresh of both paths.
- UP/DOWN: previous/next page. Manual selection remains for 20 seconds, then the home page gets the longest automatic dwell.
- Short OK: pause/resume rotation. `LOCK` replaces the footer dots while paused; score updates and manual page navigation continue. Resuming starts a fresh dwell on the current page. The lock does not persist across restart. OK is the third function button from the top on the right; the separate power button is on the left.
- Double OK: immediate data refresh, replacing the previous short-press refresh action.
- Long OK: open Wi-Fi setup again.
- To configure the backup without changing saved Wi-Fi: long-press OK, join the temporary `Passport-VCT-*` access point shown on the screen, open `http://192.168.4.1`, and use **Save API key**. Saving closes the setup portal and resumes the existing Wi-Fi connection. The portal can also remove the key.

No Wi-Fi credentials are compiled into the firmware. On first boot, the screen displays a temporary `Passport-VCT-*` access point and a random password. Connect a phone to it, open `http://192.168.4.1`, and enter the name and password of a 2.4 GHz Wi-Fi network. The portal closes after the Passport obtains an IP address. The portal also offers a way to forget saved credentials. On network loss, the last successful schedule remains visible with an offline or stale indicator.

The UI uses ASCII/Latin labels and the configured Montserrat fonts. No Chinese UI strings are currently drawn, so there is no Chinese glyph requirement in this build. Inspect every page on the actual display before accepting font size, contrast, and clipping.

## Build and device acceptance

Use ESP-IDF 5.5.3 and the repository validation gate. The verified merged image belongs at Flash offset `0x0`; writing it may reset stored NVS data, including Wi-Fi settings. Do not flash without the owner's approval. On the device, check Wi-Fi setup, time synchronization, all three pages, buttons, live/final transitions during a real match, and data-source failure handling. A firmware build does not establish these physical results.

### Validation checkpoint: 2026-09-28

- Build: PASS (ESP-IDF 5.5.3, ESP32-C3, 8 MB; merged image and archive verified).
- Host tests: PASS (Champions model, HenrikDev parser, packed assets, repository checks).
- Device tests: PASS for approved application-only flashing on COM7, initial schedule retrieval, user-confirmed team marks and three-page navigation, and a 90-second serial observation with no observed crash. Wi-Fi and API-key storage were preserved by leaving NVS untouched.
- Unverified: actual live five-second polling, map transitions, and live backup rounds. The complete gate was subsequently run in WSL; see the follow-up below.
- Application SHA-256: `5DD4EEC3AEA59EDE42F41C4DEEC52DAFD90208FB54602E311EDFDBE1C5EDB091`.
- Merged-image SHA-256 / archive directory under `build/firmware/`: `7741CBC5AE4BF005B8E117DE49B1CA3855B2E5A40AD3D6447F29B88FC6407BD2`.
- Matching ELF SHA-256: `8a25d60ceb3b474b42899505b5751336a062a8d24b2d3118f11d522a99624071`.

### WSL validation follow-up: 2026-09-28

- Environment: Ubuntu 26.04.1 on WSL2, Python 3.14.4, ESP-IDF v5.5.3. Linux tools and the validation checkout are separate from the native Windows environment. WSL uses mirrored networking and automatic proxy discovery.
- Build: PASS (`./tools/validate.sh`, including isolated configuration, merged-image verification, and matching debug archive).
- Host tests: PASS (the complete repository gate, including actionlint and all host tests).
- Device tests: NOT RUN for this Linux image. The earlier Windows image's device results above remain separate; this follow-up did not flash the board.
- Unverified: USB forwarding into WSL, actual live five-second polling, map transitions, and live backup rounds.
- The validated source snapshot and `dependencies.lock` matched the original checkout. The exported Linux image is kept at `build/wsl-validation/FoloToy-AI-Passport-full.bin`, with its verified bundle at `build/firmware/c2e59268c9432bb2e91964b93f84a55c121015a8fc8424ffc29b416f1d6df1a8/`. The native Windows build remains intact.
- Application SHA-256: `fd499cf2ad70c7d5c6d0dda28425a980b1379f56a0afd4567464e47487afc1ed`.
- Merged-image SHA-256: `c2e59268c9432bb2e91964b93f84a55c121015a8fc8424ffc29b416f1d6df1a8`.
- Matching ELF SHA-256: `a80cdc892a092b522772a8052e38ee6e0918045a00ae88f42cd7cfd57b9d7533`.

### Original-colour marks, page lock, and match-day schedule: 2026-10-01

- Accepted behavior: preserve official mark colours, short OK pauses/resumes rotation, double OK refreshes, and `SCHEDULE` retains today's final series scores or displays the next match day. UP/DOWN continue to work while locked; score updates continue. Long OK retains the setup entry.
- Build: PASS (`./tools/validate.sh`, ESP-IDF 5.5.3, isolated WSL build, merged image and debug archive verified).
- Host tests: PASS (complete gate; regressions cover dark Liquid/TYLOO pixels, a completed match removed from the calendar, exact UTC start recovery, date selection, cache retention, and lock/navigation/resume timing).
- Device tests: PASS for the observed checks: authorized application-only flash to COM7, write hash verification, and user-confirmed team marks, lock/resume, and schedule display. Wi-Fi and API Key storage were not written. Two bounded serial windows produced no log lines, so runtime health was not inferred from serial silence.
- Unverified: double/long button actions, actual completed-match restoration after restart, live day transitions, and live backup rounds. The user accepted retaining the light backgrounds for dark mark details; no replacement image was flashed.
- Application SHA-256: `7f23a3aad312ee367a7ba33a50d190cde0668d2da4973d3970a6453543668f6b`.
- Merged image / archive identity: `f6154755e45a53fd534c275a31b5cfd05c270b43c28e3accb195ed0f4161070d`.
- Matching ELF SHA-256: `870d47730b8d496a53d60bb283d46713d1aad4571c9e23bf0797312ff5ba4f31`.

