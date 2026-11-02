# Changelog

All notable changes to the `csiGarland` project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versions are not Semantic Versioning: a release is a base, such as `1.0.0`, and the firmware and
the utility each add their own build number to it, such as `1.0.0.102` and `1.0.0.27`. Updates
tell builds apart by the hashes of what they hold.

## [Unreleased]

## [1.0.0] - 2026-11-03

The first release: firmware 1.0.0.102 and the csiGarland utility 1.0.0.27 for Windows. It is the baseline, and nothing from before it is kept compatible with.

### Firmware

- 32 WS2812B LEDs by default, chains of up to 256 set by hand or estimated by a probe on the gated rail, with the 1400 mA limit applied to every frame and the power sequence of data low, gate, output.
- 154 effects in groups, from 101 algorithms in up to four layers, in an effects pack apart from the firmware with room for 512, favourites for the EFFECT button, and up to 32 effects uploaded or previewed from files.
- Three buttons, POWER, EFFECT and SPEED, with brightness steps, the off countdown and pairing; the screen in the csiPass design, with screensavers.
- Home Wi-Fi, a setup network with its password on the screen, a captive portal and mDNS; NTP time, an optional DS3231 clock module, and a schedule of up to 32 actions in the garland's time zone, where a choice made by hand outranks the steps before it.
- HTTPS with each garland's own root and certificates, made on the board, and browsers and programs paired by a press on the garland.
- Signed updates of firmware, effects or both under the owner's private root, checked before anything is written, with a trial after which the garland confirms itself or goes back, and plain HTTP left as a way back should HTTPS fail.

### Portal

- Effects, Schedule, Device, Network, Firmware and Info pages, self-contained, with glyph and word buttons, and the root certificate to install.

### Public API

- `/api/v1/` for programs: finding a garland by mDNS, pinning its root, pairing for a token, every setting the portal has, update files, and the garland's log for a unit with no USB.

### Utility

- The csiGarland utility for Windows, in Go and Gio: everything the portal does, Tune, an editor of every field of an effect with a preview drawn by the firmware's own engine, updates from a file or the online catalog of releases, an offer of a newer release as soon as it connects, and the log.

### Site

- [garland.csilab.am](https://garland.csilab.am): the product, the roadmap, the documents for owners and for builders, and the downloads, with the catalog of firmware releases the utility reads.

### Build and release

- Two build profiles: Release, with no debug information and the plain version, which is what is published, and Dev, with debug information and `-dev` on its version.
- Built again to the byte from the same sources, firmware and utility, as `docs/guides/rebuild.md` says, with deterministic signatures, a pinned toolchain and a check of two builds side by side.
- Hardware documents, a bill of materials and the 3D models of the enclosure and figurines.
