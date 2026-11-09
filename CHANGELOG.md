# Changelog

All notable changes to the `csiGarland` project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versions are not Semantic Versioning: a release is a base, such as `1.0.0`, and the firmware and
the utility each add their own build number to it, such as `1.0.0.102` and `1.0.0.27`. Updates
tell builds apart by the hashes of what they hold.

## [Unreleased]

## [1.4.0] - 2026-11-09

The firmware and the utility, both 1.4.0. Everything a 1.1.1 garland saved, settings, pairings, uploaded effects, favourites, the schedule, the playlist and its root certificate, stays as it was after this update, but for an uploaded effect with `\u0000` in a key or a text, which is refused, as said below. A garland gone back to 1.1.1 reads what this one saves as it did, with no power settings, so with its 1400 mA, and with the Starfield in place of Scheduler24. A browser paired on plain HTTP while HTTPS was down pairs again once it is up. The utility 1.4.0 works with the firmware 1.1.0 and 1.1.1 too, and the utility 1.3.0 with this firmware, without its new settings. A new firmware confirms itself only once a paired browser or program has reached it: keep the Firmware page or the utility open until the update says it is done.

### Firmware

- The current limit is worked out from the owner's power settings, no longer a fixed 1400 mA: what the supply gives beyond 250 mA for the board, the screen and Wi-Fi, less a margin of 5-20 % of that. The supply can be set from 500 to 4400 mA, so the limit stays under the 4.3 A of the LED switch, at most 3942 mA. A garland updated from 1.1.1 starts with 2000 mA and 20 %, so its limit stays 1400 mA.
- An LED at full white counts as 40 mA above 1 mA dark by default, where it counted 55 mA. One WS2812B of the owner's garland measured 36.1 mA above 0.51 mA, so 32 LEDs on full white draw 1312 mA by the model, under the limit, and are no longer dimmed to about 78 %.
- The LEDs stay dark while the saved power settings cannot be read, or while their draw when dark alone is over the limit, which no dimming takes away, and light by themselves once that changes. Saved settings that are not valid give the most cautious ones, a 200 mA limit, never the defaults, until they are saved again.
- A new firmware confirms itself only once it has answered a paired browser or program, at the end of its first minute if one came by then, and otherwise as soon as one does, for up to 10 minutes, after which it goes back to the previous firmware. A refusal does not count, so an image that broke pairing or routing goes back. Keep the Firmware page, the utility or the build machine's script open until it says the update is done.
- A new screensaver, Scheduler24, for a garland with the clock module, flies across the screen with what comes next and how soon, the next scheduled action within a day or the playlist's next effect, as `next: Fireplace` and `in 2h 05m`. With nothing to come it says what happened last and how long ago, the start, a turn on or off, the LEDs going on or off, or a scheduled action done, as `last: Off` and `3h 10m ago`. Its words wrap, and take a smaller letter, to stay within 80 % of the screen; saved, and the module gone since, it runs as the Flying Clock.
- Off the setup network, plain HTTP and the routes that ask for no pairing answer only to the garland's own names, its `.local` name, its address on the home network and the owner's domain or fixed address, and to any other name with 421. So a page under a name of its own that DNS turns to the garland, as DNS rebinding does, gets nothing there, and neither does a name a router gives the garland, such as `csigarland.lan`. The owner's domain counts as soon as it is read, a reading that succeeds late included.
- While HTTPS runs, the pairing cookie a browser was given on plain HTTP no longer counts, and is cleared: whoever intercepts plain HTTP could have put one there. Such a browser pairs again over HTTPS.
- A TLS handshake has 5 s, where it had 15, so a client that sends a byte now and then no longer holds the server's one task that long.
- A request's JSON that holds more than 1024 values is refused before it is parsed, as a body of nothing but numbers could take more memory than the server has free.
- An effect file, or any request's JSON, with `\u0000` in a key or a text is refused: the firmware's parser ended the string there, so `"bri\u0000x"` counted as a second `bri`. An uploaded effect saved before with one is refused at a start too, its reason in the log: it stays in flash, and plays again once uploaded without it under the same id, which replaces it.
- A saved time zone that cannot be read at a start is no longer replaced by the zone of the next browser to open the portal. The garland goes by UTC until it is read again, a few seconds on, and meanwhile does no scheduled step, which by UTC would come hours off, and tells no next action, day's timeline, step coming soon or time of day on its screen, as with no clock set.
- Uploaded effects not read at a start are read again every few seconds, whether or not a browser opens the portal, where they waited for one; so is the flash partition that holds them, when it could not be opened, where until a restart its effects were missing and an upload was refused.
- A scheduled step that starts the playlist, whose every effect is an uploaded effect not read at a start, or that plays such an effect, waits for it, for up to 10 minutes after the start, and comes once it is read, unless the owner chose since. It did nothing, counted as done, and a garland meant to come on stayed off.
- A favourite that is an uploaded effect not read at a start is no longer lost when another favourite is changed. Until that effect is read, a change of a favourite is refused, 503, and once it is, it is a favourite again.
- An uploaded effect no longer comes back after it is uploaded again and deleted when the whole list of uploaded effects, not only one of them, could not be read at a start. Until it is, an upload under an id the garland does not know is refused, 503, and the list is read again before each upload.
- The effect the garland was left on is no longer lost to the first favourite when it is an uploaded effect not read at a start, or when the remembered choice could not be read at a start, and only the power, brightness or speed is changed meanwhile. It stays the one saved, and plays once read, unless another was chosen since.
- With a playlist left playing, a change of only the power, brightness or speed no longer saves the playlist's first effect as the one the garland was left on. The effect saved stays, and plays when the playlist is stopped after a restart.
- A playlist left playing, whose list could not be read at a start and plays once read, no longer makes a garland with no clock yet save a choice as made just then. That kept the schedule from catching up on its steps of the last day once the time was known.
- With HTTPS down and the firmware not on trial, the open trial log answers with only its status line, no longer with the log a paired client had started keeping.

