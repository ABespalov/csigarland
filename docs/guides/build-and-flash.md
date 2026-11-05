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

The signing tool also needs the `cryptography` Python package, in the version `firmware/app/scripts/ota_signing.py` pins. The build takes the first of PlatformIO's Pythons and ESP-IDF's that holds exactly that version, and where none does, installs it with `pip` into PlatformIO's own installer environment, never into another Python. The scripts choose the same way, by `deploy/SigningPython.psm1`, taking the first that holds it of: the Python `pio` runs from, as `pio system info` names it, or found beside `pio.exe` or one folder above; PlatformIO's own in `~/.platformio/penv`; ESP-IDF's environments there, newest first. With none, they install it into PlatformIO's own, as the build does, and `publish-release.ps1 -WhatIf` installs nothing and says what is missing. A Python that has some other version is passed over, as the tool needs what that one has.

PlatformIO installed with `pip` runs from that Python, which is not PlatformIO's own, and ESP-IDF's environment is made only later in the first build. So install the two together, as CI does, best into a virtual environment of the project's or with `pip install --user`, never into a Python other programs share:

```bash
pip install platformio==6.1.19 cryptography==44.0.3
```

With that, `deploy\new-signing-certificate.ps1` in section 4 needs no build before it.

## 2. Fork and Clone

Fork `https://github.com/ABespalov/csigarland` on GitHub, then:

```bash
git clone https://github.com/YOUR-NAME/csigarland.git
```

```bash
cd csigarland
```

