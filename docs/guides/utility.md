# The csiGarland Utility

A program for your computer that does what the portal does, and two things it cannot: it tunes an effect with a preview drawn by the firmware's own engine, and it shows the garland's log. For now it runs on Windows. Ubuntu, macOS, Android and iOS come after, on the same code.

It talks to the garland over its [public API](../stubs/PUBLIC-API.md), which every release of the firmware has.

The garland does not depend on it, as it does not on the portal: buttons, screen, effects and the schedule work with neither.

## Build It

You need Go and a C++20 compiler on the `PATH`, as the effect engine is the firmware's C++ code: MinGW-w64 on Windows.

```bash
pwsh -File deploy\build-utility.ps1
```

The result is one file under `assets/utility`, with nothing to install beside it. No board or keys are needed.

Each build has its own number, apart from the firmware's: its base is in `cmd/csigarland/version.go`, and sources the same as the last build's keep its number and its file. `-Force` spends a new one. `-BuildProfile Dev` keeps the symbols and debug information, for a debugger, and marks the version `-dev`.

To build it and start it in one go, which starts nothing if the build failed, rather than the build before it. The script ends there, and the utility goes on by itself:

```bash
pwsh -File deploy\run.ps1
```

`-NoRun` builds and stops.

The icon is in `cmd/csigarland`, as `app.ico`, and `rsrc_windows_amd64.syso` puts it in the program. `generate_icon.go` there says how to make them again.

## The Window

As in the csiPass companion. The top bar has the wordmark, in the same pixel letters as the garland's screen and the portal, its tagline, and the pages. The rail under it says whether the garland is answering, its address and firmware, and what it is playing. **Refresh** reads the page again, and **Disconnect** leaves this garland for the list, where it stays paired and comes first. The utility still goes back to the last one used when it next starts. The footer is one line for what just happened, or the slogan when nothing did, and the utility's version.

Buttons show a glyph and what they do, side by side and on to a new line if the window is narrow. Those that act on one row of a table, such as an effect's or a schedule's, are a glyph alone to keep the row short, and hovering over one says what it does. Whatever loses something, such as deleting or forgetting, asks by a second press within 4 s.

A line too long for the footer is whole in the Journal, beside it, which keeps what the footer has said since the window opened, newest first. Its button turns red while there are errors you have not looked at.

Only one utility runs at a time: starting it again brings the one already open to the front. Its window is at least 1024 by 768, as csiPass's is, and the system's dialogs for choosing a file open over it. Beside a hash or a MAC, a small button puts it on the clipboard.

## Connect and Pair

The utility looks for garlands on your network, and lists those that say they are one, with their firmware. It remembers those it has been connected to, and lists them whether they answer or not, with the last one used first and a green tick on those it is paired with. It knows them by their root certificate, so a new address changes nothing. A new address found by the search is kept only once the garland has answered there under its own root: anything can answer a search, and something there without that root is left, with no offer to trust it. Choosing a paired one connects at once, and it is also where the utility goes when it starts. Forgetting one here leaves it paired on the garland until its time runs out. Another garland at an address one used to have takes nothing from the first: each is kept by its root. So one whose root has changed stays listed as not answering until you forget it.

Choose yours, or type its address below the list, as in a browser: an IP address, a domain or a `.local` name, with a port if it has another. The garland's screen shows it. Typing is what is left when the list is empty, as on a network that does not pass mDNS.

For one not paired, the list proves nothing, as anything can say it is a garland, so the next step is the same either way:

1. The utility fetches the garland's root certificate and shows its SHA-256. Compare it with the one on the portal's Info page, and press **It matches: ask for access** if they are the same. From then on it talks only to a garland under that root, and it installs nothing in your system.
2. A code appears, in the utility and on the garland's screen. If they match, press POWER and SPEED together on the garland, as for a browser.

![Connect](../images/utility/connect.png)

![Pairing](../images/utility/pair.png)

