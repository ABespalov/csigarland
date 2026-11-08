# csiGarland User Guide

Using a finished garland: the buttons, the screen, getting into the portal, and every page of it.

## 1. Buttons

Three buttons below the screen. A long press is 0.8 s.

| Button | Click | Long press |
|---|---|---|
| **POWER** (red) | In standby: turns on at the remembered brightness, 50 % at first. When on: steps brightness 10, 25, 50, 75, 100, 10 % | When on: counts down 5 s on the screen, then turns off |
| **EFFECT** (white) | When on: the next favourite effect. **Double click:** the first favourite | When on, with a playlist set: starts the playlist, or stops it and keeps what plays |
| **SPEED** (blue or black) | When on: steps speed 0.25x, 0.5x, 0.75x, 1x, 1.5x, 2x, 3x, 0.25x | When on: back to 1x |

* **Any button** cancels the countdown, or wakes the screen from a screensaver. That press does nothing else.
* **POWER and SPEED together** answer a browser's request for access, see [Pairing](#4-pairing-a-browser). **Held for 10 s,** they forget every paired browser, for a lost phone. A hold that has just answered a request forgets nothing.
* **While a request for access waits,** a click of any button but that pair refuses the request instead of doing what it would.

The light fades in over 0.8 s when the garland turns on, and out over 0.8 s before it goes dark, however it was turned on or off. A press that turns it on again on its way to dark brings it back up from there.

The effect, brightness, speed, whether it was playing and whether the playlist was playing all survive a power cut. Turning on or off is saved at once, and the rest once it has stayed unchanged for 10 seconds.

## 2. Connecting

1. Plug in the garland. With no home network set, the screen shows the network `csiGarland-Setup` and a password, which is this board's own.
2. Join that network from a phone or computer. A phone opens the portal by itself; otherwise go to `http://192.168.4.1/network`.
3. Choose your home network and enter its password. It must be 2.4 GHz.
4. The screen then shows the garland's address, and the setup network turns off about a minute later.

If the home network goes away, the setup network comes back after 20 s, and the garland keeps trying to reconnect. The password is on the screen whenever that network runs.

## 3. Ways Into the Portal

