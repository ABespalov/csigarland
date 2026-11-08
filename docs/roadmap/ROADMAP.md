# Project Roadmap: csiGarland

Milestones leading to Christmas & New Year deployment. A ticked firmware item is implemented and passes the host tests or builds; what has been seen working on the owner's garland is listed separately in Milestone 3. Agreed work that is not done, the applications and what is left to measure among it, is in the [to-do list](../stubs/TODO.md).

```mermaid
flowchart LR
    M1["M1: Proof of Concept"] --> M2["M2: 3D Prototypes"]
    M2 --> M3["M3: Firmware on the Board"]
    M3 --> M4["M4: 32-Unit Build"]
    M4 --> M5["M5: Holiday Deployment"]
```

---

### Milestone 1: Electrical Proof-of-Concept & Power Baseline
- [ ] Verify Hi-Link 5V 2A output under test load.
- [ ] Verify the MOSFET gate on the LED rail and measure standby draw.
- [ ] Check a garland on the gated rail against the LED count estimate, and calibrate it.

### Milestone 2: 3D Modeling & Print Optimization
- [ ] Print test pieces of the figurines in translucent PETG; evaluate light dispersion and snap-fit.
- [x] Design and print an enclosure for the Hi-Link 220V converter and ESP32 controller.

### Milestone 3: Firmware
Implemented:
- [x] RMT output, MOSFET sequencing, blanking at every boot, a current limiter: 1400 mA up to 1.1.1, and from 1.4.0 the limit the power settings give, 1400 mA by default.
- [x] Effect format, grouped catalog, favourites and 154 effects, with room for 256, in an effects pack of their own (format 2) beside the firmware.
- [x] Effects uploaded and previewed from files, up to 32.
- [x] HTTPS with each garland's own certificates, and browsers paired at the buttons.
- [x] Three buttons with click and long-press gestures, and the off countdown.
- [x] Screen in the csiPass design, with screensavers.
- [x] Web portal, home Wi-Fi, setup access point, captive portal, mDNS.
- [x] NTP time, an optional DS3231 clock module, and a schedule of up to 32 actions.
- [x] OTA package staged under `assets/` by `deploy/stage-assets.ps1`.
- [x] Encrypted settings and a per-board setup network password.
- [x] Signed OTA under a private root certificate, with automatic return to the previous firmware.

Seen working on the owner's garland:
- [x] Screen, gestures and screensavers, with no LED flicker from drawing or Wi-Fi.
- [x] Portal, captive portal and mDNS from a phone.
- [x] NTP sync and scheduled actions.
- [x] The clock module: found, kept through a power cut, set from each source.
- [x] How all 154 effects look on real figurines: eight, one of each kind, on 1.0.0.102 and the builds of 1.1.0.
- [x] An OTA update, a refused bad signature, and a return to the previous firmware, on the same builds.

Not implemented: see **Planned** below.

### Milestone 4: Full Garland Assembly (32 Figurines)
- [ ] Solder a continuous 32-node cable harness.
- [x] Fix the physical order of the figurines along the cable, and match the layout in the [Effects Guide](../guides/effects.md) to it: the firmware of 1.1.1.
- [ ] Batch print 32 base mounts and 32 holiday diffuser caps.
- [ ] Snap LEDs into bases, secure strain-relief channels, and attach diffuser caps.

### Milestone 5: Acceptance Testing & Tree Installation
- [ ] 24-hour thermal and stability burn-in test.
- [ ] Final current consumption verification across all animation modes.
- [ ] Installation on the Christmas tree.

## Released

* **1.3.0**, released 2026-11-06 (utility 1.3.0.42 for macOS on Apple silicon, 1.3.0.43 for macOS on Intel, 1.3.0.44 for Windows, 1.3.0.45 for Linux; the firmware is 1.1.1): the utility alone, the first for macOS, seen against the owner's garland on Apple silicon; the Intel file has not been run. The [changelog](../../CHANGELOG.md) has each.
* **1.1.1**, released 2026-11-06 (firmware 1.1.1.114): the firmware alone, with the figurines' new order and colours, and what it does when its settings cannot be read at a start, each part read again and nothing saved over it. Tested on the host and the Device page in a browser against the stubbed portal, and seen working on the owner's garland; those reads failing have not been brought about on it. A garland updated from 1.1.0 keeps all it saved. The [changelog](../../CHANGELOG.md) has each.
* **1.2.0**, released 2026-11-05 (utility 1.2.0.38 for Windows, 1.2.0.39 for Linux; the firmware stays 1.1.0.113): the utility alone, the first for Linux, tested on the host on Windows and on Ubuntu, the Linux file built in a Docker container of Ubuntu 24.04 on Windows. Not yet seen against the garland on Linux. The [changelog](../../CHANGELOG.md) has each.
* **1.1.0**, released 2026-11-04 (firmware 1.1.0.113, utility 1.1.0.37): what is listed below, tested on the host and in a browser against the stubbed portal, and seen working on the owner's garland. A garland updated from 1.0.0 keeps all it saved. The [changelog](../../CHANGELOG.md) has each:

* **Playlists and transitions:** up to 16 effects played in turn, and a cross-fade at every change of effect.
* **Fades on turning on and off,** and a scheduled turn on that rises from dark over a minute.
* **A check after an update:** the utility reads every route of `/api/v1/` and holds each answer to its types.
* **Backup and restore:** the schedule, favourites, playlist, screen and uploaded effects to a file and back, for a unit that may have to be erased.
* **The schedule on a timeline:** the next 24 hours, and where actions meet, on the portal and in the utility.
* **Current in Tune:** what an effect draws by the firmware's own model, and where the 1400 mA limit holds it back.
* **Push updates:** the garland sends its state as it changes, to the portal and the utility.
* **1.0.0**, released 2026-11-03 (firmware 1.0.0.102, utility 1.0.0.27): everything ticked in Milestone 3, the portal, the public API and the Windows utility, built again to the byte from its sources.

## In the Next Release, 1.4.0

The firmware and the utility both, as 1.4.0: 1.2.0 was a release of the utility alone.

* **The current limit set by the owner:** on the Device page of the portal and the utility, the power supply's current, 500-4400 mA, what one LED draws at full white and dark, and a margin of 5-20 % of what the supply gives beyond the board's 250 mA, from which the firmware works out the limit, at most (4400 - 250) * 95 % = 3942 mA, under the 4.3 A of the MOSFET module, and the page what the chain draws at full white and how many LEDs that leaves undimmed. The defaults keep the 1400 mA limit, with 40 mA an LED at full white, from one LED measured at 36.1 mA, where 1.1.1 assumed 55 mA. Saved settings that are not valid give the most cautious ones, a 500 mA supply, 100 mA white, 5 mA dark and a 20 % margin, a 200 mA limit, with a warning on the Device page until they are saved again. Tested on the host and the Device page in a browser against the stubbed portal; not yet on the garland.
* **A trial confirmed by an answer:** a new firmware confirms itself only once it has answered a request over the network, as the portal, the utility and `ota-update` send while they wait, within 10 minutes, and otherwise goes back to the firmware before; after an update, the portal and the utility show their Info page. Not yet on the garland.
* **The Scheduler24 screensaver,** for a garland with the clock module: what comes next and how soon, the schedule's next step within a day or the playlist's next effect, or what happened last and how long ago. Tested on the host, its pictures drawn by the firmware's own drawing code; not yet on the garland.
* **The preview and Tune on the garland's own LED count,** and the current charted for it, the part past the limit in red. Tested on the host and in pictures of the window; not yet against the garland.
* **What reviews of this release found,** in the reading of saved settings and uploaded effects after a start that could not read them, and in backups and restores: the [changelog](../../CHANGELOG.md) has each.


## Planned

Not implemented, and not to be read as what the firmware or the utility does:

* Go + Gio applications beyond Windows, Linux and macOS, on the [public API](../stubs/PUBLIC-API.md): see the [to-do list](../stubs/TODO.md#applications).

## Open Hardware Questions

Neither confirmed nor refuted by the running prototype:

* Mains inlet: a connector made for mains, or an external 5 V supply. See [Power Inlet](../hardware/control-unit.md#1-power-inlet-amass-xt60h-f-female-with-sheath-left-wall).
* The exact WS2812B variant, and its guaranteed input logic level from a 3.3 V GPIO on a 5 V rail. A one-way level shifter would break the probe on that pin.
* YZ_CTL_V1's active polarity, reset and boot behaviour, and real off current.
* Where a 470-1000 uF capacitor goes relative to the gate: the count estimate assumes mostly the LED boards' capacitors on the switched rail.
* Longest wire run, its gauge, drop at the last LED and temperature of a full 32-LED harness. The figures in [Garland cable](../guides/hardware.md#garland-cable) are calculated.
* The current limit is a model from colours and the LED count in use, not a measurement of the rail; its draw of an LED rests on one LED measured, and the board's 250 mA on the specification.

## Validation Still to Do

Host tests cover logic, not these, and the garland has not shown them yet: power cuts during writes, LEDs with LCD, Wi-Fi and flash writes together on 256 LEDs, a change to or from daylight saving, and several browsers at once. In a real browser against a board: a request from a page of another origin under the owner's domain refused for its cookie; the HTTP portal reached again after HTTPS failed, by a browser that was redirected from it before; the schedule edited from two browsers, one saving over the other's change; and a garland turned off while it probes before turning on, which must stay dark. Of 1.1.1, on the garland: settings, the remembered choice, the playlist, paired browsers or an uploaded effect that cannot be read at a start, each read again later, the 503s meanwhile, and a press during a slow start. Of 1.3.0: the utility for macOS on Intel. Of 1.4.0, on the garland: the limit from power settings other than the defaults, the cautious settings and their warning after a saved record that is not valid, the Scheduler24 screensaver, a trial confirmed by the portal's or the utility's request, and a return to the previous firmware after 10 minutes with nothing asking.