### Portal

- The Device page sets the power supply, what one LED draws at full white and dark, and the margin. As they are typed it works out the limit, what the chain draws at full white, and how many LEDs that leaves undimmed, and it warns when the saved settings could not be used.
- The Device page offers the Scheduler24 screensaver with the clock module, and says it needs one without.
- The Firmware page waits up to 12 minutes for a new firmware to confirm itself, where it gave up after 150 s, and opens Info once it has.

### Public API

- `GET` and `POST /api/v1/leds/power` are new: the four power settings, and what they come to, the limit, the chain's draw at full white and the LEDs left undimmed, with `invalid_record` when the saved settings could not be used. Both are 503 while the settings could not be read, and a garland of 1.1.1 or before answers 404.
- `/api/v1/display` takes the saver `scheduler24`, and its `GET` says `clock_module`. Without the module a `POST` of it is 409, unless it is the saver chosen already.
- `GET /api/v1/effects` says `complete`, false while an uploaded effect, or a favourite's mark of one, could not be read yet.
- `POST /api/v1/time/rtc` with a time of day by hand is 503 while a saved time zone could not be read yet, where it set the clock module hours off.
- While a saved time zone could not be read yet, `next` in `/api/v1/info` and `/api/v1/schedule` has `clock` false and no action, and `/api/v1/schedule/timeline` has `synced` false, as with the clock not set.
- A JSON body that holds more than 1024 values, or `\u0000` in a key or a text, is refused with 400, as one that does not parse.
- Off the setup network, a request on plain HTTP, or to a route that asks for no pairing, under a Host that is not the garland's own is refused with 421.
- A TLS handshake has 5 s, where it had 15.

### Utility

