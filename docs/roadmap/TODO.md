# To Do

Work that is agreed and not done. Milestones, and what is left to see working on the board, are in the [roadmap](../stubs/ROADMAP.md).

## Applications

One Go + Gio code base for all of them. Where it stands is in [its status](utility-progress.md).

- [x] **Public API.** A branch of its own, `/api/v1/`, apart from the portal's private `/api/`, as [described](../stubs/PUBLIC-API.md). Not yet seen working on a board.
- [ ] **Desktop utility: Windows written and not yet tried on a garland, then Linux (Ubuntu), then macOS:**
  - [x] Tune effects and preview them, both in the utility itself and on the garland.
  - [x] Control the garland, repeating what the portal does: playing and favourites, brightness and speed, uploads, the schedule and clock, LED count and screen, network and certificate, updates, and the browsers that have access.
  - [x] Receive the garland's log and show it, as a sealed unit has no USB to read one from.
  - [ ] See all of it working against a real garland, as listed in [its status](utility-progress.md).
  - [ ] Ubuntu: Secret Service, a file dialog, and CI with the window's development headers.
  - [ ] macOS: Keychain and a file dialog.
- [ ] **Android and iOS applications** repeating the utility, on the same Go + Gio code.
- [ ] **Notices for the utility's own binary.** Its engine links MinGW's C++ runtime statically, which `deploy/new-third-party-notices.ps1` does not yet list from the compiler that built it: list what the chosen `.exe` holds before it is passed on.
- [x] **Guide.** [The utility's guide](../guides/utility.md), and the principles of tuning in [section 14 of the Effects Guide](../guides/effects.md#14-the-settings-utility).

Settled:

- **Preview inside the application.** Through cgo, on the firmware's own C++ effect engine, which already builds on a computer for the host tests. There is one engine, so its frames cannot differ. This needs GCC where the application is built, and is still to be tried on phones.
- **Reaching the garland from an application.** As in a browser: the owner enters an IP address or domain, and pairs at the garland's buttons. [The API's page](../stubs/PUBLIC-API.md) has the port, trust and the token.

## Measurements

The firmware and these documents rest on estimates at the points below. None can be settled by a host test or CI: each needs the assembled garland and a meter, or a look at the running board. Record the result here and correct the document or constant it names.

### Power

Before the tree, as the 1400 mA limit protects the supply only if its model holds.

- [ ] **Current per LED at full white.** The limiter assumes 55 mA per LED and 1 mA idle (`firmware/app/src/effects.cpp`). Set the LED count on the Device page to 20, which stays under the limit by that model, play white at 100 %, and measure on the LED rail. Divide by 20, and correct the constants if it differs.
- [ ] **Rail current at the limit.** All 32 LEDs, white at 100 %. By the model the limiter holds 1400 mA. Measure the LED rail alone, and again at the supply's output.
- [ ] **The board's own draw.** ESP32 and display with Wi-Fi active, peak and typical, against the ~250 mA and 120-180 mA in `docs/hardware/specification.md`. It comes on top of the limit, which leaves ~350 mA of the supply's 2000 mA by calculation.
- [ ] **Hi-Link under that load.** Output voltage at the limit, and whether it holds for an hour. Its temperature in the closed enclosure.
- [ ] **Typical effects.** Current with several effects at the usual brightness, against the 480-700 mA in the specification.
- [ ] **Off current of the YZ_CTL_V1.** Rail current in standby, with the module's active polarity and what it does at reset and boot. The specification says only that it is expected to be low.
- [ ] **How long the LEDs take to go dark with the gate closed.** `kRailDrainMs` in `firmware/app/src/garland_output.cpp` waits 200 ms by estimate, when a shorter chain length is set and the blanking frame failed. Watch the rail on an oscilloscope, or the LEDs, as it closes.

### Garland cable

- [ ] **Voltage at the last LED**, between `+5V` and `GND`, with white at 100 %, against the calculated figures in `docs/guides/hardware.md`.
- [ ] **Temperature of a full 32-LED harness** at the limit, at the longest run and its gauge.
- [ ] **Outer diameter of the chosen wire**, on one printed mount, as insulation varies by maker.
- [ ] **Data signal margin.** That the chosen WS2812B variant reads a 3.3 V GPIO on a 5 V rail at the far end of the first wire, with no errors on a long effect.

### LED count estimate

- [ ] **Probe on the gated rail.** It has not been run on a MOSFET-gated garland. Read the raw values the probe logs at each turn on, with several chain lengths, and correct the calibration points in `firmware/app/src/garland_probe_math.h`.
- [ ] **A 470-1000 uF capacitor and the gate.** Measure the estimate with it on either side of the gate, as the estimate assumes mostly the LED boards' capacitors on the switched rail.

### Memory and timing

Read from the Info page, or the log.

- [ ] **Lowest free heap and largest free block** with TLS working, while an upload runs with other pages open. Measured during development, under light use: 106 KB.
- [ ] **Heap used to parse an effect**, at its largest, in the Lighting task. Its upper bound is unknown.
- [ ] **Worst frame timing of the Lighting task** with the LCD, Wi-Fi and flash writes together, on 32 and on 256 LEDs. Look for flicker and for late frames.
- [ ] **Delay of the screen and buttons** during a TLS handshake and an OTA.

### Endurance

- [ ] **24-hour run** with the enclosure closed: temperature of the supply, the MOSFET and the board, and free heap at the end against the start.
