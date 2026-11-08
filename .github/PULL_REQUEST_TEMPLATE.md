## What and why

<!-- What changes for an owner or a contributor, and the reason. Link an issue if there is one. -->

## What was checked

<!-- Host tests, a stubbed page and a build are not the garland. Tick only what this change has been through. -->

- [ ] `pio run -d firmware/app -e esp32c6 --disable-auto-clean`, first, as the host tests use what it brings in
- [ ] `pio test -d firmware/app -e native`
- [ ] `pwsh -File deploy\tests\source-rules.tests.ps1`
- [ ] `pwsh -File deploy\tests\sensitive-data.tests.ps1`
- [ ] A changed portal page, in a browser with the server stubbed
- [ ] On a board (say which, and what was seen)

**Still needs a board:**

## Checklist

- [ ] Tests and documents are in this change, and `CHANGELOG.md` if an owner of the last release would notice it.
- [ ] Commits follow Conventional Commits.
- [ ] No keys, certificates, tokens or passwords.
- [ ] The firmware can still take its next update, and no effect or setting goes around the current limit the power settings give.
- [ ] `THIRD_PARTY_NOTICES.md` is regenerated if what the firmware links has changed.
