# Security Policy

## Reporting a Problem

**Please do not open a public issue for a security problem.** Report it privately through [GitHub's vulnerability reporting](https://github.com/ABespalov/csigarland/security/advisories/new).

Say what is affected, how to reproduce it, and the firmware version from the portal's Info page. Do not include real keys, tokens or Wi-Fi passwords: yours would then need replacing.

`csiGarland` is a personal open-source project with no fixed response time. A report is read and answered as soon as possible, and fixed in the open once there is a fix.

## Supported Versions

Only the latest release of the firmware and of the utility is looked after: a fix comes in the next release, as an update file for the firmware, installed over Wi-Fi from the portal or the utility, and a new utility for each system, on [Releases](https://github.com/ABespalov/csigarland/releases). Each fork signs its own firmware, so a fix reaches a garland only when its owner installs one.

## What Counts

Among what is worth reporting:

* Using the portal or its API without pairing, other than as described below, or with a paired browser's cookie from a page of another origin.
* Getting a token, a Wi-Fi password or a private key from a garland over the network.
* Installing, through an update file, firmware or an effects pack not signed under the root that a board holds. A single effect file uploaded or previewed by a paired browser or program is meant to go unsigned.
* Keeping a garland from taking its next update, or from going back after a failed one.
* A private key or other secret in this repository or in what the build produces.
* Input over the network that crashes a garland or corrupts what it has saved.

## Accepted Limits

These are known and described in [the architecture](docs/architecture.md), so they are not defects unless you find they reach further than it says:

* **Physical access.** There is no Secure Boot or flash encryption, so whoever holds the board can read its settings and keys.
* **The setup network.** It asks for no pairing and uses plain HTTP, as phones' login windows refuse a private certificate. Joining it takes the password on the garland's screen.
* **Plain HTTP as a way back.** A firmware whose HTTPS has failed stays reachable over plain HTTP so that the next update can arrive, and tokens then travel unencrypted. This is also why HSTS is not sent, and why plain HTTP sends a browser to HTTPS by a temporary redirect that no cache keeps.
* **Relaying during pairing.** The digits stop a second asker, not someone relaying between the owner and the garland. Opening `https://` with the root installed is what stops that.
* **Signed older versions.** They install by design, so an update can be undone. A paired browser can go back to any version its owner signed.
* **First contact.** A browser has to be given the garland's root certificate once, and nothing proves it the right one but the owner's own check of it.

## Hardware Safety

The hardware runs from mains. A danger in the [hardware documents](docs/guides/hardware.md) is best reported as an ordinary issue, so that others see the warning at once.