| From | Address | Security |
|---|---|---|
| The setup network | `http://192.168.4.1` | Plain HTTP and no pairing. Joining took the password on the screen, and phones' login windows refuse our certificate and keep no cookies |
| Your home network | `https://csigarland.local` | HTTPS and a paired browser. Port 80 redirects there |
| The internet, if you set it up | `https://your-domain` | The same. See [section 6](#6-reaching-it-from-the-internet) |

`.local` names work on most systems. If one does not, `https://` and the address on the screen also reach the garland, but with a certificate warning, as a home network's address can change. To go without the warning, reserve the address on your router and enter it on the Network page, as in [section 6](#6-reaching-it-from-the-internet). The name can be changed on the Network page, which restarts the garland.

## 4. Pairing a Browser

There is no password. A browser asks, and you answer at the garland:

1. The browser shows **Allow this browser**. Press **Ask for access**.
2. The browser and the garland's screen show a four-digit code.

   <img src="../images/portal/pair.png" width="300" alt="The browser showing the code"> <img src="../images/screens/access.png" width="200" alt="The garland's screen showing the same code">

3. **If they match,** press POWER and SPEED together, and let go. **If they differ,** someone else is asking: click any one button, which refuses and holds off new requests for a minute. The page counts that minute down.

A request waits for 60 s, and only one waits at a time. Access lasts 100 days on that browser and is not extended by use, after which you pair again. Up to 8 browsers can have access; a ninth takes the place of the one paired longest ago. The [Info page](#info) lists them and forgets them one by one.

Before you press, look at the address bar: `https://` with no warning, once you have [installed the root](#5-stopping-the-certificate-warning). Someone between you and the garland could otherwise show you the very code on its screen, and your press would give them the access.

Open to anyone with no pairing: the pages themselves, which hold no data until the browser has access, the root certificate, and the request for access.

## 5. Stopping the Certificate Warning

The garland is its own small certificate authority, so a browser warns until its root is installed. Do this once per device.

1. Download `csigarland-root.crt` from the Network page. The pairing page links to it too, as a browser may warn before you have paired.
2. Compare its fingerprint with the SHA-256 on the Network page. That is the hash of the certificate itself, not of the file: on Windows `certutil -dump csigarland-root.crt` shows it as `Cert Hash(sha256)`, elsewhere `openssl x509 -in csigarland-root.crt -noout -fingerprint -sha256`. The page comes over the same connection as the file, so a match shows that the file is the one the page names, not that nobody stood between you and the garland: install the root while on a network you trust. Whoever built the garland can also hold it to the board's log, as [Building](build-and-flash.md#checking-the-root-against-the-log) says.
3. Install it:

| System | Steps |
|---|---|
| Windows | Open the file, Install Certificate, Current User, Place all certificates in: Trusted Root Certification Authorities |
| macOS | Open the file, add it to the login keychain, open it there, Trust, When using this certificate: Always Trust |
| iPhone, iPad | Open the file, Settings, Profile Downloaded, Install. Then General, About, Certificate Trust Settings: turn it on |
| Android | Settings, Security, Encryption and credentials, Install a certificate, CA certificate |
| Firefox | Keeps its own list: Settings, Privacy and Security, View Certificates, Authorities, Import |

4. **Restart the browser fully,** all its windows. In Chrome or Edge, go to `chrome://restart` or `edge://restart`. A running browser does not notice a new root, and remembers having gone past the warning.

**What you are trusting.** The root's name constraints let it vouch only for `.local` names, `192.168.4.1` and your one domain or fixed address. Even if its key were taken from the board, it could not impersonate your bank to a device that installed it.

**If the warning stays:** click it, and look at who issued the certificate. An antivirus that filters HTTPS, such as ESET, shows its own name there: add the garland's addresses to its exclusions. If it says `csiGarland root`, check that the file you installed is the current one by its certificate's SHA-256 fingerprint, as in step 2. Each domain change makes a new root, and the old one can be removed from your system's list.

## 6. Reaching It From the Internet

1. On your router, forward an outside port to port 443 of the garland, and give the garland a fixed address there. Do not forward port 80: it only redirects.
2. Point a DNS name at your router, such as `garland.example.com`. A fixed outside IP address will do instead of a name.
3. On the Network page, enter that domain or address under **HTTPS certificate** and save. The garland restarts with new certificates.
4. **Install the root again on every device,** as the domain or address is part of it.

One domain or one address, not both. The same field takes the garland's reserved address on the home network, for reaching it by number without a warning. The keys stay the same through this: certificates are made anew on the keys the board has, so there is nothing to regenerate by hand. A new mDNS name needs no new root, as it stays under `.local`.

**Before opening a port,** note what a stranger can reach: the pages themselves and the requests for access, as every other route wants a paired browser. Each stranger's request shows on the screen and can be refused. Nothing limits how often the handshake can be attempted, and the board is a small one. **If the Info page says HTTPS is not working, close the port** until a fix is installed: everything then travels unencrypted.

## 7. The Screen

The colour says what is going on:

| Colour | Means | Screens |
|---|---|---|
| Grey | Nothing is happening | Standby |
| Blue | Running, and all is well | Playing: the effect, brightness, speed, LED count, which reads `!30` if a new length could not be set and 30 are still in use. While the playlist plays, the title says where it stands, `Playing 3/12`, in a smaller font |
| Amber | You have something to do, or must not interfere | No LEDs, setup network, power-off countdown, update under way, access request, making keys |
| Green | An operation finished well | Update installed |

The header shows the firmware version. The wordmark is white and blue on any coloured frame, and on a grey one it and the version go grey too. With a clock module fitted, a small clock glyph sits at the end of the wordmark, and turns white when a scheduled action comes within a day.

The footer shows the portal's address. In the LED count, `N` is set by hand, `~N` is estimated, `0` means none are detected, and `?` means present but not measurable.

| | | |
|---|---|---|
| <img src="../images/screens/standby.png" width="200" alt="Standby screen"><br>Standby | <img src="../images/screens/playing.png" width="200" alt="Playing screen"><br>Playing | <img src="../images/screens/no-leds.png" width="200" alt="No LEDs screen"><br>No LEDs |
| <img src="../images/screens/setup-network.png" width="200" alt="Setup network screen"><br>Setup network | <img src="../images/screens/access.png" width="200" alt="Access request screen"><br>Access request | <img src="../images/screens/power-off.png" width="200" alt="Power-off countdown screen"><br>Power-off countdown |
| <img src="../images/screens/update-receiving.png" width="200" alt="Update being received"><br>Update being received | <img src="../images/screens/update-restarting.png" width="200" alt="Update installed"><br>Update installed | <img src="../images/screens/making-key.png" width="200" alt="Making the HTTPS key"><br>Making the HTTPS key |

<img src="../images/screens/standby-action-soon.png" width="200" alt="Standby screen with the clock glyph white"><br>Standby with a scheduled action within a day: the clock glyph at the top right is white.

<img src="../images/screens/playing-playlist.png" width="200" alt="Playing screen with the playlist's place in the title"><br>Playing the playlist: where it stands, here the last of 16.

The screensavers, chosen on the Device page. Black Screen turns the backlight off, and None keeps the screen above. Scheduler24, for a garland with the clock module, is new in firmware 1.4.0.

| | | |
|---|---|---|
| <img src="../images/screens/saver-starfield.png" width="200" alt="Starfield screensaver"><br>Starfield | <img src="../images/screens/saver-flying-clock.png" width="200" alt="Flying Clock screensaver"><br>Flying Clock | <img src="../images/screens/saver-breathing-dot.png" width="200" alt="Breathing Dot screensaver"><br>Breathing Dot |
| <img src="../images/screens/saver-scheduler24.png" width="200" alt="Scheduler24 with the next scheduled step"><br>Scheduler24: the next scheduled step | <img src="../images/screens/saver-scheduler24-wrapped.png" width="200" alt="Scheduler24 with a long name wrapped"><br>Scheduler24: a long name wrapped | <img src="../images/screens/saver-scheduler24-last.png" width="200" alt="Scheduler24 with the last event"><br>Scheduler24: the last event, with nothing to come |

The pictures come from the firmware's own drawing code with sample data. The version, address and password on yours differ.

## 8. Portal Pages

Every page has the same header, with the firmware version on the right. Colours of buttons: blue is the main action of its block, grey is any other, red text loses something. The pictures are the real pages with sample data.

### Effects

`/`

<img src="../images/portal/effects.png" width="360" alt="The Effects page">

* **Now** shows on or off and the effect, and follows the buttons at once, as the garland pushes each change; if it cannot, with two pages or programs following it already, it asks every few seconds. Its sliders set the brightness, from 1 to 100 %, and the speed, from 0.25x to 5x in steps of 0.25, and the garland follows as you move them. While the garland is off they do not turn it on: it shows them at the next turn on. From a speed the slider set, the SPEED button goes to its next step up, or to 0.25x from 3x or more. The choice is saved once it has stayed unchanged for 10 seconds.
* **Effects** lists all of them in eight groups, then **Uploaded**, which holds what you added from files, a file replacing a built-in effect among them, and is there even while empty. A group opens by itself if it holds a favourite or what is playing, and Uploaded whenever it holds anything. A title such as `Classic Garland (8 of 16)` says how many of its effects are favourites.
* **The tick** puts an effect in the EFFECT button's cycle. At least one must stay ticked.
* **Play** starts any effect, ticked or not, and turns the garland on. **Active** marks what plays. After a non-favourite, the button goes on with the next favourite.
* **Playlist** plays up to 16 effects in turn, each for 10 s to an hour, with a fade from one to the next of 0 to 5 s. Add effects from the list under it, remove one with its bin, and **Save playlist**; **Play the playlist** turns the garland on at its first effect, and **Stop the playlist** keeps what plays. Choosing a single effect, here, by the EFFECT button or by a scheduled action, stops it too. The fade holds for every change of effect, not the playlist's alone. An uploaded effect in the playlist cannot be deleted until it is taken out. One the garland lacks, as after an update of the effects, stays in the list marked "not on this garland", and plays again once it is back.
* **Add an effect from a file** takes a file in [the effect format](effects.md#6-effect-file). **Preview** plays it without saving, until another effect is chosen. **Save** keeps it, up to 32, and the file's name is its id. A file named as a built-in effect replaces it until you press **Restore**; one of your own has **Delete**.

### Schedule

`/schedule`

<img src="../images/portal/schedule.png" width="360" alt="The Schedule page">

* **The next 24 hours** starts with the garland's time. Actions wait until the clock is set. They go by local time, so in a zone with clock changes, an action in the hour repeated in autumn runs twice, and one in the hour skipped in spring does not run that day.
* **The next 24 hours** draws, from now, when the garland is lit as the actions leave it, with a mark at each action due, amber where actions meet in one minute, and lists them. Where they meet, the later in the list win, step by step.
* **Actions:** up to 32, each with a time, days of the week, and either **Turn off**, or steps: turn on, which can rise from dark over a minute, brightness 1-100 %, speed, start an effect or start the playlist. Nothing can go with off, and starting an effect or the playlist also turns the garland on; a playlist left empty then starts nothing. A rise is for a garland turned on from dark: one already lit stays as it is.
* **Time zone:** the first browser to open the portal sets it from its own. Change it here if that is wrong.
* **Clock module:** appears if one is fitted. It can be set from the device you are on, from the internet, or by hand. The next answer from the internet overrules a time set here.

The garland comes back from a power cut as it was left. Then, at the first time it knows the time, it carries out only the actions it missed while it had no power, as one result, up to the last 24 hours. Anything you chose counts over any action from before your choice. A choice made while it does not know the time counts over all of them.

### Device

`/device`

<img src="../images/portal/device.png" width="360" alt="The Device page">

* **LED count:** detected automatically, or set by hand, 1 to 256. An exact count cannot be detected, so if the figure is wrong, set it.
* **Power,** new in firmware 1.4.0: the garland dims a frame that would draw more than its limit, as a whole. Up to 1.1.1 the limit is a fixed 1400 mA, and the page has no power settings.
  * **The limit** is what your power supply gives beyond 250 mA for the board, the screen and Wi-Fi, less a margin of that: (2000 - 250) * 80 % = 1400 mA with the 2000 mA supply and the 20 % it comes with. At most it is (4400 - 250) * 95 % = 3942 mA, about 3.9 A.
  * **Power supply:** 500 to 4400 mA, 2000 mA by default. A larger supply, or a smaller margin, lets more of the chain show full white. **Set only what your supply, the garland's wire and its connector are all rated for:** the limit goes by these figures alone, and cannot know what the parts can take.
  * **One LED at full white,** above what it draws dark: 5 to 100 mA, 40 mA by default.
  * **One LED dark:** 0 to 5 mA, 1 mA by default. Keep both defaults unless your LEDs were measured otherwise.
  * **Margin:** 5 to 20 %, 20 % by default.
  * **As you type,** the page works out the limit, what the chain draws at full white, and how many LEDs show full white with nothing dimmed.
  * **The garland stays dark,** and lights by itself once that changes, while these settings cannot be read, or while what the chain draws dark alone, as many LEDs times the dark figure, is over the limit, which dimming cannot help. The page then says so in place of what full white draws.
  * **Saved settings that are not valid** give the most cautious ones: a 500 mA supply, 100 mA at full white, 5 mA dark and a 20 % margin, a limit of 200 mA. The Device page, in the portal and in the utility, warns of it until you save the settings again.
* **Screen:** saved and applied at once.
  * **Its brightness.**
  * **The screensaver:** None, Starfield, Flying Clock, Breathing Dot, Black Screen or, with the clock module, Scheduler24, new in firmware 1.4.0.
  * **The screensaver's delay,** 10-600 s, and **its brightness.**
* **Scheduler24,** new in firmware 1.4.0, flies across the screen as the clock does:
  * **What comes next and how soon:** the next scheduled action within a day, or the playlist's next effect while it plays, whichever is sooner, as `next: Fireplace` and `in 2h 05m`.
  * **With nothing to come, what happened last and how long ago:** the garland's start, a turn on or off by any button, page or action, the LEDs going on or off apart from that, as when the power settings hold them off, or a scheduled action done, as `last: Off` and `3h 10m ago`.
  * **Its words** take no more than 80 % of the screen's width, wrapping to more lines up to 80 % of its height, and past that a smaller letter.

### Network

`/network`

<img src="../images/portal/network.png" width="360" alt="The Network page">

Buttons show a glyph and what they do, side by side. Those on a row of a list, such as an effect's or a scheduled action's, are a glyph alone to keep the row short, and on a computer, hovering over one says what it does. These pages name them by those words.

* **Wi-Fi:** **Find networks**, or type a name, then **Connect**. With the password left empty, the network already saved keeps the one it has. **Forget** removes the network, and the setup network comes back.
* **mDNS name:** lowercase letters, digits and hyphens, without `.local`. Saving a new one restarts the garland, as the portal's certificate holds the name. The root stays, so nothing needs installing again.
* **HTTPS certificate:** the root's SHA-256 and its download, how to install it, and your one domain or fixed address, see [section 6](#6-reaching-it-from-the-internet). Saving it restarts the garland.

### Firmware

`/firmware`

<img src="../images/portal/firmware.png" width="360" alt="The Firmware page">

Shows the running version. Choose an `.ota` file, and the page says what it holds before you install it: firmware, effects, or both. Only what it holds is installed. See [Updates](#10-updates).

### Info

`/info`

<img src="../images/portal/info.png" width="360" alt="The Info page">

* **Device:** model, chip, MAC.
* **Memory:** free RAM, the lowest it has been since the start, and the firmware's room in flash.
* **Network:** the addresses, the mDNS name, the certificate's names and end date. A line **HTTPS: Not working** means the portal is on plain HTTP; install a new firmware. A line **LED output: Not working** means frames are not reaching the garland, and the firmware keeps trying to start the output again.
* **Effects and mode,** and the **next scheduled action.**
* **Browsers with access,** each with **Forget**. The one you are on says so.

### Pairing

`/pair`

Where a browser with no access lands, see [section 4](#4-pairing-a-browser).

## 9. What the Portal Does Not Have

There is no power switch on a page: that is on the buttons and the schedule, though playing an effect from the page turns the garland on. There is no sleep timer, and no editor of palettes or figurine zones. No current sensor is fitted, so the firmware only estimates current, for its limiter.

## 10. Updates

A garland takes an update file signed under the root its firmware holds.

* **The published releases** are on the [Downloads](https://garland.csilab.am/downloads.html) page and in the utility's catalog, signed under the project's root, for a garland whose firmware is a release. A release's update file, `csigarland-<version>.ota`, carries the firmware and the effects.
* **A garland you built and flashed with keys of your own** takes your own builds: see [Building](build-and-flash.md).

Where to install one from:

* **The portal:** the [Firmware page](#firmware).
* **The utility:** its Firmware page, from a file or the catalog of releases, as [its guide](utility.md) says.
* **The build machine,** for a garland you built: `ota-update.ps1`, in [Building](build-and-flash.md#10-updates-from-the-build-machine).

**What happens:**

1. The garland refuses anything not signed under its root, before touching flash. The running firmware carries on.
2. The screen shows the progress, receiving and then applying the update. The LEDs hold their last frame.
3. It restarts, and the new firmware is **on trial**:
   * **A firmware of 1.4.0 or later** confirms itself only once it has also answered a request of a paired browser or program over the network, as the Firmware page, the utility and the build machine's script send while they wait for it; a refusal does not count. With no such request within 10 minutes, it goes back to the previous firmware. **So keep the page or the utility open until it says the update is done.**
   * **A firmware of 1.1.1 or before** has its first minute on trial, and confirms itself if it started a server through which the next update can come, and its buttons and screen.
4. If it restarts for any reason while on trial, a power cut included, the previous firmware comes back. **So do not unplug it until the update is done.**

| | | |
|---|---|---|
| <img src="../images/screens/update-receiving.png" width="200" alt="Update being received"><br>Receiving | <img src="../images/screens/update-applying.png" width="200" alt="Update being applied"><br>Applying: do not turn it off | <img src="../images/screens/update-restarting.png" width="200" alt="Update installed"><br>Installed, restarting |

The page and the utility report success only when the garland has restarted, runs exactly what was sent, told by the image's hash and the effects' digest rather than the version, and has confirmed it. They say so if it went back. The utility from 1.4.0, and the Firmware page of a garland running 1.4.0 or later, then show the [Info page](#info) by themselves.

A firmware that cannot start HTTPS stays up on plain HTTP so that the next update can reach it, and the Info page says **HTTPS: Not working**.

## 11. When Something Goes Wrong

| What you see | What to do |
|---|---|
| Certificate warning | [Section 5](#5-stopping-the-certificate-warning). After installing, restart the browser fully |
| `Allow this browser` comes back after approving | Check that you let go of the buttons before 10 s. Open the portal by `https://`, not `http://` |
| `Another request is waiting, or one was just refused` | Wait for the countdown. If it keeps happening with nobody asking, click any one button at each request, and close the forwarded port |
| A button seems to do nothing during a request | It refused the request. Ask again after the countdown |
| Portal answers on neither address, screen normal | The board is not on your network. After 20 s without it the setup network comes up |
| Forgot the setup password | It is on the screen whenever that network runs |
| Lost a phone | POWER and SPEED held for 10 s |
| Screen amber, says no LEDs | Check the garland's plug. If the garland works regardless, set the LED count on the Device page: detection only advises |
| A single effect, Warm white, plays | No readable effects pack. Install the release's update file from Downloads, which carries the firmware and the effects, or `effects.ota` from your own build |
| `Not installed: ... went back to the previous firmware` | The new firmware failed its trial, or, from 1.4.0, was asked nothing within 10 minutes of its restart. Nothing is lost, and another try is safe: keep the page or the utility open until it says done. From the build machine, the script keeps the trial's log under `temp/`, as [Building](build-and-flash.md#10-updates-from-the-build-machine) says |
| The schedule does nothing | The clock is not set: see the Schedule page. It needs the internet or a clock module |

Anything needing USB is in [Building](build-and-flash.md#9-when-something-goes-wrong).

**Signed older versions install too,** by design, so anyone with a paired browser can downgrade to any version you signed.
