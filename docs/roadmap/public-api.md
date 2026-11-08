# Public API

What the csiGarland utility builds on, and anyone else's tools after it. Implemented in the firmware and in `internal/api`, and seen working on the owner's garland through the utility: host tests cover its logic, a stub server covers the client, and a chain built with the firmware's own name constraints shows that Go takes it. What is new after the last release of the firmware is not yet seen on a board.

## Why a Branch of Its Own

`/api/` belongs to the portal's own pages, which ship inside the same image, so it can change with them in one build. An application is built and updated apart from the firmware, so it needs an interface that holds still. Hence a separate branch, `/api/v1/`:

* What is under `/api/v1/` changes only by adding: new routes and new fields, never a changed meaning. A client ignores fields it does not know.
* A change that would break a client becomes `/api/v2/`, with `v1` kept alongside it for as long as the flash allows.
* `/api/` stays private and free to change with the portal's pages, which ship in the same image.

`v1` as it stands in 1.0.0, the first release, is the baseline.

## Address and Port

As in a browser: the owner enters an IP address, a domain or an mDNS name such as `csigarland.local`, with an optional port, and the application remembers it.

**On the portal's own port, 443.** A dedicated port was considered. A second TLS server takes a task with its 11 KB stack and its own sessions out of a heap that was down to 106 KB at its lowest, as measured during development, and gains nothing a separate path does not. The client still takes a port, for an owner forwarding one from a router.

Plain HTTP answers `v1` only on the setup network, and on a garland whose HTTPS failed to start, as it does the portal, so that an update can still reach it; anyone else is sent to HTTPS, by a temporary redirect no cache keeps. The utility uses HTTPS alone.

**Connections, new after 1.0.0.** A request's headers have 10 s, from a connection's opening or the end of the request before it on it, and a TLS handshake 15 s: past either, the garland closes the connection. So one kept open for the next request is closed after 10 s idle, and a client lets its own go sooner, as the utility does after 8 s: a request that changes something, sent on a connection the garland is closing, fails and is not sent again by itself. A body has 10 s to arrive, an update a minute and as long again as it takes at 8 KB/s, and a request answered with its body not read whole has its connection closed.

## Finding a Garland

A garland announces itself by mDNS as `_csigarland._tcp` on its HTTPS port, under its name. A client asks for that, and then asks each that answers `GET /api/v1/about`, keeping those whose `product` is `csiGarland` and whose `api` it can speak. What is asked after that is bounded, as anyone on the network can answer, and it all goes over one connection to each. Neither says more than what the device claims: only the root below does. A client may compare the root a device offers with those it keeps, to know one it has met at a new address, as long as it still connects under the kept one. An address typed by hand stays, for a network that does not pass mDNS.

## Trust

The garland's certificate comes from its own private root, which no system trusts. So the application keeps its own trust, and installs nothing in the system's store:

1. On first contact it fetches the root from `/csigarland-root.crt`, shows its SHA-256 and asks the owner to compare it with the one on the Info page, or in the log of the first start.
2. It stores that root for this garland, and from then on accepts only a chain to it.
3. It does not check the name in the certificate. The address is whatever the owner typed, which may not be among the certificate's names, and the pinned root is what identifies the garland.
4. A connection under a different root fails, and only the first certificate of the file is ever kept, as the fingerprint says nothing of the rest. Connecting again shows the new root, with a plain warning if it differs from the one kept for that address.

A garland makes a new root certificate when the owner sets another domain, on the same keys, or as the old one nears its end. A name the kept root does not allow is then refused, so an application connects, compares and pairs again. That is meant: only the owner can say the new one is theirs.

## Pairing

As for a browser, with the owner's press on the garland as the only approval.

1. `POST /api/v1/pair/request` with `{"label": "csiGarland on DESKTOP"}` answers `{"request", "code", "seconds"}`. Open.
2. The application shows the code, and the garland's screen shows the same.
3. The owner presses POWER and SPEED together on seeing them match.
4. `POST /api/v1/pair/wait` with `{"request"}` answers `{"state": "waiting"}` until it is settled. Once approved, it answers `{"state": "approved", "token": "..."}`, and the token appears nowhere else. Open.

