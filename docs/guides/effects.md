# csiGarland Effects Guide

What effects there are, how the catalog and an effect file are built, every algorithm, and how to tune or add an effect. Effect files live in [`firmware/app/effects/`](../../firmware/app/effects/). They ship as a pack beside the firmware, not inside it, so they can be replaced without a new one: see section 3. **Format:** version 1, implemented.

| Looking for | Section |
|---|---|
| How an effect gets onto a board | [3](#3-getting-effects-onto-a-board) |
| The catalog's structure, and the list of effects | [4](#4-catalog) |
| The file format | [6](#6-effect-file), [7](#7-layer) |
| Every algorithm | [8](#8-algorithms) |
| Tuning and adding | [12](#12-tuning-an-effect), [13](#13-adding-an-effect) |
| The settings utility | [14](#14-the-settings-utility) |

## 1. Approach

An effect is a JSON file listing **layers**. Each layer names an algorithm written for this project and gives it parameters. Its field names follow WLED segments, which are widely known, and csiGarland adds three things: algorithms referenced by name, layers stacked with a blend mode, and masks by figurine type.

**No code is copied from other projects.** Existing effects and presets are the base of ideas: what an effect should look like and which parameters make sense. Reading other code to understand what was meant is fine; every algorithm here is an own implementation.

## 2. What Other Projects Do

| Project | How an effect is described | License | Taken from it |
|---|---|---|---|
| [WLED](https://github.com/wled/WLED) | Compiled C++ algorithms; JSON presets hold only parameters (`fx`, `sx`, `ix`, `pal`) | EUPL-1.2 since `0.15.0-b7`; MIT up to `v0.15.0-b6` | Field names, the list of effects as ideas |
| [FastLED](https://github.com/FastLED/FastLED) | Library: math, noise, palettes | MIT | Nothing yet |
| [Hyperion.ng](https://github.com/hyperion-project/hyperion.ng) | JSON arguments + Python script + JSON Schema for the UI | MIT | The idea of a schema that validates files |
| WLED-MM [ARTI-FX](https://mm.kno.wled.ge/moonmodules/arti-fx/) | Interpreted program file | EUPL-1.2 | Nothing |
| [Pixelblaze](https://github.com/simap/pixelblaze) | JavaScript subset, `render(index)` | Firmware closed; the checked pattern repositories declare no license | The per-LED function model, for a later version |

No project offers a descriptive format that defines an animation's behaviour and comes with a freely licensed effect library.

## 3. Getting Effects onto a Board

### The pack

`scripts/effect_pack_tool.py` turns `catalog.json` and the effect files into one pack, which every build makes as `generated/effects.pack`. Every flash and every update file carries it.

| Way | What is written |
|---|---|
| `pwsh -File deploy\flash-device.ps1` | Firmware and pack. Settings and uploaded effects stay |
| `pwsh -File deploy\flash-device.ps1 -EffectsOnly` | The pack alone, with no build. The board's partition table is read first, and one without the slots is left untouched |
| `pwsh -File deploy\ota-update.ps1 -Address csigarland.local` | Firmware and pack |
| `pwsh -File deploy\ota-update.ps1 -Address csigarland.local -FirmwareOnly` | Firmware alone. The effects on the board stay |
| `pwsh -File deploy\ota-update.ps1 -Address csigarland.local -EffectsOnly` | The pack alone, with no build. Needs the signing key |
| `pwsh -File deploy\ota-update.ps1 -Address csigarland.local -File <path>`, or the portal's Firmware page | Whatever that update file holds. Both show it first |

Every build leaves three update files in its environment's folder, `.pio\build\esp32c6-release` for a Release build: `firmware.ota` with both parts, `firmware-only.ota` and `effects.ota`.

The board checks every effect at startup but keeps only pointers into the mapped pack, and parses an effect when it comes to play: one takes 452 bytes parsed, and only one plays. A pack (`src/effect_pack.h`) is a 12-byte header, then the records:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `CGPK` |
| 4 | 4 | Format, 2 (little-endian) |
| 8 | 4 | Size of the whole pack |
| 12 | | Records: a kind letter (`e` effect, `f` effect and default favourite, `p` planned), then the id, the group's name, the effect's name and the text, each ending in a NUL. The text is the minified effect file, empty for a planned effect |

The board keeps the pack in two 96 KB flash slots and writes a new one to the slot not in use. Each slot holds a commit record in its first 4 KB (`CGPS`, sequence number, size, SHA-256) and the pack after that, so a pack holds up to 92 KB, and up to 256 effects (section 4). The record is written last, so a power cut leaves the previous pack in charge. At startup the newest slot whose digest and format are good wins. With no good pack at all, the board plays the one effect held in its firmware and keeps its portal, so it can still be updated.

A board that has gone back to an older firmware keeps the newer pack, and refuses just the effects whose algorithm it does not have.

### Uploaded effects

The portal's Effects page and this API take a file in the format of section 6 without a new firmware. Up to 32 can be kept. The parser is the one built-in effects go through, so a file is accepted whole or not at all, and the current limit applies after rendering as for any other effect.

| Request | Does |
|---|---|
| `POST /api/effects/file?id=my-effect` with the file as body | Saves it. An id of a built-in effect replaces that effect |
| `DELETE /api/effects/file?id=my-effect` | Removes it, or gives a built-in effect its own definition back. Refused while the schedule uses an effect that would be gone |
| `POST /api/effects/preview` with the file as body | Plays it without saving, until another effect is chosen |
| `GET /api/effects/store` | How many are kept, the limits, and free memory |

An id is 1-32 lowercase letters, digits or hyphens, and a file is at most 4096 bytes. Positions in the catalog do not move as effects come and go, so favourites and the active effect stay right. Uploaded effects live apart from the pack, and a new pack leaves them alone; one that replaces a built-in effect keeps replacing it. The portal lists them all in a group of their own, Uploaded, the last one, so a replacement leaves its group until it is restored.

## 4. Catalog

[`catalog.json`](../../firmware/app/effects/catalog.json) lists every effect the project knows about, in the order the EFFECT button and the portal use.

```json
{ "id": "fireworks", "group": "burst", "favorite": true, "idea_from": "Fireworks", "params": ",Frequency" }
{ "id": "new-idea", "name": "New Idea", "todo": true, "group": "flash", "idea_from": "Something seen" }
```

The file's `groups` list names the groups in display order: Classic Garland, Warm Light, Holiday Colors, Twinkle and Sparkle, Running Lights, Color Flow, Bursts and Drops, Strobe and Flash. Every effect belongs to one, ready or not, and the entries of a group stay together.

| Field | Meaning |
|---|---|
| `id` | Lowercase letters, digits and hyphens. A ready effect's file is `<id>.json` |
| `group` | The group's `id`. Required |
| `favorite` | `true`: in the button's cycle on a board where the owner has not chosen yet |
| `todo` | `true`: planned, with no file and no algorithm yet. The portal lists it but cannot play it |
| `name` | Required with `todo`. A ready effect takes its name from its file |
| `idea_from` | What inspired it, for finding what was meant: usually a WLED effect |
| `params` | WLED's labels for its sliders, in the order `sx`, `ix`, `c1`, `c2`, `c3`, `o1`, `o2`, `o3`. `!` is the default label |

The catalog covers the effects of WLED `v0.15.0-b6` that fit a single chain, each written for this project: effects that need a 2D matrix or sound are left out. No entry is marked todo now; the flag stays for what comes next. A pack holds up to 256 ready effects, and uploads and the preview take 33 positions more, 289 in all. Favourites travel as one bit per position. The slots, the favourites and the state's effect index all have room for 512 effects, at the size effects are now, should `kMaxPackEffects` be raised: that would need no new partition table, and so no flash over USB. The build fails if a ready entry has no file or a todo entry has one, so the flag cannot drift.

**To implement a todo effect:** write its algorithm if none fits, add `<id>.json`, and remove `todo` and `name` from its entry.

### The effects, in catalog order

154 effects, which is the order of the portal's list. `*` marks a favourite on a board where the owner has not chosen yet. A name may repeat between effects, as WLED's do: the `id` is what tells them apart.

**Classic Garland** (16)

| Effect | `id` | Algorithms |
|---|---|---|
| Combination * | `garland-combination` | `combination` |
| In Waves * | `garland-waves` | `waves` |
| Sequential * | `garland-sequential` | `sequential` |
| Slo-Glo * | `garland-slo-glo` | `slo-glo` |
| Chasing Flash * | `garland-chasing` | `chasing` |
| Slow Fade * | `garland-slow-fade` | `slow-fade` |
| Twinkle Flash * | `garland-flash` | `flash` |
| Steady On * | `garland-steady` | `steady` |
| Build Up | `garland-build-up` | `build-up` |
| Cross Fade | `garland-cross-fade` | `cross-fade` |
| All Glow | `garland-all-glow` | `all-glow` |
| Random Channel | `garland-random` | `random-group` |
| Chase over Glow | `garland-chase-glow` | `steady`, `chasing` |
| Sparkle over Chase | `garland-chase-sparkle` | `chasing`, `sparkle` |
| Shimmer | `garland-shimmer` | `shimmer` |
| Bubble Lights | `bubble-lights` | `bubble` |

**Warm Light** (9)

| Effect | `id` | Algorithms |
|---|---|---|
| Fireplace | `fireplace` | `fire` |
| Living Candle | `living-candle` | `candle` |
| Candle | `candle` | `candle` |
| Fire Flicker | `fire-flicker` | `candle` |
| Warm Breath | `breathe` | `breathe` |
| Fade | `fade` | `colorloop` |
| Fade | `base-fade` | `fade` |
| Heartbeat | `heartbeat` | `heartbeat` |
| Sunrise | `sunrise` | `sunrise` |

**Holiday Colors** (18)

| Effect | `id` | Algorithms |
|---|---|---|
| Classic Christmas | `classic-christmas` | `twinkle` |
| Candy Cane | `candy-cane` | `stripes` |
| Christmas Wipe | `wipe` | `wipe` |
| Figurine Colors | `figurine-colors` | `solid`, `sparkle` |
| Tri Wipe | `tri-wipe` | `wipe` |
| Solid Pattern | `solid-pattern` | `bands` |
| Solid Pattern Tri | `solid-pattern-tri` | `bands` |
| Gradient | `gradient` | `gradient` |
| Traffic Light | `traffic-light` | `traffic-light` |
| Halloween Eyes | `halloween-eyes` | `eyes` |
| Spots | `spots` | `spots` |
| Spots Fade | `spots-fade` | `spots-fade` |
| Merry Christmas | `merry-christmas` | `bands` |
| Halloween Run | `halloween-run` | `bands` |
| Curtain | `curtain` | `wipe` |
| Ring the Bells | `ring-the-bells` | `solid`, `heartbeat` |
| Star Shine | `star-shine` | `solid`, `twinkle` |
| Figurine Parade | `figurine-parade` | `figure-parade` |

**Twinkle and Sparkle** (15)

| Effect | `id` | Algorithms |
|---|---|---|
| Winter Snowfall | `winter-snowfall` | `sparkle` |
| Ice Crystal | `ice-crystal` | `solid`, `sparkle` |
| Twinkle | `twinkle` | `twinkle` |
| Fairy Lights | `fairy` | `twinkle` |
| Fairytwinkle | `fairytwinkle` | `shimmer` |
| Twinklecat | `twinklecat` | `sparkle` |
| Twinkleup | `twinkleup` | `twinkle-up` |
| Glitter | `glitter` | `rainbow`, `sparkle` |
| Sparkle+ | `sparkle` | `sparkle` |
| Sparkle Dark | `sparkle-dark` | `sparkle` |
| Dissolve | `dissolve` | `dissolve` |
| Dissolve Rnd | `dissolve-rnd` | `dissolve` |
| Slow Fade Twinkle | `slow-fade-twinkle` | `shimmer` |
| Hyper Sparkle | `hyper-sparkle` | `sparkle` |
| Sparkle Pulse | `sparkle-pulse` | `breathe`, `sparkle` |

**Running Lights** (42)

| Effect | `id` | Algorithms |
|---|---|---|
| Chase | `base-chase` | `chase` |
| Marquee Lights | `theater` | `theater` |
| Theater Rainbow | `theater-rainbow` | `theater` |
| Golden Scanner | `scanner` | `scanner` |
| Scanner Dual | `scanner-dual` | `scanner` |
| Ice Meteor | `meteor` | `meteor` |
| Meteor Smooth | `meteor-smooth` | `meteor` |
| Multi Comet | `multi-comet` | `saw` |
| Two Dots | `two-dots` | `scanner` |
| Running | `running` | `running` |
| Running Dual | `running-dual` | `running` |
| Saw | `saw` | `saw` |
| Sweep | `sweep` | `sweep` |
| Sweep Random | `sweep-random` | `sweep-random` |
| Wipe Random | `wipe-random` | `wipe-random` |
| Scan | `scan` | `scan` |
| Scan Dual | `scan-dual` | `scan` |
| Android | `android` | `android` |
| Chase Random | `chase-random` | `band-random` |
| Chase Rainbow | `chase-rainbow` | `band` |
| Chase 2 | `chase-2` | `band` |
| Chase 3 | `chase-3` | `bands` |
| Chase Flash | `chase-flash` | `chase-flash` |
| Chase Flash Rnd | `chase-flash-rnd` | `chase-flash-random` |
| Rainbow Runner | `rainbow-runner` | `running` |
| Stream | `stream` | `stream` |
| Stream 2 | `stream-2` | `stream-smooth` |
| Lighthouse | `lighthouse` | `beam` |
| Loading | `loading` | `loading` |
| Sinelon | `sinelon` | `sinelon` |
| Sinelon Dual | `sinelon-dual` | `sinelon` |
| Sinelon Rainbow | `sinelon-rainbow` | `sinelon` |
| Oscillate | `oscillate` | `oscillate` |
| ICU | `icu` | `icu` |
| Tetrix | `tetrix` | `tetrix` |
| Percent | `percent` | `percent` |
| Chunchun | `chunchun` | `flock` |
| Washing Machine | `washing-machine` | `washing` |
| Perlin Move | `perlin-move` | `perlin-move` |
| Circus Combustus | `circus-combustus` | `bands` |
| Chase Blackout | `chase-blackout` | `theater` |
| Railway | `railway` | `slow-fade` |

**Color Flow** (32)

| Effect | `id` | Algorithms |
|---|---|---|
| Aurora | `aurora` | `aurora` |
| Rainbow Radiance | `rainbow-radiance` | `rainbow` |
| Color Loop | `colorloop` | `colorloop` |
| Color Drift | `dynamic-smooth` | `drift` |
| Dynamic | `dynamic` | `dynamic` |
| Pacifica | `pacifica` | `aurora` |
| Color Waves | `colorwaves` | `aurora` |
| Lake | `lake` | `aurora` |
| Tri Fade | `tri-fade` | `colorloop` |
| Random Colors | `random-colors` | `random-colors` |
| Colorful | `colorful` | `bands` |
| Palette | `palette` | `rainbow` |
| Pride 2015 | `pride-2015` | `pride` |
| Bpm | `bpm` | `bpm` |
| Fill Noise | `fill-noise` | `noise` |
| Noise 1 | `noise-1` | `noise` |
| Noise 2 | `noise-2` | `noise-glow` |
| Noise 3 | `noise-3` | `noise-glow` |
| Noise 4 | `noise-4` | `noise` |
| Noise Pal | `noise-pal` | `noise` |
| Plasma | `plasma` | `plasma` |
| Phased | `phased` | `phased` |
| Phased Noise | `phased-noise` | `phased-noise` |
| Sine | `sine` | `sine` |
| Flow | `flow` | `flow` |
| Flow Stripe | `flow-stripe` | `flow-stripe` |
| Blends | `blends` | `rainbow` |
| Wavesins | `wavesins` | `aurora` |
| Dancing Shadows | `dancing-shadows` | `shadows` |
| TV Simulator | `tv-simulator` | `tv` |
| Color Clouds | `color-clouds` | `noise` |
| Water Wave | `water-wave` | `water-wave` |

**Bursts and Drops** (15)

| Effect | `id` | Algorithms |
|---|---|---|
| Fireworks * | `fireworks` | `fireworks` |
| Fireworks Starburst | `fireworks-starburst` | `starburst` |
| Fireworks 1D | `fireworks-1d` | `rocket` |
| Ripple | `ripple` | `ripple` |
| Ripple Rainbow | `ripple-rainbow` | `rainbow`, `ripple` |
| Rain | `rain` | `rain` |
| Drip | `drip` | `drip` |
| Bouncing Balls | `bouncing-balls` | `bouncing-balls` |
| Rolling Balls | `rolling-balls` | `rolling` |
| Popcorn | `popcorn` | `popcorn` |
| Juggle | `juggle` | `juggle` |
| Icicle Drip | `icicle-drip` | `icicle` |
| Hourglass | `hourglass` | `hourglass` |
| Sparkler | `sparkler` | `sparkler` |
| Comet Collision | `comet-collision` | `comet-collision` |

**Strobe and Flash** (7)

| Effect | `id` | Algorithms |
|---|---|---|
| Blink | `blink` | `blink` |
| Blink Rainbow | `blink-rainbow` | `blink` |
| Strobe | `strobe` | `blink` |
| Strobe Rainbow | `strobe-rainbow` | `blink` |
| Strobe Mega | `strobe-mega` | `strobe-burst` |
| Police | `police` | `alternate` |
| Lightning | `lightning` | `lightning` |

## 5. Favourites and Activation

* The **EFFECT button** cycles through favourites only.
* The **portal** ticks favourites and can play any ready effect, favourite or not. Playing turns the garland on. After a non-favourite, the button continues with the next favourite in catalog order.
* Favourites are saved by id, so a new effect in the catalog does not disturb the owner's choice. At least one must stay ticked. With nothing saved yet, the ones marked `favorite` in the catalog count: the classic garland modes and Fireworks.

## 6. Effect File

```json
{
  "format": "csigarland.effect/1",
  "name": "Winter Snowfall",
  "layers": [
    { "fx": "sparkle", "sx": 64, "ix": 48, "col": ["FFFFFF", "0A2A4A"] }
  ]
}
```

| Field | Required | Meaning |
|---|---|---|
| `format` | yes | Always `csigarland.effect/1` |
| `name` | yes | For the display and portal: up to 32 printable ASCII characters, no `"` or `\` |
| `layers` | yes | 1 to 4 layers, drawn in order |

## 7. Layer

| Field | Range | Meaning |
|---|---|---|
| `fx` | see section 8 | Algorithm. Required |
| `sx` | 0-255 | Speed slider, default 128 |
| `ix` | 0-255 | Intensity slider, default 128 |
| `col` | 1-3 colours | `"RRGGBB"` or `[r, g, b]` |
| `pal` | see below | Palette |
| `start`, `stop` | 0-65535 | Range of LEDs; `stop` is exclusive, 0 means the end of the chain |
| `grp` | 1-255 | LEDs per group |
| `spc` | 0-255 | LEDs skipped between groups |
| `of` | integer | Rotates the layer |
| `rev`, `mi` | bool | Reverse, mirror |
| `bri` | 0-255 | Layer brightness, default 255 |
| `zone` | list of figurines, or `{"every": n, "offset": k}` | Draw only on these LEDs. The algorithm still runs over the whole range |
| `blend` | `"replace"` (default), `"add"`, `"max"` | How the layer combines with what is below |

**Palettes.** `pal` is `"Rainbow"` or an inline gradient of 2 to 16 position and colour pairs, for example `[0, "000000", 128, "FF4000", 255, "FFD080"]`. Omitted, the palette is built from `col`.

**Figurines.** Zones resolve through the layout, the order of the figurines along the cable from the control unit, repeated: `tree`, `star`, `snowman`, `pinecone`, `lantern`, `bell`, `icicle`, `gingerbread`. The eight repeat four times along the cable, so the order is chosen for the loop: the two clear figurines, the icicle and the snowman, stand four places apart, as do the two warm ones, the pinecone and the gingerbread man, and the two cool ones, the star and the bell, and no two neighbours share a colour. A harness assembled in another order lights the wrong figurines in the effects that name them.

## 8. Algorithms

`sx` at 128 is each algorithm's natural pace unless the table says otherwise.

| `fx` | What it draws | `sx` | `ix` | Colours |
|---|---|---|---|---|
| `solid` | One colour | - | - | First palette colour |
| `fade` | Whole chain rises from zero and falls back | Pace | - | First palette colour |
| `chase` | A light with a short tail, first LED to last, 100 ms per LED | Pace | - | First palette colour |
| `fire` | Heat appears at the start and rises along the chain | How fast heat is lost | How often new heat appears | Palette by heat |
| `candle` | Each LED flickers on its own | Pace | The dimmest level | First palette colour |
| `twinkle` | LEDs light and fade at their own rhythm | Pace | Share of LEDs lit | Palette |
| `sparkle` | Short sparks on a background | How fast sparks fade | How often they appear | `col[0]` spark, `col[1]` background, or a palette for the sparks |
| `stripes` | Two-colour bands travelling along the chain | Pace | Band width | `col[0]`, `col[1]` |
| `aurora` | Slow waves of colour and brightness | Pace | Wave size | Palette |
| `rainbow` | A palette travelling along the chain | Pace | Palette span per LED | Palette, rainbow by default |
| `breathe` | Whole chain rises and sinks along a sine, without going dark | Pace | The level it sinks to | First palette colour |
| `wipe` | Each colour in turn spreads from the first LED to the last, 80 ms per LED, then holds | Pace | Hold, 16 ms per unit | Palette stops in order; a lone colour takes turns with darkness |
| `theater` | Every n-th LED lit, stepping along the chain | Pace | n: 2 at 0-63, up to 5 at 192-255 | `col[0]` lit, `col[1]` background, or a palette along the chain |
| `meteor` | A head with a tail, 60 ms per LED, leaving past the last LED | Pace | Tail length, 2 + `ix`/16 LEDs | Palette along the tail, head at 0 |
| `scanner` | A light running to the last LED and back, 70 ms per LED, with a tail | Pace | Tail length, 2 + `ix`/16 steps | Palette along the tail, head at 0 |
| `colorloop` | The chain travels through the palette, 20 s per round | Pace | Shift between LEDs, 0 for none | Palette |
| `drift` | Each LED wanders slowly to a new place on the palette, by the short way | Pace | How often an LED picks a new place | Palette; a closed one, like `Rainbow`, avoids a jump at its ends |
| `fireworks` | A white flash at a random LED opens up to 3 LEDs each way and fades | Pace | Bursts at once, 1 + `ix`/48 | Palette picks each burst's colour |
| `heartbeat` | A strong beat and a weaker one, then a rest, 1.4 s per cycle | Pace | The level between beats | First palette colour |
| `running` | Waves of light travelling along the chain | Pace | How many fit | First colour over `col[1]`, or a palette along the chain |
| `random-colors` | The chain fades to a random colour, then holds it, 4 s per colour | Pace | - | Palette |
| `dissolve` | LEDs turn on at random until all are lit, then off the same way, 6 s per round | Pace | - | First palette colour, or a palette for each LED |
| `bouncing-balls` | Balls rising from the first LED and falling back | Pace | Balls, 1 + `ix`/64 | Palette stops in order |
| `juggle` | Dots swinging along the chain at different paces | Pace | Dots, 1 + `ix`/43 | Palette |
| `ripple` | Rings spreading from random LEDs and fading | Pace | Rings at once, 1 + `ix`/64 | Palette picks each ring's colour |
| `sunrise` | Climbs through the palette once and stays at its end | Pace | Duration, `ix` + 1 seconds | Palette |
| `drip` | A drop swells on the first LED, then falls faster and faster | Pace | Drops, 1 + `ix`/64 | Palette along the tail, head at 0 |
| `rain` | Drops starting at random LEDs | Pace | Drops, 2 + `ix`/32 | Palette along the tail, head at 0 |
| `bands` | Palette stops as equal runs repeating along the chain | Pace, 0 keeps still | Run, 1 + `ix`/32 LEDs | Stops in order, positions ignored |
| `saw` | Heads with a fading trail, one after another | Pace | Distance, 3 + `ix`/16 LEDs | `col[0]`, trail to `col[1]`, or a palette for each head |
| `sweep` | A colour spreads over the last one, on every other pass from the far end | Pace | Hold, 12 ms per unit | Palette stops in order |
| `sweep-random`, `wipe-random` | The same in random colours; `wipe-random` always from the first LED | Pace | Hold | Palette |
| `scan` | Dots with no trail running to the last LED and back | Pace | Dots, 1 + `ix`/64 | Stops in order, `col[1]` background |
| `android` | A segment that stretches ahead, then pulls its tail in | Pace | Longest length | `col[0]`, `col[1]` background |
| `band` | A band running around the chain | Pace | Width, 1 + `ix`/16 LEDs | `col[0]`; `col[1]` or a palette along the chain as background |
| `band-random` | The same, a new colour for each lap | Pace | Width | Palette |
| `chase-flash` | A pair of LEDs flashes twice, then moves on | Pace | - | `col[0]` background, `col[1]` flash |
| `chase-flash-random` | The same at random places, on dark | Pace | - | Palette |
| `stream`, `stream-smooth` | Blocks of random colours travelling, sharp or shaded | Pace | Block, 1 + `ix`/32 LEDs | Palette |
| `beam` | A soft patch going around the chain | Pace | Width | `col[0]` on `col[1]` |
| `gradient` | A soft patch to the last LED and back | Pace | Width | `col[0]` on `col[1]` |
| `loading` | A bar fills the chain, then fades | Pace | Fade time | Palette along the chain |
| `sinelon` | A dot swinging by a sine, leaving a trail | Pace | Trail length | First colour, or a palette over time |
| `perlin-move` | Dots wandering by noise, with a short trail | Pace | Dots, 1 + `ix`/64 | Palette |
| `oscillate` | Three short segments bouncing, adding up where they cross | Pace | - | Stops in order |
| `icu` | A pair of eyes that looks around and blinks | Pace | - | First colour, `col[1]` background |
| `tetrix` | Blocks fall and stack, then the full chain fades | Pace | Block, 1 + `ix`/64 LEDs | Palette for each block |
| `percent` | Still: the share of the chain set by `ix` | - | Share | `col[0]`, rest `col[1]` |
| `flock` | Dots following each other along a sine | Pace | Dots, 2 + `ix`/32 | Palette |
| `washing` | The palette's pattern turns one way and back | Pace | Palette span per LED | Palette |
| `traffic-light` | Lamps in threes through the phases of a traffic light, 6 s per cycle | Pace | From 128, no stop-and-ready phase | `col[0]` stop, `col[1]` ready, `col[2]` go |
| `eyes` | Pairs of eyes open at random places, blink once and close | Pace | Pairs, 1 + `ix`/64 | Palette |
| `spots`, `spots-fade` | Still patches; with `spots-fade` they swell and shrink | Pace of the swelling | Distance, 3 + `ix`/16 LEDs | `col[0]` on `col[1]` |
| `shimmer` | Every LED stays lit and dips at its own rhythm | Pace | The level it dips to | First colour, or a palette for each LED |
| `twinkle-up` | LEDs rise slowly and go out quickly | Pace | Share of LEDs lit | Palette |
| `icicle` | Rows of 4 to 11 LEDs, down each of which a drop runs in its own time, gathering speed | Pace | Length of a row | `col[0]` drop, `col[1]` background |
| `bubble` | Each LED in its own colour with a slow swell and a quicker, uneven stir, never dark | Pace | | Palette stops, repeating along the chain |
| `dynamic` | LEDs jump to new places on the palette | Pace | Share changing at each step | Palette |
| `pride` | Colour and brightness in waves of changing length | Pace | - | Palette |
| `bpm` | Pulses along the chain to a steady beat | Pace | Delay between LEDs | Palette |
| `noise` | Noise picks a place on the palette | Pace | Grain | Palette |
| `noise-glow` | The palette along the chain, noise sets the brightness | Pace | Grain | Palette |
| `plasma` | Waves running against each other | Pace | Wave size | Palette |
| `phased`, `phased-noise` | Brightness where two waves meet; with noise the meeting points wander | Pace | Wave size | Palette along the chain |
| `sine` | A wave of brightness one way, the palette the other | Pace | Wave size | Palette |
| `flow` | The palette running out from zones and back in | Pace | Zones, 1 + `ix`/32 | Palette |
| `flow-stripe` | Stripes getting narrower and wider as they move | Pace | - | Palette |
| `shadows` | Soft shadows drifting over the palette | Pace | Shadows, 1 + `ix`/48 | Palette |
| `tv` | Light of a television in a dark room | Pace | Depth of the flicker | Palette picks each scene |
| `starburst` | Fragments flying both ways from a random LED | Pace | Bursts, 1 + `ix`/64 | Palette picks each burst |
| `rocket` | A rocket climbs and bursts into falling sparks | Pace | Rockets, 1 + `ix`/128 | Palette picks each burst |
| `rolling` | Balls rolling evenly between the ends | Pace | Balls, 1 + `ix`/64 | Stops in order |
| `popcorn` | Kernels thrown to a random height at random moments | Pace | Kernels, 2 + `ix`/32 | Palette |
| `blink` | The whole chain on and off, 1 s per cycle. High `sx` and low `ix` make a strobe | Pace | Share of the cycle on | First colour, or a palette for each flash; `col[1]` between |
| `strobe-burst` | A rattle of quick flashes, then a pause | Pace | Flashes, 3 + `ix`/32 | First colour, `col[1]` between |
| `alternate` | Sections flash twice each, in turn | Pace | Sections, 2 + 2 * (`ix`/64) | `col[0]`, `col[1]` |
| `lightning` | One to four flashes over a random stretch | Pace | Storms, 1 + `ix`/64 | First colour |
| `hourglass` | Grains fall from the upper pile and stack at the first LED, then the glass turns over | Pace | Share of the chain that is sand | Palette along the grains |
| `sparkler` | A head walks the chain, 350 ms per LED, throwing sparks | Pace | Sparks, 4 + `ix`/16 | Palette picks each spark |
| `water-wave` | Rings from drops spread and come back from the ends. Never dark | Pace | Drops, 1 + `ix`/64 | The height of the water picks a place on the palette, still water at 80 |
| `comet-collision` | Comets from both ends meet near the middle and burst | Pace | Fragments each way, 2 + `ix`/64 | `col[0]` and `col[1]`, for the two comets |
| `figure-parade` | One type of figurine at a time rises and hands over, 0.9 s each. Follows the layout in section 7 | Pace | The level the others keep | Palette stops, one for each figurine |

The modes of the classic 8-function garland controller, written for this project from how those garlands behave. Their bulbs are driven in groups, and each bulb keeps one colour: the palette's stops repeat along the chain. `ix` sets the number of groups, 2 + `ix`/64.

| `fx` | What it draws | `sx` | Groups used |
|---|---|---|---|
| `steady` | Steady On: always lit | - | - |
| `waves` | In Waves: each group rises and falls in its turn, 4 s per cycle | Pace | All |
| `sequential` | Sequential: groups turn on one after another, then off in the same order, 0.5 s per step | Pace | All |
| `slo-glo` | Slo-Glo: the whole chain rises from dark and falls back, 6 s per cycle | Pace | - |
| `chasing` | Chasing Flash: one group lit at a time, 0.2 s per step | Pace | All |
| `build-up` | Build Up: groups turn on one after another and stay on, then all go off at once, 0.4 s per step | Pace | All |
| `cross-fade` | Cross Fade: each group fades into the next, 0.9 s per step | Pace | All |
| `all-glow` | All Glow: all rise and fall together, with a moment of dark between, 5.7 s a cycle | Pace | All |
| `random-group` | Random Channel: a different group at random lit each step, 0.35 s per step | Pace | All |
| `slow-fade` | Slow Fade: odd and even groups cross-fade, never dark, 5 s per cycle | Pace | Odd and even |
| `flash` | Twinkle Flash: odd and even groups blink in turn, 0.5 s per step | Pace | Odd and even |
| `combination` | Combination: the six moving modes in turn, 10 s each | Pace | All |

## 9. What Stays Outside the File

* **Global brightness (POWER) and speed (SPEED):** runtime state. SPEED scales the effect's clock, so every algorithm follows it and `sx` keeps its meaning. An algorithm that moves in steps of its own, such as `fire`, `candle`, `drift` and `dynamic`, takes every step that has come since its last frame, up to 8 in one frame (`kMaxCatchUpSteps`): a fast SPEED or a late frame moves it on as far as the time that passed, and a long stall costs no frame more than that.
* **The current limiter:** applied to the final frame, at the limit the Device page's power settings give, 1400 mA by default. No file can bypass it.
* **LED count:** from the portal or the probe.

## 10. Bad Input

A file that fails validation is rejected whole and left out of the catalog, with the reason in the log. Unknown fields are ignored, so a newer file can load on older firmware. A new meaning for an existing field takes a new `format` version.

## 11. Not Implemented Yet

* **Playlist.** [`carousel.json`](../../firmware/app/effects/carousel.json) shows the planned format, following WLED's playlist fields: `items`, then `dur` and `transition` in tenths of a second, and `repeat` with 0 looping forever. The firmware does not read it.
* **Cross-fade between effects.** The change is a cut.
* **WLED preset import.** Layers can take `sx`, `ix`, `col`, `grp`, `spc`, `of`, `rev`, `mi`, `start` and `stop` unchanged. A converter needs a table from WLED's effect number to its name, and refuses effects still marked `todo`. WLED's `sx` and `ix` mean something different for each effect, so converted values are a starting point, not an exact look.
* **New algorithms without reflashing.** If needed, an expression layer on the Pixelblaze `render(index)` model.

## 12. Tuning an Effect

An effect is a small JSON file, so tuning is editing it and looking. [The utility](utility.md) does that with a preview beside the file, as section 14 says, and these steps are the same without it.

**Try it on the garland, with nothing saved:**

1. Take the effect's file from [`firmware/app/effects/`](../../firmware/app/effects/) and change a value.
2. On the portal's Effects page, under **Add an effect from a file**, choose it and press **Preview**. A bad file is refused with the reason. A preview plays until another effect is chosen.
3. Repeat. Then **Save** keeps it on this garland, and **Restore** gives the built-in one back. To change it for good, see below.

**What to change first:**

| To change | Change | Notes |
|---|---|---|
| The pace | `sx` | 128 is the algorithm's natural pace. Do not fit this to the room: the SPEED button scales every effect's clock on top of it, 0.25x to 3x |
| How much is going on | `ix` | Means something different per algorithm: density, width, tail or count. Section 8 says which |
| The colours | `col`, or `pal` for a gradient | Section 7. Which of them count depends on the algorithm |
| Which LEDs | `start`, `stop`, `grp`, `spc`, `zone` | `zone` picks figurines, and the algorithm still runs over the whole range |
| The depth | More `layers` | Up to 4. `blend` `"add"` or `"max"` lays sparkles over a base, as `glitter.json` does |
| How bright this effect is | `bri` | Each layer's. Prefer it to dark colours, and leave overall brightness to POWER |

**Things that do not need tuning:** overall brightness and speed are runtime state, and the current limit applies to the final frame. So a bright effect is safe; it just dims as a whole when it would go over.

**Judge on the real figurines,** with the caps on, at the brightness you will use: a bare strip looks different.

## 13. Adding an Effect

**On one garland only,** upload a file as in section 12. Its name is its id, and no build is needed.

**For every garland you build for:**

1. Write `firmware/app/effects/<id>.json`, with an id of lowercase letters, digits and hyphens. [`effect.schema.json`](../../firmware/app/effects/effect.schema.json) describes the file for an editor.
2. Add an entry to [`catalog.json`](../../firmware/app/effects/catalog.json), within its group. Positions in the catalog are the portal's order, and favourites are kept by id, so inserting one disturbs nobody.
3. Add it to the list in section 4. `deploy/tests/source-rules.tests.ps1` fails if an effect or an algorithm is missing from this guide.
4. Check and send it. `pio test -e native` parses every effect. `-EffectsOnly` needs no build and no new firmware:

```bash
pwsh -File deploy\ota-update.ps1 -Address csigarland.local -EffectsOnly
```

**Limits:** a pack holds up to 92 KB and 256 effects, and 154 of those are used now, so 102 places wait for new ones. An uploaded file is at most 4096 bytes. The build fails if a pack does not fit.

**A new algorithm** needs a new firmware: add it in `src/effect_algorithms.cpp`, name it in `src/effect_parser.cpp`, test it in `test/test_logic`, and document it in section 8. A board on an older firmware refuses just the effects whose algorithm it does not have, and keeps the others.

## 14. The Settings Utility

[The csiGarland utility](utility.md) tunes effects with a preview drawn by this firmware's own engine, for now on Windows, and needs no board to show one. Its use is in its own guide. The principles:

* **One engine.** The utility is built with the firmware's effect code, so its frames are the garland's, and the firmware's parser is what judges a file. A screen does not show what a figurine does to light, so the garland has the last word: preview there too before keeping anything.
* **What you tune reaches a garland as an upload.** Saved from the utility, a file lands in the Uploaded group, and one under a built-in effect's id replaces that effect until it is restored. It stays through a firmware update, as uploads live apart from the pack.
* **What everyone should have goes in the pack.** Add the file to `firmware/app/effects` as sections 12 and 13 say, and a firmware or effects update carries it.
