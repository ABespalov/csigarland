# Building a Fork and the First Flash

From a clean fork of this repository to your own signed firmware on a board. After this the board updates over Wi-Fi, and the [User Guide](user-guide.md) takes over. Commands are for Windows with PowerShell 7.2 or later, run from the repository root.

## 1. What You Need

| Tool | Version | Why |
|---|---|---|
| [Git](https://git-scm.com/) | Any recent | |
| [PowerShell](https://github.com/PowerShell/PowerShell) (`pwsh`) | 7.2 or later, as the scripts use .NET 6 | Every script in `deploy/` |
| [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/) | 6.1, on the `PATH` (CI pins `platformio==6.1.19`) | Installs ESP-IDF 6 and its toolchain at the first build, which takes a while |
| Python | What PlatformIO needs (CI uses 3.12) | |
| [Go](https://go.dev/) | 1.26.3 or later, as `go.mod` says, only for the utility, which also needs a C++20 compiler | CI builds it. The firmware does not need it, and `stage-assets.ps1` checks the third-party notices only with it |
| GCC on the `PATH`, such as [WinLibs](https://winlibs.com/) | Any recent, with C++20 | Only for the host tests and the document pictures, which run on your computer. The firmware brings its own compiler |
| Chrome or Edge | | Only for the document pictures |
| A USB-C data cable | Not a charge-only one | Only for the first flash, and for recovery |

The signing tool also needs the `cryptography` Python package, in the version `firmware/app/scripts/ota_signing.py` pins. The build takes the first of PlatformIO's Pythons and ESP-IDF's that holds exactly that version, and where none does, installs it with `pip` into PlatformIO's own, never into another Python, so there is nothing to do by hand. The scripts choose the same way, by `deploy/SigningPython.psm1`. A Python that has some other version is passed over, as the tool needs what that one has.

## 2. Fork and Clone

Fork `https://github.com/ABespalov/csigarland` on GitHub, then:

```bash
git clone https://github.com/YOUR-NAME/csigarland.git
```

```bash
cd csigarland
```

A fork keeps the MIT licence and its copyright notice. Git ignores everything personal to your copy: all of `cert/` but its README, `temp/`, `assets/`, `deploy/*.local.*` and the build counter. So a clean clone holds no keys, and no build can be signed for a real board until section 4.

## 3. Check That It Builds

A test build makes a signing chain on the spot and throws it away. It proves your tools work, and **it cannot be installed by OTA on a garland running your own firmware, by design.**

| | Test or CI build | Owner build |
|---|---|---|
| Signing chain | Throwaway, made on the spot | Yours, in `cert/` |
| How | `$env:CSIGARLAND_THROWAWAY_SIGNING = '1'` | [Section 4](#4-make-your-signing-certificates) first |
| Installs on your garland by OTA | No | Yes |

```bash
$env:CSIGARLAND_THROWAWAY_SIGNING = '1'
```

```bash
pio run -d firmware/app -e esp32c6
```

```bash
pio test -d firmware/app -e native
```

**The order matters on a clean checkout:** the host tests use the cJSON that the firmware build brings into `managed_components`.

Then check the repository's rules, including the links in these documents, and that no key is in what git would track:

```bash
pwsh -File deploy\tests\source-rules.tests.ps1
```

```bash
pwsh -File deploy\tests\sensitive-data.tests.ps1
```

**Before going on, close that terminal or remove the variable**, so that it cannot make a throwaway build for your board:

```bash
Remove-Item Env:CSIGARLAND_THROWAWAY_SIGNING
```

GitHub Actions runs all of this on every push: the builds, tests and repository rules through [`.github/workflows/build-and-test.yml`](../../.github/workflows/build-and-test.yml) with a throwaway chain, and the search for keys through [`.github/workflows/sensitive-data.yml`](../../.github/workflows/sensitive-data.yml). Your fork needs no secrets for either.

## 4. Make Your Signing Certificates

A board installs an update only if signed under the root its firmware holds. Make that chain once:

```bash
pwsh -File deploy\new-signing-certificate.ps1 -Person "Your Name" -Organization "Your Lab" -Country AM -Email you@example.com
```

Every option is optional. **What you give goes into every update file, so it is public as soon as you publish one.**

It asks for a passphrase of at least 12 characters for the root key and writes to `cert/`, where git ignores all but the [README](../../cert/README.md). It runs under the Python `deploy/SigningPython.psm1` picks, and puts the pinned `cryptography` into PlatformIO's own if none has it, with no build first.

> **Nothing recovers the root key or its passphrase.** Without them, boards take new firmware over USB only. Keep them apart, off the build machine. Builds need only `cert/ota-root-ca.local.crt`, `cert/ota-signing.local.key` and `cert/ota-signing.local.crt`.

| Option | Does |
|---|---|
| `-RenewSigning` | New signing key under the same root. Boards accept it without an update |
| `-Force` | New root as well. Boards trusting the old one then need a flash over USB |

**A board flashed by someone else's build trusts their root,** the upstream project's included. It takes your updates only after one flash of your build over USB.

## 5. First Flash

> **Read this first. The first start of the firmware burns a random key into eFuse block `KEY5`, and this cannot be undone.** It is what encrypts the settings, and software cannot read it back. Settings found unencrypted, from firmware before that, are erased once, Wi-Fi password included.

Do this before the enclosure is closed, as USB is not reachable afterwards. USB alone powers the board, so [the mains can stay unplugged](hardware.md#5-checks-before-and-after-power) while the lid is off.

Find the port:

```bash
pwsh -File deploy\flash-device.ps1 -ListCandidates
```

Build, flash, and stay on its log. Naming the port is what allows a first flash, and enrols that board:

```bash
pwsh -File deploy\flash-device.ps1 -Port COM5
```

The script keeps a hashed identity of the board in `deploy/device-profile.local.json`, and from then on refuses any other. A board that does not answer: hold BOOT, tap RESET, release BOOT, and try again.

### What a good first start looks like

The log is also in `temp/serial_monitor.<build>.log`.

```text
I (741) csiGarland:  csiGarland Firmware Starting (v1.0.0.102)
I (761) effect_pack: pack 1 from pack_a, 25223 bytes
I (812) csiGarland:  Effects: 154
I (827) nvs: NVS partition "nvs" is encrypted.
I (1121) rtc: clock module found
W (8659) tls: this board makes its own root key
W (8716) tls: this board makes its own portal key
W (8728) tls: a new root certificate, which browsers must be given again
W (8844) tls: certificate for csigarland.local,192.168.4.1, root SHA-256 7F6BBCE4...
I (8850) esp_https_server: Server listening on port 443
I (8852) portal: portal ready
```

The `rtc` line appears only with a clock module. **Copy the `root SHA-256` line:** it is the one independent check of the certificate you will install in browsers. The `tls` lines come once. A later start that makes new keys means the settings were lost.

The screen says `Making the HTTPS key` for that moment, then shows the setup network and its password. Carry on with [Connecting](user-guide.md#2-connecting) in the User Guide.

### Check it before closing the enclosure

1. Join your home network through the portal.
2. Send a first update over Wi-Fi, so that you know the chain works while USB is still within reach:

   ```bash
   pwsh -File deploy\ota-update.ps1 -Address csigarland.local
   ```

3. It should end in `Confirmed: running ...`. Close the enclosure only then.

## 6. Every Script

| Script | Does |
|---|---|
| `deploy\flash-device.ps1 -ListCandidates` | Lists the ESP32-C6 USB devices. Touches nothing |
| `deploy\flash-device.ps1 -Port COMn` | Builds, flashes the firmware and effects over USB, and stays on the log. Settings and uploaded effects stay |
| `deploy\flash-device.ps1 -Enroll -Port COMn` | Records that board as the one this repository may flash, and stops |
| `deploy\flash-device.ps1 -DetectOnly` | Finds and checks the target as a flash would, and stops |
| `deploy\flash-device.ps1 -BuildOnly` | Builds and checks, spending a build number. Needs no board |
| `deploy\flash-device.ps1 -EffectsOnly -Port COMn` | Writes only the effects, with no build |
| `deploy\flash-device.ps1 -FullClean` | Rebuilds every component first |
| `deploy\flash-device.ps1 -NoMonitor` | Stops after flashing |
| `deploy\monitor-device.ps1` | Attaches to the log, without flashing |
| `deploy\ota-update.ps1 -Address <host>` | Builds and installs over Wi-Fi. See [Updates](user-guide.md#10-updates) |
| `deploy\stage-assets.ps1` | Checks a build's signature under your root and copies it to `assets/firmware`, with a checksum, a manifest and the third-party notices. Add `-NewBuild` to spend a new build number, and `-BuildProfile Dev` to stage a Dev build in place of a Release one |
| `deploy\new-third-party-notices.ps1` | Writes `THIRD_PARTY_NOTICES.md` from what the last firmware build linked and the versions it and the Go program use, and fails on a library it cannot place. Run it after a build that changed a dependency or a setting that links more. `-Check` writes nothing and fails if the file differs, which CI and `stage-assets.ps1` do |
| `deploy\new-signing-certificate.ps1` | Section 4 |
| `deploy\test-utility.ps1` | The checks of the Go code. Use it rather than plain `go test`, which could keep an effect engine compiled before the firmware last changed |
| `deploy\build-utility.ps1`, `deploy\run.ps1` | Build [the utility](utility.md) into `assets/utility`; the second also starts it, and starts nothing if the build failed. `-NoRun` only builds, `-Force` spends a build number though nothing changed, and `-BuildProfile Dev` keeps the debug information |
| `deploy\new-doc-images.ps1` | Makes the pictures of the screens, portal pages and utility in `docs/images`, from the firmware's own drawing code, its real pages and the utility's own window, with no device. Needs one firmware build first, and Chrome or Edge |
| `deploy\site-assets.ps1` | Copies the renders and pictures the [public site](../../site/README.md) shows from `docs/images` |
| `deploy\rebuild-release.ps1` | Builds a published release again in a copy of the sources, and compares it to the byte. See [Building a Release Again](rebuild.md) |
| `deploy\publish-release.ps1` | Writes the site's downloads and firmware catalog for a staged release, and makes its GitHub Release once its tag is pushed |
| `deploy\tests\reproducible-build.tests.ps1` | Builds the firmware and the utility twice from copies of the sources, and compares what they make |

## 7. Versions

A version is `CSIGARLAND_FIRMWARE_BASE` from [`platformio.ini`](../../firmware/app/platformio.ini) with a build number in it, such as `1.0.0.102`. The utility counts its own builds of the same base, so its numbers are not the firmware's.

A build has one of two profiles, which `CSIGARLAND_PROFILE` picks for `pio run` and `-BuildProfile` for the deploy scripts:

| Profile | Debug information | Version | Default of |
|---|---|---|---|
| Release | None, in the firmware or the utility | `1.0.0.102` | `flash-device.ps1`, `ota-update.ps1`, `stage-assets.ps1`, `build-utility.ps1`, `run.ps1` |
| Dev | ESP-IDF's, and the utility's symbols | `1.0.0.102-dev` | A plain `pio run` |

Releases are Release builds. A Dev build is never published, and its `-dev` says so wherever its version shows. A last build of the other profile is not staged: `stage-assets.ps1` builds again.

Each build through `flash-device.ps1` spends a number from `firmware/app/.csigarland_build_counter`, which stays on your machine. Each clone therefore counts from its own start, and a lower number is not refused: signed older versions install, by design.

Every build leaves three update files in `firmware/app/.pio/build/esp32c6`:

| File | Holds |
|---|---|
| `firmware.ota` | Firmware and effects |
| `firmware-only.ota` | Firmware |
| `effects.ota` | Effects |

### What a second build repeats

The same sources build to the same bytes, firmware and utility, with the same tools: the platform, ESP-IDF and its components are pinned in `platformio.ini` and `dependencies.lock`, Go's toolchain and modules in `go.mod` and `go.sum`, the signing tool's `cryptography` in `ota_signing.py`. What keeps them the same:

| Made | What keeps it the same |
|---|---|
| `firmware.bin` | The Release profile, whose ELF has no debug information to carry the paths of the machine, ESP-IDF's reproducible build, no date or time but `CSIGARLAND_SOURCE_DATE`, the build number given, and the components linked in one fixed order (`scripts/link_order.py`) |
| `generated/effects.pack` | The catalog's order, whatever order the files are on disk |
| The update files | The image and pack above, under the same key, signed deterministically (RFC 6979) |
| The utility | `-trimpath`, Go's toolchain as `go.mod` names it, no VCS stamp or link time, and the same C++ compiler |

A Dev build is not held to this: its debug information names the folders it was built in.

`pwsh -File deploy\tests\reproducible-build.tests.ps1` builds both twice from copies at different paths and compares them. [Building a Release Again](rebuild.md) says how anyone builds a published release again and compares it with the release. The compilers of the host tests are not pinned, and are part of no release.

## 8. What to Keep to Yourself

* `cert/` holds the keys of your update chain. Never publish it, and leave it out of release attachments.
* `deploy/portal-token.local.txt` is a paired browser's token for your garland.
* Update files carry your signing certificate, with the name and email in it, and nothing secret: HTTPS keys are born on each board and never leave it.
* Run `deploy\tests\sensitive-data.tests.ps1` before your first push. A key that has reached history is out, even if a later commit removes it: replace it.

## 9. When Something Goes Wrong

| What you see | What to do |
|---|---|
| The build stops: no update certificate | Section 4, or `$env:CSIGARLAND_THROWAWAY_SIGNING = '1'` for a test |
| The build could not install `cryptography` | It prints the `pip` command to try: run it, then build again |
| Host tests cannot find cJSON | Build the firmware first |
| Could not install `pyserial` | ESP-IDF's Python on a new machine lacks it, and the first build adds it for esptool. Run the `pip` command it prints, then build again |
| The script cannot find the board, or times out | Hold BOOT, tap RESET, release BOOT, and try again |
| The script refuses the board | It is not the enrolled one. `-Enroll -Port COMn` records a new one, deliberately |
| A setting in `sdkconfig.defaults` has no effect | The build remakes `firmware/app/sdkconfig.esp32c6` when the defaults change, by their digest in `generated/`. If it still has none, delete that file and build again |
| The board takes no OTA from your build | It holds another root. Flash it once over USB |
| The update says the effects pack does not fit | The board has the earlier partition table, with 32 KB slots, which OTA cannot change. Flash it once over USB, which keeps settings and uploaded effects. Until then `ota-update.ps1 -FirmwareOnly` still installs firmware |
| Lost the root key | Make a new chain with `-Force`, and flash every board over USB once |
| Firmware will not start | Hold BOOT, tap RESET, release BOOT, and flash over USB. Settings survive unless the partition table changes where they are |

How the firmware itself works is in [Architecture](../architecture.md).