- The Device page has the power settings, for a garland of 1.4.0, with the limit they give, what the chain draws at full white and how many LEDs that leaves undimmed. It takes the ranges the garland sends, and warns when the saved settings could not be used.
- The Device page offers the Scheduler24 screensaver, for a garland with the clock module.
- The preview and Tune draw as many LEDs as the garland connected to sends to, where they always drew 32, and Tune works the current out for that many. A garland set to 100 LEDs showed 32 and their current; a long chain is drawn smaller, numbered every few LEDs where the numbers would run together, and with no garland it is 32.
- Tune works an effect's current out by the power settings of the garland connected to, and by the model of 1.1.1, 55 mA and 1400 mA, for a garland of 1.1.1 or before. While those of the garland connected to cannot be read, it shows no current rather than another garland's, and asks again.
- Tune's chart of the current is worked out away from the window, once the file, the power settings or the LED count have stood still a moment, so a slider dragged no longer holds the window up. Meanwhile Tune shows the current drawn now, and no chart of what was before.
- Tune's chart draws the part of a frame past the limit in the limit's red, the rest blue, where the whole frame was amber, and keeps the limit's line below its top edge.
- Tune's controls change a key written twice in a layer where the garland reads it, the first, as the preview does, its colours and its zone's period and offset too. They changed the second, which nothing plays; unticked, or a palette taken off, such a key now goes in every copy, where the next one came out from behind it.
- Tune shows no controls for a file with `\u0000` in a key or a text, which the garland refuses, and says why. Its slider changed another key than the one the garland and the preview read.
- Tune opens a file of up to 64 KB, where it read any file whole, and saves one whole or not at all, as a backup is saved.
- A backup holds the power settings, and a restore puts them back; one of a garland of 1.1.1 or before holds none, and its restore leaves the garland's own.
- A backup is refused, to be tried again in a moment, while the garland says its list of effects is not whole, as when an uploaded effect could not be read yet. It was made without that effect, or without a favourite's mark, and called made.
- A restore no longer shortens the playlist or the schedule it puts back by a list of effects the garland says is not whole. It leaves out none of their effects for it, changes no favourite, and says to try again.
- A restore puts a backup's Scheduler24 back as the Flying Clock, and says so, on a garland without the clock module or of 1.1.1 or before, which refuse it, with the rest of the screen as the backup has it.
- A restore that puts the playlist back drops a draft of it on the Effects page and shows the list restored, where the draft hid it. One whose backup holds no playlist, or that the garland refused, leaves the draft, and so does an edit made while the restore ran.
- After a firmware update it sent has confirmed itself, the utility opens Info and holds the garland to its API; an upload cancelled and not installed stays on Firmware, with no check of the API. It follows an update for up to 12 minutes, where it gave up after 150 s.
- Leaving a garland while the catalog of releases is read no longer reports its end on the next page, and an attempt to reach or pair a garland given up for a newer one no longer frees the newer one's buttons before it ends.
- On Linux a file dialog still open when the utility closes goes with it, the desktop portal's or zenity's.

### Site

- The main page shows the figurines in their order along the cable, and says the current limit as the owner's power settings give it, 1400 mA by default, and the trial a new firmware passes. The roadmap lists 1.4.0 and what it brought.

### Build and release

- `ota-update` waits up to 12 minutes for a new firmware to confirm, and takes only a pairing wait of 1 to 600 seconds from the garland. With `-Insecure` / `--insecure`, a new firmware whose HTTPS failed is asked over plain HTTP with the script's token, so that it has the paired request it confirms itself by, and what it runs is held to the update by its own answer, where the script read only its open log.
- Replacing the signing root asks for the word REPLACE, and says that a sealed garland on the old root takes no update signed under the new one, where it said such a board takes the next firmware over USB.
- `build-utility.ps1`, `flash-device.ps1` and `ota-update.ps1` let go of their build lock however they end, also when run inside another script's PowerShell, as `run.ps1` runs `build-utility.ps1`. A build after it, the Linux one in its container too, found the lock held until that PowerShell was closed.
- `build-utility` writes its build counter only over the number before it, and stops when another build took a number meanwhile, as one on Windows and one in the Linux container on the same checkout can.
- `flash-device.ps1` refuses a build that recorded no zlib, as `flash-device.sh` does, where it wrote an empty one or failed on it. `new-third-party-notices.ps1` says when Go is missing, and fetches a module from anywhere it is run, as its bash version does.
- The digest of the engine's sources sorts them as the bash version does whatever the computer's language, the same digest as before for today's files. Git's lists of files are read whole, a path that is not ASCII as it is, and a copy of the sources for a build stops at the first file that fails.
- `publish-release` needs `gh` only to publish, not for `-SiteOnly`, writes the site's dates and sizes alike whatever the computer's language, keeps the notes in `temp/` and takes its copies away however it ends.
- The source rules hold Python and PowerShell files to the source header too.