The access token stays in Windows Credential Manager, and the addresses and roots in `%AppData%\csiGarland`. Access lasts 100 days, as a browser's does, and then the utility asks you to pair again. If it says the garland no longer knows it, it was forgotten on the garland, or its time ran out. If it says the certificate is not valid at this computer's time, the garland is yours but a clock is wrong, on this computer or on the garland, which makes a new certificate once it knows the time: pairing again would not help, putting the clock right does.

## With No Garland

**Effects** in the menu, or **Work on effects** on the connect page, needs no garland. The other pages need one, and say so. It lists the built-in effects, which come with the utility, to preview and tune, or a file from this computer, and the result is saved to a file. The same list stands in when a garland paired before does not answer.

![With no garland](../images/utility/no-garland.png)

## Pages

**Effects:** on and off, brightness and speed, every effect by group, favourites for the EFFECT button. Beside each effect: preview it here, tune it, and play it on the garland.

![Effects](../images/utility/effects.png)

**Schedule:** the actions, the time zone, and the clock module if there is one. An action's time is typed, or picked by the clock beside it in a window of wheels, as on a phone: the mouse wheel turns one, a drag carries its numbers, and a press on a number brings it to the middle. Its days are a block of segments, any of them on at once, and a step's value stands beside its box once it is ticked: the brightness on arrows, the speed in segments, and the effect in a window of its own, by group. Each zone is shown with its offset from UTC, and its summer one if it has one. The time to set the module to by hand is typed, or picked in the same window of wheels, for the date too, which starts as what is typed, or the module's time, or the garland's, or this computer's; it is in the garland's time zone, and **Set by hand** waits for something in the field. If the list was changed elsewhere while you edited it, your change is not saved, the list is read again, and you can repeat it.

![Schedule](../images/utility/schedule.png)

![An action](../images/utility/schedule-action.png)

![Picking a time](../images/utility/schedule-time.png)

![Picking an effect](../images/utility/schedule-effect.png)

![Setting the clock module](../images/utility/schedule-clock.png)

**Device:** the LED count and the screen. **Network:** Wi-Fi, the mDNS name, and the certificate's domain. A password left empty for the network already saved keeps the one it has. A new domain means a new root certificate: browsers need it installed again, and the utility goes back to **Connect**, to compare it and pair again.

![Device](../images/utility/device.png)

![Network](../images/utility/network.png)

**A newer firmware:** once connected, the utility asks the garland what it runs and the catalog what is released. When the newest release is newer, a window offers it: **Install** downloads it and sends it, as the Firmware page below does, and **Not now** leaves it until the next connection. A Dev build, such as `1.0.0.101-dev`, counts as its number. With no internet, nothing is offered and nothing is said.

**Firmware:** choose an update file from this computer, or **From the catalog**, the releases the project's site, [garland.csilab.am](https://garland.csilab.am/downloads.html), lists, newest first, with the one running and those newer marked. A file from the catalog is downloaded from GitHub Releases and kept only if its length and SHA-256 are the catalog's; whether it is good is still the garland's to say, by its signature. Then the utility says what it holds, sends it with a bar of how far it has gone, and follows the garland until it has restarted and confirmed itself, or gone back. The bar counts what has left for the network, so all of it gone is not yet taken: the garland then checks and installs it before it answers. While this goes on the other pages wait, as the garland can answer nothing else, and the footer says what the utility waits for: the garland back on the network, counting the seconds, then the new firmware's minute on trial, counting down. The journal keeps each of these once. **Cancel** stops sending. The garland may already have taken the whole file by then, so the utility goes on watching, and says what came of it only once the garland shows it, about 15 s later if it stays as it was.

![Firmware](../images/utility/firmware.png)

![Sending an update](../images/utility/firmware-sending.png)

**Info:** the board, its memory and network, and what has access, with **Forget**. **Forget** on the line of this program ends its access here too.

![Info](../images/utility/info.png)

**Log:** what the garland is saying, for a sealed unit with no USB to read it from. The garland starts keeping its log at the first look here, in 6 KB of RAM, so what came before is not there, and it stops at its next restart until you look again. **Copy all** puts it on the clipboard for a bug report.