A fork keeps the MIT licence and its copyright notice. Git ignores everything personal to your copy: all of `cert/` but its README, `temp/`, `assets/`, and `deploy/*.local.*`. So a clean clone holds no keys, and no build can be signed for a real board until section 4.

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
| `deploy\flash-device.ps1 -BuildOnly` | Builds and checks, spending a build number, which a build that fails or fails a check gives back. Needs no board |
| `deploy\flash-device.ps1 -EffectsOnly -Port COMn` | Writes only the effects, with no build |
| `deploy\flash-device.ps1 -FullClean` | Rebuilds every component first |
| `deploy\flash-device.ps1 -NoMonitor` | Stops after flashing |
| `deploy\monitor-device.ps1` | Attaches to the log, without flashing |
| `deploy\ota-update.ps1 -Address <host>` | Builds and installs over Wi-Fi, then watches the garland restart and confirm the new firmware. An update that gets no answer at all, as when the connection drops while the garland restarts, may still have been installed, so it watches all the same and says what it finds. See [Updates](user-guide.md#10-updates) |
| `deploy\stage-assets.ps1` | A build it makes and then cannot stage, for any check or a failed copy, gives its number back, and leaves nothing staged under it. Checks a build's signature under your root and copies it to `assets/firmware`, with a checksum, a manifest and the third-party notices, whose digests the manifest records, as the next staging writes them again. It copies the build's files to a set of their own under `temp\` and checks that set, so a build meanwhile cannot mix two builds' files, and fetches the Go modules first, whose licences the notices hold. Add `-NewBuild` to spend a new build number, and `-BuildProfile Dev` to stage a Dev build in place of a Release one |
| `deploy\new-third-party-notices.ps1` | Writes `THIRD_PARTY_NOTICES.md` from what the last firmware build linked and the versions it and the Go program use, and fails on a library it cannot place. Run it after a build that changed a dependency or a setting that links more. `-Check` writes nothing and fails if the file differs, which CI and `stage-assets.ps1` do `-UtilityOnly` checks the utility's part alone, with no firmware build, as `deploy\build-utility.ps1` does for its manifest |
| `deploy\new-signing-certificate.ps1` | Section 4 |
| `deploy\test-utility.ps1` | The checks of the Go code. Use it rather than plain `go test`, which could keep an effect engine compiled before the firmware last changed |
| `deploy\build-utility.ps1`, `deploy\run.ps1` | Build [the utility](utility.md) into `assets/utility`; the second also starts it, and starts nothing if the build failed. `-NoRun` only builds, `-Force` spends a build number though nothing changed, and `-BuildProfile Dev` keeps the debug information |
| `deploy\new-doc-images.ps1` | Makes the pictures of the screens, portal pages and utility in `docs/images`, from the firmware's own drawing code, its real pages and the utility's own window, with no device. Needs one firmware build first, and Chrome or Edge |
| `deploy\site-assets.ps1` | Copies the renders and pictures the [public site](../../site/README.md) shows from `docs/images` |
| `deploy\site-pages.ps1` | Makes the public site's Russian pages under `site/ru` from its English pages and `deploy/site-ru.json`; `-Check` says whether they are current |
| `deploy\rebuild-release.ps1` | Builds a published release again in a copy of the sources, and compares it to the byte. See [Building a Release Again](rebuild.md) |
| `deploy\publish-release.ps1` | Makes the GitHub Release of a staged release, or adds to one, and writes the site's downloads and firmware catalog for it, to commit. See [Publishing a Release](#publishing-a-release) |
| `deploy\tests\reproducible-build.tests.ps1` | Builds the firmware and the utility twice from copies of the sources, and compares what they make |

### Publishing a Release

`pwsh -File deploy\publish-release.ps1 -Release 1.1.0 -FirmwareVersion 1.1.0.113 -UtilityVersion 1.1.0.37` makes the GitHub Release of what `stage-assets.ps1` and `build-utility.ps1` left in `assets\`, once its tag, `v1.0.0`, is pushed, and then writes the site's `downloads\manifest.json` and `downloads\firmware-catalog-v1.json` for it, to commit.

* **What it publishes.** `-Only Firmware` publishes the firmware with its root certificate, `-Only Utility` the utility, for the platform `-Platform` names as Go names it, `windows-amd64` by default; both by default. A platform is released only once `deploy/BuildVersion.psm1` names the compiler its releases are built with, so far Windows x64 alone. A release already on GitHub takes what a later run adds, as the utility for another platform.
* **`-BuildNew`**, in place of the versions: builds what it publishes first, the firmware by `stage-assets.ps1 -NewBuild` and the utility by `build-utility.ps1 -Force`, the utility only for the computer it runs on, and releases the versions they say they built. Before either spends a number it checks both bases against the release, and, unless `-SiteOnly`, that the tag on GitHub is the one here and that the sources, the notices and the licence here are the tag's.
* **What it checks.** What the tag holds is read from git as it stores it, with no checkout. Each file must be built from what the tag holds, by its sources' digest; the update file signed under the root published with it, its signed header naming the image, pack and root its manifest names; the notices and licence beside it those it was staged with and the tag holds; the catalog's count of effects the staged file's and the tag's. The utility must be built with the compiler [Building a Release Again](rebuild.md) names, `CC` and `CXX` as Go has them and with no arguments of their own, no `CGO_*FLAGS` but Go's own, x86-64 level `v1` on amd64, as a higher `GOAMD64` stops at start on an older processor, `GOWORK=off`, and no `GOFLAGS`, `GOEXPERIMENT` or `GOFIPS140` but off, `go env -w` included, all of which `build-utility.ps1` sets or records. The utility's source digest takes in `deploy/BuildVersion.psm1` with the script, as it sets the engine's flags.
* **Files already released.** A file the release holds counts as sent only if its SHA-256 there, as GitHub keeps it or of its bytes read back, is the one here; one of other bytes is refused, never replaced, before the notes or the site change. The notes' table of files is written from the files the release holds, read again right before, and read back after: a run in another checkout that added a file keeps its line.
* **The site.** The lists keep the releases before, newest first, and what the run does not publish as it was, the utility's platforms in the order Windows, Linux, macOS. Each build is dated as GitHub dates its own file, so a utility added days after its release has its own day, and the lists are sorted by those days before the firmware catalog is cut to its newest within 48 KB, as the utility of 1.0.0 reads 64 KB at most; the Downloads page keeps them all. The files are written whole beside the others, under names of the run's own, and then moved into place.
* **Taking it up again.** `-SiteFromRelease` writes the site for a release already made on GitHub, as when a run stopped before writing it, once the release holds the staged files. `-SiteOnly` writes it to `temp\site-preview-<release>\` to look at, before the tag.
* **What it leaves.** One run at a time in a checkout, under `temp\build-publish.lock`. `-WhatIf` writes nothing outside `temp\`, asks Go with `GOTOOLCHAIN=local`, so nothing is downloaded, and installs nothing into a Python, saying what is missing instead. What it uploads is a copy of the staged files, made before it checks them, so a staging meanwhile cannot change them; each run keeps its copies and the notes in `temp\` under a name of its own.

## 7. Versions

A version is `CSIGARLAND_FIRMWARE_BASE` from [`platformio.ini`](../../firmware/app/platformio.ini) with a build number in it, such as `1.0.0.102`. The utility counts its own builds of the same base, so its numbers are not the firmware's.

A build has one of two profiles, which the PlatformIO environment picks for `pio run` and `-BuildProfile` for the deploy scripts:

| Profile | Environment | Debug information | Version | Default of |
|---|---|---|---|---|
| Release | `esp32c6-release` | None, in the firmware or the utility | `1.0.0.102` | `flash-device.ps1`, `ota-update.ps1`, `stage-assets.ps1`, `build-utility.ps1`, `run.ps1` |
| Dev | `esp32c6` | ESP-IDF's, and the utility's symbols | `1.0.0.102-dev` | A plain `pio run` |

Releases are Release builds. A Dev build is never published, and its `-dev` says so wherever its version shows. Each profile builds in its own folder, `firmware/app/.pio/build/<environment>`, as its flags differ for every component of ESP-IDF: going from one to the other compiles nothing again that has not changed, and a last build of the other profile is never staged.

**A build compiles only what changed.** PlatformIO deletes all of `.pio/build`, every environment's objects, when `platformio.ini` or the list of source files changes, and ESP-IDF then takes minutes to compile again. The deploy scripts build with `--disable-auto-clean`, and `firmware_build_version.py` does in its place what such a change needs: CMake configures each environment again, and only objects whose command changed are compiled. Add the flag to a `pio run` of your own for the same:

```bash
pio run -d firmware/app -e esp32c6 --disable-auto-clean
```

`flash-device.ps1 -FullClean` still compiles everything, should a build ever need it.

Each build through `flash-device.ps1` spends a number from `firmware/app/.csigarland_build_counter`, and a utility build one from `cmd/csigarland/.csigarland_build_counter`. Git keeps both, so commit them with the work they were built from: a clone on another machine then goes on from the last number. Neither counts as a change of the sources, in the digest a manifest records or in its `dirty`, and on a merge of two machines' builds the higher number is the one to keep. A build that fails, is stopped, or fails a check after it gives its number back and leaves no image of it, so the next build takes the same number; so does a utility build, which is made and checked in a folder of its own under `temp\` and put in place only then, so a failed build of a number named again leaves the file there as it was, and whose counter is written once the file and its manifest are in place. One firmware build and one utility build of a checkout run at a time, each under a lock, `temp\build-firmware.lock` and `temp\build-utility.lock`, held from taking the number to giving it back: a second waits for none, and says so. `stage-assets.ps1` holds the firmware's for all its staging, `ota-update.ps1` from its build to the bytes it reads, and a write over USB until the last try of the write, not the log after it, so no build replaces the files they take in between. `ota-update.ps1 -EffectsOnly` makes its file under `temp\`, never in the build's `generated\`. A utility file of a number named again is put in place with its manifest, or both stay as they were, and a build whose sources changed while Go compiled them fails, spending no number. A staging with no Go, which cannot check the notices against what the build links, says so in its manifest, `notices_checked`, and no release takes it. The scripts find PlatformIO's packages and Pythons under `PLATFORMIO_CORE_DIR` when it is set, and a build alone, for staging or OTA, no longer asks for esptool. A Release firmware build refuses what PlatformIO would take into it from the environment, `PLATFORMIO_BUILD_FLAGS`, `PLATFORMIO_BUILD_SRC_FLAGS`, `PLATFORMIO_BUILD_UNFLAGS`, `PLATFORMIO_BUILD_SRC_FILTER`, `PLATFORMIO_EXTRA_SCRIPTS`, `PLATFORMIO_SRC_DIR`, `PLATFORMIO_INCLUDE_DIR`, `PLATFORMIO_LIB_DIR`, `PLATFORMIO_LIB_EXTRA_DIRS` and `PLATFORMIO_DEFAULT_ENVS`, and what ESP-IDF would, `SDKCONFIG_DEFAULTS` and `SDKCONFIG`, and its mark of a config made is of a format of its own, `made-from-defaults/2`, so one marked by a build before these checks is made again once, and runs the scripts `platformio.ini` lists for it alone. The utility's build takes `GOFLAGS`, `GOEXPERIMENT` and `GOFIPS140` as Go really has them, `go env -w` included, warns of any but off, and records them; a release takes none of them. An image that passed its checks keeps its number, whether or not it was written to a board. A clone that has not taken the latest counter counts on from the number it holds, and a lower number is not refused: signed older versions install, by design.

Every build leaves three update files in its environment's folder, `firmware/app/.pio/build/esp32c6-release` for Release:

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
| The build could not install `cryptography`, or says the Python is not PlatformIO's to install into | It prints the `pip` command for the Python that runs PlatformIO: run it, then build again |
| Host tests cannot find cJSON | Build the firmware first |
| Could not install `pyserial` | ESP-IDF's Python on a new machine lacks it, and the first build adds it for esptool. Run the `pip` command it prints, then build again |
| The script cannot find the board, or times out | Hold BOOT, tap RESET, release BOOT, and try again |
| The script refuses the board | It is not the enrolled one. `-Enroll -Port COMn` records a new one, deliberately |
| A setting in `sdkconfig.defaults` has no effect | The build remakes `firmware/app/sdkconfig.<environment>` when the defaults change, by their digest in `generated/`. A Release build, `esp32c6-release`, also remakes one that differs from what it made from the defaults, as after an edit by hand: `scripts/sdkconfig_made.py` records it as made in the same build whose configure made it, before anything else can change it, `menuconfig` included, and every later build holds it to that digest, and to what its configure wrote to `config/sdkconfig.json`, so a published image holds only what the sources hold. If it still has none, delete that file and build again |
| The board takes no OTA from your build | It holds another root. Flash it once over USB |
| The update says the effects pack does not fit | The board has the earlier partition table, with 32 KB slots, which OTA cannot change. Flash it once over USB, which keeps settings and uploaded effects. Until then `ota-update.ps1 -FirmwareOnly` still installs firmware |
| Lost the root key | Make a new chain with `-Force`, and flash every board over USB once |
| Firmware will not start | Hold BOOT, tap RESET, release BOOT, and flash over USB. Settings survive unless the partition table changes where they are |

How the firmware itself works is in [Architecture](../architecture.md).