## [1.3.0] - 2026-11-06

A release of the utility alone, for Windows, Linux and now macOS. It works with the firmware 1.1.0 and 1.1.1 alike.

### Utility

- Runs on macOS: the token stays in the login keychain, files are chosen with the system's own panels, which open as a sheet over the window, and a second start brings the first window to the front, as on Linux. On macOS the build makes an application bundle, `csiGarland` with its icon in the Dock, which asks before it reaches a garland on the local network.
- A Mac's name for itself loses the `.local` of its network on the garland's list of what has access, and with no name at all the utility is called `csiGarland on macOS` there.

### Site

- On a phone the picture preview's buttons are glyphs in one row that fits the screen, 1:1 pressed a second time fits the picture again, the four screens stand in the middle, and the navigation runs in rows from the left rather than out to both edges.

### Build and release

- `build-utility.ps1` and `build-utility.sh`, building a number named again, put back the build that was there when the run fails or is stopped while moving it aside, instead of deleting it, and leave it in the run's folder under `temp/` if it cannot be put back.
- On macOS the utility's file is its application bundle zipped, `csigarland-<version>-darwin-arm64.zip` or `-darwin-amd64.zip`, `csiGarland.app` in it with its icon, `Info.plist` and an ad hoc signature: `build-utility.sh` makes it the same to the byte from the same sources, unzips the bundle beside it, and `run.sh` starts that. `build-utility.ps1` refuses to build on macOS, and names the bash script. `cmd/csigarland/generate_icon.go` makes `app.icns` beside `app.ico`, whose bytes stay as they were.
- macOS is released: Apple silicon and Intel, built with the clang of Apple's command line tools for macOS 26 on a Mac, and published from any computer with `-Platform darwin-arm64` or `darwin-amd64`. The utility's source digest takes in `deploy/lib/macos_bundle.sh`; a release before it holds none, so its digest is as it was.
- The utility's build counter is written whole or not at all, so a run that fails or is stopped while writing it keeps the number before, and a counter that holds no number, empty included, stops the build instead of numbering from 1 again, which gave out numbers already used.

## [1.1.1] - 2026-11-06

A release of the firmware alone, after 1.1.0. Everything a 1.1.0 garland saved, settings, pairings, uploaded effects, favourites, the schedule, the playlist and its root certificate, stays as it was after this update, and a garland gone back to 1.1.0 reads what this one saves as it did. The utility 1.2.0 and 1.3.0 work with it.

### Firmware

- The figurines' order along the cable, which effects use to find a figurine by name, is now tree, star, snowman, pinecone, lantern, bell, icicle, gingerbread, repeated, chosen so that neither colours nor shapes bunch together. A garland assembled in the order before lights other figurines in `Figurine Colors`, `Ring the Bells`, `Star Shine` and `Figurine Parade`; nothing it saved changes.
- `Figurine Colors` and `Figurine Parade` light each figurine in the colour of its new filament.
- A garland whose settings cannot be read at a start keeps a way to its next update: it opens its setup network, the password on the screen, where plain HTTP takes an update, instead of starting no network at all. What it saved stays as it is, its playlist is read again until it can be, and a new firmware that cannot read its settings still goes back. Its uploaded effects still load, and its LED count, favourites and screen settings are read again until they can be, with nothing saved over them meanwhile: until then the portal and the utility are told 503 for the effects list and the screen settings, rather than shown the defaults as what is saved, which a page or a backup would keep.
- An update whose check of its last bytes fails lets go of what the check held.
- A button pressed while the garland is still starting outranks the schedule's steps missed before it, as a later choice does, instead of an older missed step overriding it, and a turn on or off made then is saved at once.
- A scheduled step that gave way to a choice made while it was worked out no longer keeps a playlist left playing from coming back at a start.
- A remembered choice read only after a start's first tries failed now brings back the schedule's steps missed with no power since it, which were lost for good before, a step done meanwhile among them, and keeps the time it was saved with instead of being dated by that start.
- What a start brings back late, the remembered choice and a playlist left playing, gives way to a choice made with a button or a page while it was being read, instead of overriding it, and a turn off made then is saved at once. A turn on or off, or a change of the brightness or speed, no longer keeps that playlist from coming back.
- A playlist resumed while the schedule cannot be read no longer passes by the steps missed before the start, such as an Off, once the schedule is read.
- An uploaded effect whose file could not be read at a start no longer comes back after the same effect is uploaded again and deleted: until it is read, an upload under an id the garland does not know is refused, 503.
- Paired browsers that cannot be read at a start are no longer taken for none: pairing a new browser no longer saves over them, so they are not forgotten, and they are let in again once a later request reads them. Meanwhile the garland answers 503, not 401, so the utility keeps its token.

