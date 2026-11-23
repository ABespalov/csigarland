# To Do

Work that is agreed and not done. Milestones, and what is left to see working on the board, are in the [roadmap](../stubs/ROADMAP.md).

## Applications

One Go + Gio code base for all of them. Where it stands is in [its status](utility-progress.md).

- [x] **Public API.** A branch of its own, `/api/v1/`, apart from the portal's private `/api/`, as [described](../stubs/PUBLIC-API.md), seen working on a board through the utility.
- [ ] **Desktop utility: Windows written and seen working against the owner's garland, then Linux (Ubuntu), then macOS:**
  - [x] Tune effects and preview them, both in the utility itself and on the garland.
  - [x] Control the garland, repeating what the portal does: playing and favourites, brightness and speed, uploads, the schedule and clock, LED count and screen, network and certificate, updates, and the browsers that have access.
  - [x] Receive the garland's log and show it, as a sealed unit has no USB to read one from.
  - [x] See all of it working against a real garland, as listed in [its status](utility-progress.md).
  - [x] Ubuntu: Secret Service, a file dialog, one window at a time, and CI with the window's development headers.
  - [x] Linux and macOS: the scripts of `deploy/` in bash, with the same checks and digests as the PowerShell ones.
  - [x] A release compiler for Linux, Ubuntu 24.04's GCC, named in `deploy/BuildVersion.psm1` and `deploy/lib/csg_tool.py`, and the Linux utility built on Windows in Docker, as [the utility's guide](../guides/utility.md#the-linux-utility-on-windows) says.
  - [ ] Ubuntu: see it working against a real garland, and the bash scripts against a real board.
  - [x] macOS: the login keychain, the system's file panels, one window at a time, and an application bundle signed ad hoc, with its icon and the question of the local network.
  - [x] macOS: seen working against the owner's garland, on macOS 26 on Apple silicon: connecting and pairing, the effects, an upload, an update file chosen in the panel, a second start connecting at once by the token kept in the keychain, after a new build the keychain's question once, Always Allow, and no question at the start after it, a backup saved and restored through the panels, and a bundle downloaded and opened past Gatekeeper.
  - [x] macOS: the release compiler named for `darwin-arm64` and `darwin-amd64`, and the bundle zipped as the file a release publishes. Two builds on one Mac, from copies at paths of other lengths, made the same zip to the byte, for either processor.
  - [ ] macOS Intel: see the `darwin-amd64` build run on an Intel Mac, or under Rosetta: it is built on Apple silicon, and has not been started yet.
- [ ] **Android and iOS applications** repeating the utility, on the same Go + Gio code.
- [x] **Notices for the utility's own binary.** `deploy/new-third-party-notices.ps1` names Go's library and the compiler's libraries the `.exe` links statically, by the groups `deploy/build-utility.ps1` holds its link map to. The compiler ships no licence texts, so those of winpthreads and of mingw-w64's runtime, from mingw-w64 v14.0.0, which WinLibs GCC 16.1.0 r2 is built on, are kept in `deploy/licences/`.
- [x] **Guide.** [The utility's guide](../guides/utility.md), and the principles of tuning in [section 14 of the Effects Guide](../guides/effects.md#14-the-settings-utility).

Settled:

- **Preview inside the application.** Through cgo, on the firmware's own C++ effect engine, which already builds on a computer for the host tests. There is one engine, so its frames cannot differ. This needs GCC where the application is built, and is still to be tried on phones.
- **Reaching the garland from an application.** As in a browser: the owner enters an IP address or domain, and pairs at the garland's buttons. [The API's page](../stubs/PUBLIC-API.md) has the port, trust and the token.

## Build and Release

- [x] **The zlib a firmware is built with.** `firmware/app/scripts/portal_pack.py` gzips the portal's pages with the zlib of the Python running the build, and another zlib packs them into other bytes: 1.1.0's firmware built again on Ubuntu 22.04 has the release's effects pack and the same nine pages before compression, but its classic zlib, 1.2.11, 1.3 and 1.3.2 alike, packs five of them 5 to 12 bytes tighter than the `1.3.1.zlib-ng` of Python 3.14 on Windows that built the release, so the image moves after them. `CSIGARLAND_RELEASE_ZLIB` in `platformio.ini` names it now: the build records and warns, the manifest holds it, `publish-release` refuses another, `rebuild-release` says so.
- [ ] **A firmware for a release built on Linux or macOS.** Their Python has the classic zlib, never zlib-ng, so a release's firmware is built on Windows. Packing the pages by a compressor of the project's own, the same on every computer, would lift it, and change the images of releases from then on.

- [ ] **A build that spends no number.** An option of the build scripts, in both versions of each, `-KeepNumber` in PowerShell and `--keep-number` in bash, for the firmware (`flash-device`, `stage-assets`, `ota-update`) and the utility (`build-utility`, `run`): a build under the number the counter holds, the counter left as it is, for a build to check or to stage that is no new version. Now each build moves a counter git keeps, so a build made only to check the scripts or the tools takes a number a release would. It has to settle how such a build is told apart, as two builds of one number may then differ: by its hashes, which the manifests and the garland already go by, and whether a release may take one at all.

## Measurements

The firmware and these documents rest on estimates at the points below. None can be settled by a host test or CI: each needs the assembled garland and a meter, or a look at the running board. Record the result here and correct the document or constant it names. The [Lab build](../guides/lab.md) shows on a closed unit what a meter cannot, and its section 4 is the way through Milestone 1's.

### Power

Before the tree, as the current limit protects the supply only if its model holds.

- [x] **Current of one LED.** Measured 2026-11-07 on a garland of one WS2812B, a meter in series with pin 1 of J2, the LED count set to 1, brightness 100 %, solid colours uploaded as effects: dark 0.51 mA; red 12.85, green 11.98, blue 12.68, white 36.60, grey `808080` 18.45 mA. So 36.1 mA at full white above the idle draw, each channel within 3 % of a third of it, their sum within 0.3 % of white, and half a level drawing half: the model of three equal, linear channels holds. The default is 40 mA and 1 mA, for a batch's spread (`firmware/app/src/power_model.h`), where 1.1.1 assumed 55 mA.
- [x] **Current of the assembled garland.** Measured 2026-11-23 on the 32 LEDs of the soldered harness with every figurine on, a meter on its 10 A range in series with the LED rail, the LED count set to 32, brightness 100 %, solid colours uploaded as effects: dark 16.08 mA; red 399.0, green 373.1, blue 394.4, yellow `FFFF00` 729.0, grey `808080` 565.6, white 1011.8 mA; white on LEDs 1-16 alone 571.5 mA, on LEDs 17-32 alone 555.8 mA. So 0.50 mA an LED dark, as the one LED, and 12.0, 11.2 and 11.8 mA an LED above it for red, green and blue, 3-7 % below the one LED. Past that the draw falls short of the channels' sum as the current grows: yellow 96 % of red and green, white 31.1 mA an LED, 89 % of the three channels, while grey, at half the current, is 98 % of half of them. The far half draws 2.8 % less than the near one. That is the rail sagging under load, along the harness and through the meter, which the WS2812B's drivers follow, not LEDs that draw more: none came near the 36.1 mA of the one LED, so the default of 40 mA keeps its room for a batch's spread, and the model, linear, counts 1312 mA for white where 1012 mA flow, 30 % on the safe side. The supply's output is left to the board's own draw below.
- [x] **Rail voltage at full white.** Measured 2026-11-23, the meter out of the current path, white on all 32 LEDs at 100 %, about 1 A: 4.80 V at the control unit's plug, 3.99 V at the last LED, a drop of 0.81 V. The wire alone, the 3 m lead of 20 AWG and 31 gaps of 12.5 cm of 22 AWG, `+5V` and `GND` both, comes to about 0.42 V by the figures of the hardware guide, so about as much again is lost elsewhere, of which the copper of the LED boards, which the rail crosses at every figurine, takes 0.08 V (below). The last LED stays within the WS2812B's 3.5-5.3 V, at the most current this garland can draw by its default settings. The sag is what cuts white to 89 % of the channels' sum above.
- [x] **Where the drop is.** Measured 2026-11-23 at the same white: the wire between the figurines. The LED boards are not it: on the first board, which carries nearly all the current, the two `+5V` pads measured 2.5 mV apart on 2026-11-23, about 2.6 mΩ a board and a rail, so 0.08 V for all 32 on both rails, where 0.41 V was unexplained. That leaves about 0.33 V for the wire, the joints and the plug. From the connector on the control unit's box to the last board, the 3 m lead included, `+5V` measured 0.410 V and `GND` 0.408 V, each where about 0.25 V was expected: no bad joint on one rail, but the same excess along both, as wire thinner than its label would give. Parted at the first figurine, the 3 m lead of 20 AWG kept to its figure and the excess was all in the wire between the figurines, sold as 22 AWG: it drops what 26 AWG would, which makes it 0.40 V a rail. The garland stays as it is, the last LED within the WS2812B's range at the most this garland draws; the hardware guide tells a builder to expect more than the table.
- [ ] **The board's own draw.** ESP32 and display with Wi-Fi active, peak and typical, against the ~250 mA and 120-180 mA in `docs/hardware/specification.md`, and the 250 mA the limit keeps for it (`kBoardReserveMa`). It comes on top of the limit, which leaves ~350 mA of the supply's 2000 mA by calculation at the default margin of 20 %, and ~88 mA at 5 %, if the board stays within its 250 mA. It cannot be measured on the garland's plug: it needs the supply's output, or a wattmeter at the mains for a rough figure. Measured there with the LEDs dark, it is what is left after their 16 mA, and with white on all 32 it is the whole load at its peak.
- [ ] **Hi-Link under that load.** Output voltage at the limit, and whether it holds for an hour. Its temperature in the closed enclosure.
- [ ] **Typical effects.** Current with several effects at the usual brightness, against the 480-700 mA in the specification.
- [ ] **Off current of the YZ_CTL_V1.** Rail current in standby, with the module's active polarity and what it does at reset and boot. The specification says only that it is expected to be low. 2026-11-23: 0 V on the garland's connector in standby, so the gate closes and the LEDs draw nothing then; what it does at reset and boot is still to see, section 4.1 of the [Lab build](../guides/lab.md).
- [ ] **How long the LEDs take to go dark with the gate closed.** `kRailDrainMs` in `firmware/app/src/garland_output.cpp` waits 200 ms by estimate, when a shorter chain length is set and the blanking frame failed. Watch the rail on an oscilloscope, or the LEDs, as it closes.

### Garland cable

- [ ] **Temperature of a full 32-LED harness** at the limit, at the longest run and its gauge.
- [ ] **Outer diameter of the chosen wire**, on one printed mount, as insulation varies by maker.
- [ ] **Data signal margin.** That the chosen WS2812B variant reads a 3.3 V GPIO on a 5 V rail at the far end of the first wire, with no errors on a long effect.

### LED count estimate, dropped

- [x] **Probe on the gated rail.** Dropped with the estimate itself in 1.4.1, which takes the count by hand only: [Architecture](../architecture.md), Garland detection, says why. Read the raw values the probe logs at each turn on, with several chain lengths, and correct the calibration points in `firmware/app/src/garland_probe_math.h`. First run, 2026-11-23, the Lab build 1.4.0.121-lab at boot on the garland of 32, the rail off for a second after the blanking frame: the pin held at 2.02 V and fell to 2.00 V over 0.54 s, so `count unknown`, as for a live rail, which reads about 1.9 V. The same at a turn on after 140 s in standby, so no charge left from the blanking frame: 1.99 V, flat to 0.54 s. The rail reads 0 V in standby, so the gate is closed and the rail unpowered: these LEDs hold their input at 2.0 V with no supply and pass the pull-up's current into the rail, if at all, too little to raise it in half a second, where the LEDs measured in development held it one diode drop above the rail and let it charge. The count cannot be estimated on this garland, which would show `?` on automatic, and takes its count, 32, from the Device page; presence still works, 2.0 V being under the 2.3 V it takes.

### Memory and timing

Read from the Info page, or the log.

- [ ] **Lowest free heap and largest free block** with TLS working, while an upload runs with other pages open. Measured during development, under light use: 106 KB. The Lab build 1.4.0.121-lab, whose log holds 12 KB more, reported 50.7 KB free at its start and 34.3 KB at the lowest after a minute of TLS handshakes, on 2026-11-23, and 14.5 KB at the lowest after a burst of handshakes that clients broke off, at two minutes. With 12 KB less for the log, a Release build would have had about 26 KB: little room for a TLS handshake, whose buffers alone take some 20 KB while it runs (16 KB in, 4 KB out).
- [x] **Updates over HTTPS with mbedTLS's slot and pool.** 1.4.1 gives mbedTLS a 16.75 KB slot for its record buffer and a 16 KB pool for the rest (`firmware/app/src/tls_pool.cpp`). A first 32 KB pool for everything failed on the garland in 1.4.1.124-lab: the event streams' send buffers took the record buffer's hole between records. On the garland, with the Lab build 1.4.1.125-lab: updates from the utility in standby and lit, with the portal open in a browser, each through; the `tls:` line's pool peak under 16 KB, no slot missed and nothing spilled during an update; the heap's lowest free. Then the pool's size corrected to the peak with room. Seen on 2026-11-23: three updates from the utility, two lit and one in standby, each through at the first try; the pool's peak 12.2 KB of 16, no slot missed, nothing spilled, the heap's lowest free 10.5 KB lit and 23.3 KB in standby. The pool keeps its size.
- [ ] **Heap used to parse an effect**, at its largest, in the Lighting task. Its upper bound is unknown.
- [ ] **Worst frame timing of the Lighting task** with the LCD, Wi-Fi and flash writes together, on 32 and on 256 LEDs. Look for flicker and for late frames.
- [ ] **Delay of the screen and buttons** during a TLS handshake and an OTA.

### Endurance

- [ ] **24-hour run** with the enclosure closed: temperature of the supply, the MOSFET and the board, and free heap at the end against the start.
