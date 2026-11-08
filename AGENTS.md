# Agent Rules for csiGarland

**Canonical agent rules file:** `AGENTS.md`. This file holds the rules. The
other names a tool may look for -- `CLAUDE.md`, `GEMINI.md`, `.cursorrules` --
each contain one `@AGENTS.md` import line and a comment saying so. Edit the rules here;
never edit a stub, and never let one grow a rule of its own.

These rules are mandatory for every code and documentation change in this
repository.

## 0. Project rules

- **Project name:** always write `csiGarland` with exactly this capitalization in
  user-visible text and documentation.
- **Repository:** `https://github.com/ABespalov/csigarland`.
- **License:** Open Source under the **MIT License**.
- **Target hardware:**
  - Microcontroller: ESP32-C6-LCD-1.3 (Waveshare, ESP32-C6FH4 32-bit RISC-V @ 160 MHz)
    with onboard 1.3" IPS LCD (ST7789V2 240x240). One core, so nothing here runs in parallel
    with the Lighting task, only ahead of or behind it.
  - Power supply: Hi-Link 5V 2A (HLK-10M05B, 47x28x22 mm) isolated AC-DC converter.
  - LEDs: 32x WS2812B addressable LEDs (8 unique figurines x 4 copies each) on 9x9x2.5 mm breakout
    boards. The firmware takes chains of up to 256, set by hand or estimated by its probe.
  - Optical diffusers: hollow holiday figurines of the 50 mm height class (about 46-49 mm) printed on Bambu Lab (P2S / P1S / X1C)
    with translucent PETG, mounted with base at top on hanging garland cord.
  - Controls: three push buttons, POWER, EFFECT and SPEED (GPIO with internal pull-up), the
    onboard display, and the web portal.
  - Clock: optional DS3231 module. Everything has to work without it.
  - Power switch: High-side MOSFET module (YZ_CTL_V1) with EN/PWM control from ESP32-C6 GPIO,
    gating the +5V power rail to LEDs during standby to eliminate quiescent draw entirely.
  - Pins and limits have one source, the build flags in `firmware/app/platformio.ini`, but for the
    current limit, whose settings and their defaults are in `firmware/app/src/power_model.h`. Documents
    follow them.
- **Power architecture:** Hardware power-gating via YZ_CTL_V1 MOSFET module for zero standby
  draw on the LED rail, combined with software power clamping to a limit worked out from the
  owner's settings (owner's decision of 2026-11-07): what the supply gives beyond 250 mA for the
  board, less a margin of 5-20 % of that, so (2000 - 250) * 80 % = 1400 mA on the 2000 mA Hi-Link
  at the default 20 %.