![Log](../images/utility/log.png)

Commands to the garland go one behind another, in the order you gave them, and those still waiting are dropped if you disconnect. Setting the clock module from the internet takes its turn like any other, and only starts the garland asking. The source the garland reports is that of the time it has, so if that came from the internet before, the utility can say that it asked, but not that this time worked.

## Previewing an Effect

The eye beside an effect opens it in a window of its own, drawn here and sent nowhere, on the garland or off it. The LEDs are a chain from the garland's board, numbered from 0 there, as an effect's `start` and `stop` count them, with each number under its LED, going back and forth across the window as a garland is hung: left to right, then right to left, with arrows along the cable. A wider window puts more of them on a line. An LED that is off is a dark circle, and one that is on sends out rays of its colour, as long and as wide as it is bright. Under the LEDs, in this window and in Tune, a speed and a brightness for the preview change nothing on the garland. The speed starts as the garland's and sets how fast the effect's time runs from where it is. The brightness starts at full, as a screen shows a garland turned down low as all but dark where its LEDs in a room are not, and is applied on the garland's own curve, which the screen's own gamma takes back, so that 15% looks about 15% of full.

![Preview](../images/utility/preview.png)

## Tuning an Effect

![Tune](../images/utility/tune.png)

The principle is that there is one engine. The firmware's effect code is built into the utility, so the preview moves as the garland would, and its parser is what says if a file is wrong. It is not frame for frame what the garland shows: the preview is always 32 LEDs, with no current limit applied, and what is random in an effect comes out differently. A screen cannot show what a figurine does to light either, so the preview is for the motion and the colours, and the garland has the last word.

1. **Start from something.** **New effect** on the Effects page starts one of your own: one layer of one colour, under the id `my-effect`. The sliders beside an effect read its file, from your garland if there is one, into a window over the page. Or open a file from this computer, with **Open a file** on the Effects page or **Open** in that window.
2. **Change it.** Every field has a control: the name, and for each layer its algorithm, from the parser's own list, `sx`, `ix` and `bri`, its colours, its palette from its colours, Rainbow or a gradient of stops, each a colour over its place from 0 to 255, where a new one goes after the last, with its colour, at 255, and a last one already there moves halfway back, its range and grouping, reverse and mirror, its zone of every LED, some figurines or every few, and how it blends over the layers below; layers are added, up to four, and removed. A field the algorithm does not read has no control, as the firmware's engine finds by drawing the algorithm with each field changed: rain, say, draws from its palette, so its colours count only while the palette is made of them, and sparkle reads two colours, its sparks and its ground, so it is offered no third. One left in the file is said to change nothing, with a button to take it out. The first layer has no blend, as it is drawn over dark. With more than one, a colour is dragged before or after another, where a mark shows it will go, and the colours are numbered again; a gradient's places stay where they are, in order. A colour pressed opens the picker in a window of its own, of saturation and value, hue and hex, with **Remove** and **Done**; it is lighter than Tune's, so the LEDs above show what the colour does. Each control writes its own field in the file under them and changes no other byte of it, and the file can be typed in as well: the controls follow it as soon as it is JSON in the shape of an effect. The controls and the file scroll as one, under the LEDs. The [format and every algorithm](effects.md) are in the Effects Guide. An error shows above the controls until it is fixed.

   ![Picking a colour](../images/utility/tune-colours.png)
3. **See it on the garland.** **Preview on garland** plays it with nothing saved, until another effect is chosen.
4. **Keep it.** **Save to file** puts it in a file on this computer, right or wrong, as a draft is worth keeping. **Save on garland** saves it there under the id you give, where it appears in the Uploaded group. An id of a built-in effect replaces that effect until you restore it. An uploaded file is at most 4096 bytes.

Closing the window, or opening another file, over what is not saved asks by a second press. An effect asked for from the garland opens only if it is still the last thing you asked for, and no window has opened since, so it cannot replace a draft.

To put an effect in the firmware for everyone, add its file to `firmware/app/effects`, as [the Effects Guide](effects.md#14-the-settings-utility) says.