### Portal

- A paired browser's cookie counts only from a page of the same scheme as the server it reaches: a page on plain HTTP under the garland's name can no longer act with the cookie of HTTPS, which Firefox and Safari would send with it, so whoever intercepts plain HTTP cannot use it. The portal's own pages send what they did.

### Public API

- A route answers 503 where it would otherwise show or save defaults in place of what the garland could not read: every route that wants pairing while the paired browsers cannot be read, with `Retry-After`, `GET /api/v1/effects` and the favourite while the favourites cannot be, `/api/v1/display` while the screen settings cannot be, and an upload under an id the garland does not know while an uploaded effect cannot be. A program asks again later.

### Build and release

- `ota-update.sh` hands curl the token, and the id it is collected with, in files of yours alone, no longer on its command line, where anyone on the computer could read them in the list of processes. It needs curl 7.55 or later, as Linux and macOS have it.

## [1.2.0] - 2026-11-05

A release of the utility alone, for Windows and now for Linux: the firmware stays 1.1.0.113, and the utility works with it as before.

### Utility

- Runs on Linux: the token stays in the Secret Service, as GNOME Keyring and KWallet give it, files are chosen with the desktop portal's dialog, or zenity's where there is no portal, and a second start asks the first window forward and ends. The addresses and roots live in `~/.config/csiGarland`.
- A file for Linux on x86-64, built for Ubuntu 24.04 and running on Ubuntu 22.04 and later with a desktop, which has the Wayland, X11 and EGL libraries it uses.
- The preview finds a figurine by name in the new order along the cable, and draws `Figurine Colors` and `Figurine Parade` in the new filaments' colours, as the firmware after 1.1.0 will; a garland on 1.1.0 still lights them in the order and colours before.

### Build and release

- Every script of `deploy/` has a bash twin for Linux and macOS, `deploy/<name>.sh`, with the same checks, and the same counters, manifests and digests to the byte, so the two can take turns in one checkout. They run with bash 3.2 and the BSD tools of macOS as well as on Linux, and `deploy/tests/shell-lib.tests.sh` holds them to the digests the PowerShell scripts recorded for 1.1.0.
- The digest of the utility's sources takes in the bash scripts that build it as well. A release before this one holds none of them, so its digest is as it was.
- The firmware's image holds the portal's pages gzipped by the zlib of the Python that builds it, and another zlib packs them into other bytes. `CSIGARLAND_RELEASE_ZLIB` in `platformio.ini` names the one a release is built with, `1.3.1.zlib-ng` of Python 3.14 on Windows, as 1.1.0 was: a build records the one it used in its manifest and warns of another, and a release refuses another.
- The Linux utility's release compiler is the GCC of Ubuntu 24.04, and on Windows it is built in a Docker container of that Ubuntu, pinned by its image digest, the day of its packages and Go's checksum, as `docs/guides/utility.md` says, so it builds again to the byte there and on Ubuntu.
- The build counters are kept in git, so a clone on another computer goes on from the last number.

### Hardware

- The figurines print in new colours: tree Translucent Olive, star Translucent Light Blue, icicle and snowman Clear, pinecone Translucent Brown, bell Translucent Teal, gingerbread man Translucent Orange, lantern Translucent Gray. `csigarland_full_set.3mf` follows them, with tree supports on every figurine's plate.

## [1.1.0] - 2026-11-04