- **Releases:** 1.0.0 is the first release and the baseline (owner's decision of 2026-11-03):
  nothing from before it is kept compatible with. From it on, a garland updated to a new release
  keeps what a released build saved, settings, pairings, uploaded effects and its root certificate,
  or the change says what is lost and why; `/api/v1/` changes only by adding. Keep what a change
  would otherwise cost the owner in hand work, such as installing the portal's root certificate
  again on every device, and say so.
- **A closed unit:** the owner's garland is sealed, with no USB to reach. Never answer a problem
  on it with USB. Every update reaches it over OTA, so firmware must always keep a way to take
  the next update, including when HTTPS or a new image has failed.
- **The garland stands alone:** buttons, screen, effects and the schedule work with no network and
  no browser, and a portal failure leaves them working.
- **What the owner chose outranks the schedule:** a choice made by hand is never overridden by a
  schedule step from before it. Only steps missed after it are carried out at a start.
- **File size:** no source file is longer than **1000 lines** -- C, C++, Go,
  Python, PowerShell, HTML/CSS/JS alike, product code and tests. Split by responsibility before
  crossing the limit, not at a line count. `deploy/tests/source-rules.tests.ps1` enforces it. Only a
  third party's file copied whole is left out, and that script names each one.
- **Temporary files:** create scratch data only under root `temp/`.
- **Staged artifacts:** signed update files live under root `assets/`, which git ignores. Refresh
  them with `pwsh -File deploy\stage-assets.ps1 -NewBuild`. Never flash a board or send an update
  unless the owner asks: the owner installs from `assets/firmware`.
- **Versions:** the base version is `CSIGARLAND_FIRMWARE_BASE` in `firmware/app/platformio.ini`, and
  the deploy scripts add the build number. A plain `pio run` keeps the previous number. Ask before
  changing the base. The utility is built apart and counts apart: its base is in
  `cmd/csigarland/version.go`, and `deploy/build-utility.ps1` adds its build number.
- **Profiles:** a build is Release, with no debug information and the plain version, or Dev, with
  debug information and `-dev` on its version. The deploy scripts build Release unless given
  `-BuildProfile Dev`; a plain `pio run` builds Dev. Only Release builds are published.
- **PowerShell:** every tracked `.ps1` states `#requires -Version 7.2` under its
  header and is invoked as `pwsh -File`.
- **Bash:** every tracked `.sh` starts with `#!/usr/bin/env bash` and states `set -euo pipefail`, or,
  sourced from `deploy/lib/`, with `# shellcheck shell=bash`; it is invoked as `bash deploy/<name>.sh`.
  It runs on Linux and on macOS as it comes: bash 3.2 and the BSD tools, so no associative arrays,
  `mapfile`, `${x,,}`, `sed -i`, `readlink -f`, `grep -P`, `flock`, `timeout` or other GNU-only option,
  no `case` inside `$( )`, whose patterns' `)` bash 3.2 takes for its end, and an empty array is expanded
  as `${a[@]+"${a[@]}"}`. What bash cannot do alike on both, digests,
  JSON and binary formats, goes in `deploy/lib/*.py`, under any `python3` from 3.8 on, with nothing
  installed. The source rules enforce what a machine can see of this.
- **Scripts in step across systems:** every script of `deploy/` has a version for each system: a
  PowerShell `.ps1` or `.psm1` for Windows, and a bash `.sh` of the same name for Linux and macOS, its
  modules under `deploy/lib/`. A change to one is made to every version in the same change set: the
  same options, checks, refusals, outputs and files written, and the same digests, manifests and
  counters to the byte, so the versions can take turns in one checkout. Where a system cannot do the
  same, as Windows Credential Manager against the Secret Service, the version says so in its header
  and does the nearest thing. A change made to one version only is not done.
- **Chat language:** Russian.
- **Tracked docs, code, comments, and UI language:** English.
- **Git:** never commit, push, rewrite history, or push to `main` unless the user
  explicitly requests it. Commits must follow Conventional Commits.
- **Tests and docs:** code changes come with their tests and documentation in the same change set.
  Where documents and code disagree, code is the truth until fixed in that change set.
- **Saying what was checked:** host tests, a stubbed page and a build are not the garland. Say which
  of them a change has been through, and what still needs the board.

## 1. Mandatory source headers

Every new or significantly modified native Go or C/C++ source file must start
with the applicable exact-format header.

Go:

```go
// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: [Short, clear description of the file purpose]
// Contributors: [List contributors or state "None"]
```

C/C++:

```cpp
/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: [Short, clear description of the file purpose]
 * Contributors: [List contributors or state "None"]
 */
```

Python and PowerShell carry the same fields as `#` comments.

Preserve third-party copyright and license headers. After a change to the firmware's components,
refresh `THIRD_PARTY_NOTICES.md` with `deploy/new-third-party-notices.ps1`.

## 2. Documentation

| File | Purpose |
|---|---|
| `README.md` | Repository entry point; no hardcoded component versions |
| `CHANGELOG.md` | What a release changed for an owner, against the release before it only |
| `CONTRIBUTING.md` | Setting up, checking and proposing a change; points here for the rules |
| `SECURITY.md` | Reporting a security problem; its accepted limits follow `docs/architecture.md` |
| `docs/architecture.md` | How the firmware works, the portal API, the accepted risks |
| `docs/guides/user-guide.md` | Using a garland |
| `docs/guides/build-and-flash.md` | Building, signing and installing, for a fork too |
| `docs/guides/rebuild.md` | Building a release again, to the byte, and comparing it with the release |
| `docs/guides/effects.md` | Every effect and algorithm, enforced by the source rules |
| `docs/guides/hardware.md`, `docs/hardware/` | Building the hardware, its specification |
| `docs/stubs/ROADMAP.md` | What is planned, and what is left to see working on the board |
| `docs/guides/utility.md` | The csiGarland utility: using it and building it |
| `docs/stubs/TODO.md` | Agreed work that is not done: the applications, and what is left to measure and how |
| `docs/stubs/PUBLIC-API.md` | The public API, `/api/v1/`, that the utility and other programs use |
| `docs/stubs/` | Short names to cite, see below |

**Stubs.** `docs/stubs/` holds no documents: only symbolic links to the real files under `docs/`,
such as `docs/roadmap/`, and a README with the table of them. Plans and to-do lists go in
`docs/roadmap/`. Cross-references cite `docs/stubs/<name>`, never the real path. Edit the target,
never a stub, and do not leave "Moved" pages behind. With `core.symlinks=false`, as on Windows
without the privilege, a stub on disk is a one-line text file holding the target's relative path,
and git has to hold it with mode 120000, which `git add` alone will not do for a new one:
`docs/stubs/README.md` has the command. `deploy/tests/source-rules.tests.ps1` enforces this.

Pictures live under `docs/images/`. Those of the screens, portal pages and utility come from
`deploy/new-doc-images.ps1`, out of the firmware's own drawing code, its real pages and the utility's
own window, with sample data only: never drawn by hand, and remade when a screen or page changes how it looks.

Never place secrets, keys or real passwords in tracked documents or examples.

## 3. C/C++ and ESP32 Firmware Rules

- Prefer modern, type-safe C++ and fixed-size stack storage. Avoid naked `new`,
  `malloc`, and unbounded C string operations.
- Non-blocking state machines: FreeRTOS priority-based multitasking on the ESP32-C6
  RISC-V core: High-priority Lighting task with cycle-accurate RMT hardware output;
  Normal-priority UI rendering (ST7789V2 SPI) and Button gesture debouncing.
- The Lighting task never waits on a lock. What it shares with other tasks is atomic, and what
  belongs together in one action shares one word.
- Never block in an ISR. ISR code uses `IRAM_ATTR` and only `FromISR` FreeRTOS APIs.
- Check hardware initialization results, and handle runtime errors so that the rest keeps working.
- **Never emit non-ASCII bytes on the serial log.** Only plain ASCII (0x20-0x7E)
  characters. No emoji, box drawing, or unicode symbols. The source rules hold firmware sources
  to ASCII.
- Use ESP-IDF logging (`ESP_LOGI/W/E`). Never log the Wi-Fi password, tokens or private keys.
- Use FreeRTOS tasks with `vTaskDelay(pdMS_TO_TICKS(ms))`, not raw tick values.
- Strict power limit in software: clamp LED output to the limit the power settings give, 1400 mA
  on the 2000 mA Hi-Link by default. The settings take only their ranges, the margin never below
  5 %, and no effect or other setting goes around the limit.
- Keep the power sequence: data low, gate high, then the output on the way up; a blackout frame,
  data low, gate low on the way down, and the chain blanked at boot.
- Compile with zero project warnings: the project's own code builds with `-Wall -Wextra -Werror`.
  Do not suppress a warning unless an external library forces it and the reason is written down.
- Effects are written for this project. Names may follow WLED's; code is not taken from it.

## 4. Portal and updates

- Treat everything that comes over the network as hostile: bound its length and its JSON depth, and
  reject what is malformed.
- Every `/api/` route wants a paired browser, but for the two that ask for access, and on the setup
  network, whose password is on the garland's screen. Any new exception is named in
  `docs/architecture.md`. Pairing needs a physical press on the garland.
- The API never returns the Wi-Fi password, a token or a private key.
- Portal pages are self-contained, as the portal may have no internet: no external scripts, styles
  or fonts. Every byte is a byte in flash, so class names stay one letter.
- An update's signature is checked before anything is written to flash, and a new image proves
  itself on trial or the board goes back.
- Keys of the portal's HTTPS are made on the board and never leave it. No image or update file
  carries a private key.
- The risks `docs/architecture.md` lists as accepted, such as no Secure Boot, stay as they are until
  the owner decides otherwise. Do not change one in passing, and do not report one as a defect.

## 5. Security and filesystem safety

- Never hardcode secrets, passwords, API tokens or private keys.
- Never read or print `.env`, credential files, or key material: `cert/` above all, with its keys
  and passphrase, and `deploy/*.local.*`, which holds the token.
- Git tracks only `cert/README.md` of that folder. `deploy/tests/sensitive-data.tests.ps1` enforces it
  and looks for keys anywhere else. A key that reached history is out: replace it.
- Install dependencies only inside the Go module or PlatformIO project, never globally, pin their
  versions, and ask before adding one.
- Ask before destructive filesystem operations, permission changes, or git push.

## 6. Go Rules

- Go 1.26+ and idiomatic standard library.
- Structured logging with `github.com/rs/zerolog`; not `log`, `slog` or `fmt.Printf`.
- Put `context.Context` first on I/O or long-running calls, and give every goroutine a way to end.
- Wrap errors with context using `%w`, and inspect them with `errors.Is/As`.
- Never panic or call `os.Exit` from reusable packages or handlers.
- Use `path/filepath` for filesystem paths.
- Format with `gofmt`. Verify with `pwsh -File deploy\test-utility.ps1`, or `bash deploy/test-utility.sh`
  on Linux and macOS: it runs `go build ./...`,
  `go vet ./...` and `go test -race ./...`, and plain ones could use an engine compiled before the
  firmware last changed.

## 7. Verification order

The firmware build comes first, as the host tests use the cJSON it brings in.

1. `pio run -d firmware/app -e esp32c6 --disable-auto-clean`: the flag keeps the objects through a
   change of `platformio.ini` or of the list of sources, see `docs/guides/build-and-flash.md`.
2. `pio test -d firmware/app -e native`
3. `pwsh -File deploy\tests\source-rules.tests.ps1`, or `bash deploy/tests/source-rules.tests.sh`
4. `pwsh -File deploy\tests\sensitive-data.tests.ps1`, or `bash deploy/tests/sensitive-data.tests.sh`
5. When the signing tool changed, `firmware/app/scripts/test_ota_sign_tool.py`, under the Python
   `Get-csiGarlandSigningPython` in `deploy/SigningPython.psm1` picks. When device detection
   changed, `pwsh -File deploy\tests\partition-entry.tests.ps1`, or its `.sh`. When a script of
   `deploy/` changed, its other versions too, and `bash deploy/tests/shell-lib.tests.sh`, which holds
   the bash scripts' digests to those the PowerShell ones recorded for a release.
6. When a portal page changed, the real page in a browser with the server stubbed: host tests do not
   run its script.
7. When Go changed, the checks of section 6.
8. Confirm that documents and code agree.