The device list, its limit of 8 and the 100 days are those of browsers: an application counts as one, and the Info page can forget it. A token that has run out answers 401, and the application asks the owner to pair again.

The token goes in `Authorization: Bearer <token>` on each request. The utility keeps it in the system's own store: Credential Manager on Windows, the Secret Service on Linux, and the login keychain on macOS. Anywhere else, or on a Linux with no Secret Service, it refuses to keep a token, and pairing lasts until it closes.

## Requests and Answers

* Bodies are JSON, but for an update file or an effect file, as under `/api/`.
* A failure is the right HTTP status, with the body `{"error": {"code": "not_found", "message": "unknown effect"}}`. The `code` is stable, for the program, and the `message` is for a person. The codes are `bad_request`, `not_paired`, `not_found`, `method_not_allowed`, `timeout`, `conflict`, `too_large`, `try_later`, `internal` and `failed`.
* 401 means pair again, and 409 means the list changed since it was read. 503 with `Retry-After` on any route that wants pairing means the garland cannot read its paired clients for now: the token may still be good, so keep it and try again. 429 with `Retry-After` means wait, as after a refused request. 413 means a body over its route's limit, and 405 a route that does not take that method.
* An update the garland refuses says whose problem it is: 400 for the file, 408 for a connection lost on the way, 409 for a garland on trial or restarting, 500 for the garland itself.
* A garland that takes an update answers and then restarts, so a client whose upload ended with no answer cannot tell whether it was taken. It compares `boot`, `image` and `pack_digest` from `GET /api/v1/firmware` with those from before. The same `boot` well past the restart's 1.5 s means it was not.
* The body of most requests is bounded at 192 bytes, so a client does not make it longer than JSON has to.
* `POST /api/v1/wifi` with an empty password, for the network already saved, keeps its password, as a client cannot read it back to send it. A network that has become open is forgotten first.
* The server takes one request at a time per socket, and few sockets: a client keeps one connection, and polls no faster than once a second.
* What a route answers is what its portal route answers, in [the portal's API](../hardware/control-unit.md#53-rest-api-endpoints), unless this page says otherwise. `internal/api/routes.go` has each shape as a Go type.

## Routes

| Method | Route | Does | Shares its handler with |
|---|---|---|---|
| `GET` | `/api/v1/about` | Product, API version, firmware version, whether on trial and its seconds left, effects, pack's digest, image hash, boot id. Open, holding nothing private | New |
| `POST` | `/api/v1/pair/request`, `/api/v1/pair/wait` | Above. Open | `/api/auth/*` |
| `GET`, `POST` | `/api/v1/state` | Power, brightness, speed, effect, whether the playlist plays (`playlist`) and where it stands (`playlist_at` from 1, 0 when what plays is not in it, and `playlist_size`), all three new after 1.0.0, LED count, and `steps_done_until`, new after 1.0.0, the unix second up to which the scheduler has done its steps, 0 before its first pass: a step of the timeline due before it is done, whatever the power came to since | `/api/state` |
| `GET` | `/api/v1/events` | Push updates, new after 1.0.0: Server-Sent Events, `event: state` with what `/api/v1/state` answers, at once and on every change, and `event: alive` with `data: {}` within every 15 s of a stream's own silence, whatever the other stream hears: a reader passes over an event it does not know, and one that hears nothing for 45 s may take the stream as lost. Two at a time; 503 for a third, during an update, and when the garland is busy for a moment. A stream ends once its client is forgotten or out of its days. A client keeps its own connection for it, and asks for the state now and then whenever it has none | `/api/events` |
| `POST` | `/api/v1/power` | `{"on": true}` turns on or off, and answers with the state | New |
| `GET` | `/api/v1/effects` | The effects, with favourite and active flags. 503 while the favourites saved could not be read, as the flags would be the defaults. `complete`, new after 1.1.1, is false while an uploaded effect, or a favourite mark of one, could not be read yet: the list is then partial, and a backup must not be made of it | `/api/effects` |
| `POST` | `/api/v1/effects/play` | Plays one, `{"id": "..."}` | `/api/effect` |
| `POST` | `/api/v1/effects/favorite` | Ticks or unticks one. 503, nothing changed, while the favourites saved could not be read | `/api/favorites` |
| `GET` | `/api/v1/effects/file?id=` | The file of any effect, as it stands on this garland | New |
| `POST`, `DELETE` | `/api/v1/effects/file?id=` | Saves or deletes an uploaded effect. A `POST` under an id the garland does not know is 503 while a file saved before could not be read, as it may hold that id | `/api/effects/file` |
| `POST` | `/api/v1/effects/preview` | Plays a file without saving it | `/api/effects/preview` |
| `GET` | `/api/v1/effects/store` | Room left for uploads | `/api/effects/store` |
| `GET`, `POST` | `/api/v1/schedule` | The list, whole, with its revision. An action's `power` is `on`, `off`, or `rise`, on rising from dark over a minute, and in place of an `effect` it can hold `"playlist": true`, which starts the playlist, turning on only if it has effects; both new after 1.0.0. The answer has `"schema": 2`. A `POST` without `"schema": 2` is refused with 409 while the list holds a rise or a playlist step, as a client of 1.0.0 would drop them unseen. The answer has `missing`, new after 1.0.0, the effects the actions name that this garland lacks, which it keeps, and a `POST` may send back, but cannot add one. Both answer 503 while the garland could not read its schedule yet, which it tries every minute: what it would show is no empty list, and a list saved over it would replace the real one | `/api/schedule` |
| `GET` | `/api/v1/schedule/timeline` | The next 24 hours, new after 1.0.0: `{"synced", "now", "power", "events"}`, `power` whether the garland was lit as the day was worked out, one snapshot with it, `now` the garland's own time, read once with the events it was worked out from, from which a client draws the day, each event `{"at", "time", "day", "entries"}` with the steps the actions due in that minute add up to, as an action spells them. A start of the playlist while it is empty comes to what the other steps of that minute do, here and in the `next` of `/api/v1/info` and `/api/v1/schedule` alike. `entries` are their places in the list, and more than one meet there | `/api/schedule/timeline` |
| `GET`, `POST` | `/api/v1/playlist` | New after 1.0.0: `{"effects", "missing", "dwell", "cross_fade", "playing", "max", "revision"}`. `missing` names those of `effects` this garland's catalog lacks, which it keeps and plays once they are there; a `POST` may send them back, and keeps them, but cannot add one. A `POST` of `{"effects": [...], "dwell": 10-3600, "cross_fade": 0-5000}` replaces the list; an empty one stops it, and its answer is the list as saved. With `"revision"`, the one the list was read with, it replaces it only if no one saved another since, else 409; a revision is counted from a random start at each start of the garland, so one read before a restart is refused too: an editor gives it, a restore that means to replace whatever is there does not. A `GET` when the list is there and could not be read is 503, never an empty list | `/api/playlist` |
| `POST` | `/api/v1/playlist/play` | `{"play": true}` starts the playlist, turning the garland on at its first effect, and `false` stops it. 409 for an empty list, and 503, the list unreadable, with nothing done. New after 1.0.0 | `/api/playlist/play` |
| `GET`, `POST` | `/api/v1/time` | Clock and zone. `zone_set` false means none was chosen, and a client may set its own; it is true while a saved zone could not be read yet, though the clock goes by UTC meanwhile | `/api/time` |
| `POST` | `/api/v1/time/rtc` | Sets the clock module. A time of day by hand, `"source": "manual"`, is 503, nothing changed, while a saved time zone could not be read yet, as it is in that zone, new after 1.1.1 | `/api/time/rtc` |
| `GET`, `POST` | `/api/v1/display` | Screen and screensaver. 503, nothing changed, while the settings saved could not be read. `GET` says `clock_module`, whether the clock module answered, new after 1.1.1; `scheduler24` is offered only with it, and a `POST` of it without is 409 unless it is the saver chosen already | `/api/display` |
| `POST` | `/api/v1/leds` | LED count, 0 for automatic | `/api/leds` |
| `GET`, `POST` | `/api/v1/leds/power` | The current limit's settings, new after 1.1.1: `{"supply_ma", "led_white_ma", "led_idle_ma", "margin_percent"}`, and what they come to, `board_ma`, `limit_ma`, the chain's `leds` and `peak_ma` at full white, `undimmed_leds`, with each setting's range as `supply_min` and so on (`supply_ma` 500-4400), and `invalid_record` when the saved settings could not be used and the cautious ones stand in. A `POST` sends all four and answers as a `GET`. 503, nothing changed, while the settings saved could not be read. A garland of 1.1.1 or before answers 404, and holds to 55 mA an LED at full white, 1 mA dark and 1400 mA | `/api/leds/power` |
| `GET`, `POST` | `/api/v1/wifi` | The home network | `/api/wifi` |
| `GET`, `POST` | `/api/v1/wifi/scan` | The networks in reach | `/api/wifi/scan` |
| `POST` | `/api/v1/network/name`, `/api/v1/network/certificate` | The mDNS name, and the certificate's domain. Answers `{"restarting": true}` when the garland restarts to take it, `false` when nothing changed | `/api/network/*` |
| `GET`, `POST` | `/api/v1/firmware` | Identity; installs an update file | `/api/ota` |
| `GET` | `/api/v1/info` | Board, memory, addresses, certificate | `/api/info` |
| `GET` | `/api/v1/clients` | What has access | `/api/auth/devices` |
| `POST` | `/api/v1/clients/forget` | Forgets one, `{"id": 1}` | `/api/auth/forget` |
| `GET` | `/api/v1/log?after=` | The log, below | New |

## The Log

For a sealed unit with no USB. It wants paired access, or the setup network, and never holds a password, token or key.

* **It costs nothing until it is read.** The first read starts the firmware keeping its log, in the 6 KB of RAM it otherwise keeps only while on trial, and it goes on keeping it until the next restart. So what came before the first read is not there, unless on trial.
* **The body is text.** `after` is the position the last answer ended at, 0 at first. `X-Log-Position` gives the next one, and `X-Log-Lost` the bytes that went round the buffer before they were read, so a client gets each line once and knows of a gap. `X-Log-Boot` says which start of the garland this is: under another one, a position means nothing, and the client asks from 0. `X-Log-Missed` counts lines since the last look that came while the log was being sent, as the firmware does not wait for that, and they are in neither the text nor `X-Log-Lost`.

## In the Firmware

* **Routes:** `v1` takes one wildcard entry per method in the server's table, and its own table in `portal.cpp`, rather than doubling them.
* **Answers:** `portal_send_err` and `portal_send_status` are the one way to fail: plain text on the portal, `{"error": ...}` on `v1`.
* **Sharing the handlers:** where a shape is the same, `v1` calls the portal's handler, so one fix serves both. So a change for the portal's sake that alters a shape must be looked at for `v1`. A test in `internal/api` fails if the firmware stops saying any field of its types, which is less than holding a whole shape still.
* **Authorization:** `find_caller` takes the Bearer header ahead of the cookie. A token is sent only by a program, so a Bearer request is not asked for its `Origin`. A cookie counts only with the request's `Origin` that of the portal itself, by its `Host` and the scheme of the server it came to, `https://` on HTTPS and `http://` on plain HTTP, or with none: `SameSite=Strict` tells sites apart, not origins, so a page on another name under the owner's own domain would otherwise have its requests carry the cookie (`auth_cookie_origin_ok` in `auth_logic.h`). On the setup network, which has no pairing, `Host` and `Origin` are checked instead.