Everything a 1.0.0 garland saved, settings, pairings, uploaded effects, favourites, the schedule and its root certificate, stays as it was after this update. What this one saves reads in 1.0.0 as it means here, should a garland go back to it: a rise as a plain turn on, and the playlist as not set.

### Firmware

- The light fades in over 0.8 s when the garland turns on, and out over 0.8 s before the rail goes off, by the buttons, the portal, the countdown or the schedule.
- A scheduled action can turn the garland on rising from dark over a minute, or start the playlist.
- A playlist: up to 16 effects played in turn, each for 10 s to an hour, started and stopped from the portal or the utility, and remembered across a restart. Choosing a single effect, by any button, page or action, stops it, and the playlist moving on is no choice of the owner's that would outrank the schedule.
- With a playlist set, a long press of EFFECT starts it, or stops it and keeps what plays. Going back to the first favourite is now a double click of EFFECT.
- While the playlist plays, the screen's title says where it stands, as `Playing 3/12`.
- Every change of effect cross-fades from the last frame of the one before, over 0 to 5 s, 1 s unless set otherwise with the playlist.
- Push updates: the garland sends its state as it changes, over a stream of Server-Sent Events, to two pages or programs at a time.
- A schedule with a rise or a playlist step is not replaced by a program that knows neither, such as the 1.0.0 utility: it is told to update.
- A domain setting that could not be read, or a key that could not be read for want of memory, no longer makes the garland replace its HTTPS root: it keeps its keys, and serves HTTP until a start reads them. A portal key that cannot be read is made again under the root kept, which browsers trust already.
- A playlist keeps an effect the garland lacks, as after an update of the effects, by its id, and plays it again once it is back.
- A scheduled action naming an effect the garland lacks, as after an update of the effects, stays, and a change of another action no longer has the whole list refused for it.
- The setup network's exception from pairing holds only while that network is up, and never for a home network of the same addresses.
- A client sending slowly can no longer hold up the portal, an update among it: a request's headers, its body and an update each have a time limit, and a connection past it, or one whose request was answered before its body was read, is closed. A connection left idle is closed after 10 s. A renewal of the certificate waits for an update being received rather than restart in the middle of it.
- An Off by the POWER button's countdown during an update stays after the restart that ends it.
- A list of favourites too long to save whole is refused, and nothing is changed, where it was saved in part and lost the rest at the next start.
- An effect file with a NUL byte in it is refused whole, on upload and on preview.
- A change of the chain's length is drawn whole, and held to the 1400 mA limit, before a frame goes out over it: one sent in between could carry an older frame's tail past the limit, by the firmware's own model up to some 2.6 A for full white from 32 back to 256 LEDs.

### Portal

- The Effects page holds the playlist: its effects, how long each plays, the fade between them, and Play or Stop. A list saved from another page or program since it was read is not written over: the page says so, and reads it again. Its Now says where the playlist stands, and follows the garland as it changes, asking for nothing while the stream is open.
- The Schedule page shows the next 24 hours, by the garland's clock: when the garland is lit, each action due, and where actions meet in one minute, in place of the next action alone. Its form has the rise from dark and the playlist.
- An upload whose connection was lost is no longer called refused at once: the page watches what the garland then runs, and says whether it took the file.

### Public API

- `GET` and `POST /api/v1/playlist`, `POST /api/v1/playlist/play`, `GET /api/v1/schedule/timeline`, and `GET /api/v1/events`, the push updates. `GET /api/v1/state` says how far the scheduler has done its steps, `steps_done_until`, whether the playlist plays and where it stands, and a scheduled action may hold the power step `rise` and `"playlist": true`. Nothing that was there has changed.

### Utility

