# Contributing to csiGarland

Thank you for helping. A contribution is accepted under the [MIT License](LICENSE) of this repository.

`csiGarland` 1.0.0 is the first release. A garland updated to a later release keeps what a released build saved, settings, pairings, uploaded effects and its root certificate, unless a change says what it loses and why, and the [public API](docs/stubs/PUBLIC-API.md) changes only by adding.

## Before You Start

* **A bug or an idea:** open an [issue](https://github.com/ABespalov/csigarland/issues/new/choose) first if the change is large, so that no work is wasted.
* **A security problem:** do not open an issue. See [SECURITY.md](SECURITY.md).
* **The rules:** [AGENTS.md](AGENTS.md) holds the rules of this repository, for people and coding agents alike. This page does not repeat them all.

**Mains voltage and eFuses.** The hardware runs from mains, and the first start of the firmware burns a key into the chip, which cannot be undone. Read [Building the Hardware](docs/guides/hardware.md) and [Building a Fork and the First Flash](docs/guides/build-and-flash.md) before you build or flash anything. Much of the code can be worked on with no board at all, as below.

## Set Up

Install the tools in [What You Need](docs/guides/build-and-flash.md#1-what-you-need). The commands are for Windows with PowerShell 7.2 or later, run from the repository root.

A test build needs no certificates and no board. It signs with a chain made on the spot and thrown away:

```bash
$env:CSIGARLAND_THROWAWAY_SIGNING = '1'
```

```bash
pio run -d firmware/app -e esp32c6
```

Close that terminal or remove the variable before you make a build for a real board.

## Check Your Change

In this order, as the host tests use the cJSON that the firmware build brings in:

```bash
pio test -d firmware/app -e native
```

```bash
pwsh -File deploy\tests\source-rules.tests.ps1
```

```bash
pwsh -File deploy\tests\sensitive-data.tests.ps1
```

Run these as well when the change touches them:

| Changed | Also |
|---|---|
| The signing tool | `firmware/app/scripts/test_ota_sign_tool.py`, under ESP-IDF's Python |
| Device detection | `pwsh -File deploy\tests\partition-entry.tests.ps1` |
| A portal page | The real page in a browser with the server stubbed, as host tests do not run its script |
| A dependency, or a setting that links more | `pwsh -File deploy\new-third-party-notices.ps1` after a build |
| How a screen, portal page or utility page looks | `pwsh -File deploy\new-doc-images.ps1`, which remakes the pictures in the documents |
| Go | `pwsh -File deploy\test-utility.ps1`, which runs `gofmt`, `go build`, `go vet` and `go test -race` on the engine as it stands, with a C++20 compiler on the `PATH` for the effect engine: MinGW-w64 on Windows |

GitHub Actions runs these on every push. Your fork needs no secrets for them.

## What a Change Needs

* **Tests and documents in the same change.** Where they disagree with the code, the code is the truth until that change fixes them.
* **Plain words about what was checked.** Host tests, a stubbed page and a build are not the garland. Say which of them the change has been through, and what still needs a board.
* **The limits that keep a garland safe.** No effect or setting goes around the 1400 mA limit, and the Lighting task never waits on a lock.
* **A way to the next update.** The firmware must always be able to take one, including when HTTPS or a new image has failed, as a closed unit has no USB to fall back on.
* **Files of at most 1000 lines**, split by responsibility, with the header in [section 1 of AGENTS.md](AGENTS.md#1-mandatory-source-headers) on native sources.
* **English** in code, comments, documents and the interface, and `csiGarland` written just so.
* **Your own work.** Effects are written for this project: names may follow WLED's, code is not taken from it. Keep third-party licence headers as they are.
* **No secrets.** Never commit keys, certificates, tokens or passwords. Git tracks only `cert/README.md` of that folder.

Do not edit generated or linking files: `THIRD_PARTY_NOTICES.md` comes from its script, and `docs/stubs/` holds only links, as its [README](docs/stubs/README.md) explains.

New effects are welcome: a pack has room for 256, and 154 are used. Adding one has its own steps in [the Effects Guide](docs/guides/effects.md#13-adding-an-effect), and please say that you have seen it on real figurines, as no test can judge how it looks. Ask before adding a dependency, and pin its version.

## Commits and Pull Requests

Commits follow [Conventional Commits](https://www.conventionalcommits.org/), such as `fix(portal): refuse a foreign Host on the setup network`. Open pull requests against `main`, and fill in the template. What changes for an owner against the last release goes in [CHANGELOG.md](CHANGELOG.md) under `Unreleased`; a fix of work not yet released does not.
