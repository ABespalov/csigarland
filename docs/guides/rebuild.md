# Building a Release Again, to the Byte

How anyone, from a clean fork, builds a published firmware and utility again and shows that what comes out is the release byte for byte. No key is needed: the firmware's update file is signed, but what it carries, the image and the effects pack, is compared by the digests its signed header holds.

## 1. What a Release Publishes

Beside each update file in a release:

| File | What it is |
|---|---|
| `csigarland-<version>.ota` | The signed update |
| `csigarland-<version>.ota.manifest.json` | Its version, the SHA-256 of its image and pack, of its root certificate, and of the firmware sources |
| `csigarland-ota-root.crt` | The root certificate the image trusts, which every image holds anyway |
| `csigarland-<version>-windows-amd64.exe` and its `.manifest.json` | The utility, with its SHA-256 and the Go and C++ compiler that made it |

## 2. The Tools, Exactly

The firmware's toolchain comes from the platform `platformio.ini` pins and the components `dependencies.lock` names. Install:

* Git and PowerShell 7.2 or later.
* PlatformIO Core 6.1.19, with the signing tool's `cryptography` in the same Python, as the first build needs it before ESP-IDF's environment exists: `pip install platformio==6.1.19 cryptography==44.0.3`.
* For the utility: Go, any 1.26 or later (the build fetches the exact `toolchain` that `go.mod` names), and WinLibs GCC 16.1.0 UCRT, POSIX threads, SEH, release r2, on the `PATH`. Another compiler builds the utility as well, but not the same file.

## 3. The Sources

Clone, and check out the release's tag, `v` and its base version, such as `v1.0.0`, or the commit its manifests name. The firmware and the utility of a release each have their own build number, such as `1.0.0.102` and `1.0.0.27`:

```bash
git clone https://github.com/ABespalov/csigarland C:\src\csigarland
```

```bash
git -C C:\src\csigarland checkout v1.1.0
```

A working tree with no changes is what the manifest's `source_sha256` describes. Any folder will do: a release is a Release build, with no debug information to name where it was built.

Download the release's files from its [GitHub Release](https://github.com/ABespalov/csigarland/releases): the `.ota.manifest.json`, `csigarland-ota-root.crt`, and the utility's `.exe.manifest.json`.

## 4. Build Again and Compare

From the repository's root:

```bash
pwsh -File deploy\rebuild-release.ps1 -FirmwareManifest <path>\csigarland-<version>.ota.manifest.json -UtilityManifest <path>\csigarland-<version>-windows-amd64.exe.manifest.json
```

It checks that the root certificate is the release's, copies the sources to `temp\rebuild-release`, so the checkout is left as it is, builds the firmware there with that root and the release's build number, unsigned, and builds the utility there with its build number. Each file is reported `same` or `DIFFER`, with both hashes.

To see that two builds of your own checkout agree, with nothing published:

```bash
pwsh -File deploy\tests\reproducible-build.tests.ps1
```

## 5. What Makes It Repeat

* **Firmware:** the Release profile, compiled with no debug information, the components linked in one fixed order, ESP-IDF's reproducible build (`CONFIG_APP_REPRODUCIBLE_BUILD`) keeps the date, time and paths of the machine out of the image. The date the image holds, the earliest its certificates may start, is `CSIGARLAND_SOURCE_DATE` in `platformio.ini`. `CSIGARLAND_BUILD_NUMBER` gives a build its number in place of the counter. Update files are signed deterministically (RFC 6979), so the owner, with the same key, makes the same `.ota` too.
* **Utility:** the Release profile, linked with no symbols, `-trimpath`, no VCS stamp, the repository's path mapped away in the C++ of the engine, no time in the PE header, and `-BuildNumber` for the version.

The host tests' compiler is not pinned, and is not part of any release.