- The Effects page holds the playlist; the Schedule page shows the next 24 hours in place of the next action alone, and its form has the rise from dark and the playlist. The rail says `Playlist 3/12 (Fireplace)` while it plays.
- The window follows the garland's pushed state, on a connection of its own, and asks for it only when there is no stream.
- Tune shows the current an effect draws, by the firmware's own model, over the 20 s the preview plays in, with a line that moves with the preview, at the brightness chosen, and where the 1400 mA limit holds it back.
- The Info page saves the garland's schedule, favourites, playlist, screen and uploaded effects to a backup file, and puts them back on this garland or another.
- After an update confirms itself, the utility reads every route of the public API and says whether each answer reads as it expects. The Firmware page's Check the API does the same at any time.
- After a start, the footer counts down a firmware's trial until it confirms itself, as it did only during an update sent from here.
- Offers the newest release only when the garland's firmware is older; a Dev build counts as its number.
- A download from the catalog that ends after a file was chosen, or while the file dialog is open, leaves that file chosen, and installs nothing.
- `THIRD_PARTY_NOTICES.md` names what the utility's own file holds: Go's standard library and runtime, and the C++ runtime of GCC, winpthreads and mingw-w64's runtime it links statically. A build that links any other of the compiler's libraries fails.

### Build and release

- `deploy\publish-release.ps1` takes `-Release`, `-FirmwareVersion` and `-UtilityVersion`, publishes only files built from what the tag holds, checked by their sources' digests against a checkout of the tag, and an update file signed under the root published with it, and writes its release notes from this changelog. `-BuildNew` builds the firmware and the utility first, and releases what they made. `-Only Firmware` or `-Only Utility` publishes one of them, the utility for the platform `-Platform` names, and a later run adds to a release already made.
- A manifest's `source_sha256` is worked out from each source file's bytes, as `source_format` `raw-blobs/1` says. The manifests of 1.0.0 record the older form, and are not compared with it.
- The Release profile builds as the PlatformIO environment `esp32c6-release`, in a folder of its own, and the deploy scripts keep the objects through a change of `platformio.ini` or of the list of sources: a build compiles only what changed. `CSIGARLAND_PROFILE` no longer picks the profile, and `ota-update.ps1 -NoBuild` asks for `-BuildProfile` when both profiles are built, and refuses a build left of another profile or version, as one of 1.0.0. A Release build makes its `sdkconfig` again from the defaults when it differs from what they made, as after an edit by hand.
- A build that fails, is stopped, or fails a check after it gives its build number back, for the firmware and the utility, so the next build takes it.
- A firmware build of a checkout runs alone, and refuses to run beside another, so two never take one number; the utility's build does the same, and builds in a folder of its own, so a failed one leaves the file there as it was.
- `publish-release.ps1` checks a file the release already holds by its SHA-256 on GitHub, dates each build on the site by its own file, keeps the firmware catalog within what the utility of 1.0.0 reads, and checks the tag before `-BuildNew` builds anything.
- `ota-update.ps1` goes on watching the garland when an update gets no answer at all, as when the connection drops while it restarts, since it may have been installed, and says what it finds, calling it confirmed only when the garland shows it took this file.
- The guides install PlatformIO with the signing tool's `cryptography`, as a first build needs it before ESP-IDF's environment exists, and the scripts take the Python pio runs from, as the build does.

### Site

- The Downloads page lists each product once, the utility with Windows, Linux and macOS under it, every release newest first, with the day each build was released, and from 1.1.0 on, a link to its release notes. The documents and the roadmap say that released firmware is ready to download and install, and what has been seen working on the owner's garland.

### Hardware

- The star diffuser is an even 1.2 mm hollow shell that follows its facets. The solid rays with an ellipsoid chamber printed floating walls that showed through the translucent PETG near the centre. `csigarland_full_set.3mf` is rebuilt with it: the star plate takes 1 h 09 min and 17 g, where it took 1 h 30 min and 20 g.

## [1.0.0] - 2026-11-03

The first release: firmware 1.0.0.102 and the csiGarland utility 1.0.0.27 for Windows. It is the baseline, and nothing from before it is kept compatible with.

### Firmware

- 32 WS2812B LEDs by default, chains of up to 256 set by hand or estimated by a probe on the gated rail, with the 1400 mA limit applied to every frame and the power sequence of data low, gate, output.
- 154 effects in groups, from 98 algorithms in up to four layers, in an effects pack apart from the firmware, which holds up to 256 effects, in flash slots with room for 512 of today's size, favourites for the EFFECT button, and up to 32 effects uploaded or previewed from files.
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
