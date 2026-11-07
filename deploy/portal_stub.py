# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Serves the portal's real pages with sample answers, for pictures and for trying a page.
# Contributors: None
#
# The pages, stylesheet and script are the firmware's own, from src/portal_*.h, put together as
# scripts/portal_pack.py does. Only the garland behind them is made up: the effects are the real
# catalog, and everything else below is a sample, with no address or key of any real board. Each
# page also tells this server how tall it came out, so that a picture can be exactly that tall.
#
# Example:
#   python deploy/portal_stub.py 8765

import json
import os
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "firmware" / "app" / "src"
EFFECTS = ROOT / "firmware" / "app" / "effects"
DEFINITION = re.compile(r'(k\w+)\[\] = R"(\w+)\((.*?)\)\2"', re.S)
PAGES = {
    "/": ("kPageEffects", "portal_page_effects.h"),
    "/schedule": ("kPageSchedule", "portal_page_schedule.h"),
    "/device": ("kPageDevice", "portal_page_device.h"),
    "/network": ("kPageNetwork", "portal_page_network.h"),
    "/firmware": ("kPageFirmware", "portal_page_firmware.h"),
    "/info": ("kPageInfo", "portal_page_info.h"),
    "/pair": ("kPagePair", "portal_page_pair.h"),
}
# Reports the height once the page has had its answers, and presses a button if the address asks.
# With ?rows=N on the Effects page, stops after the first N effects, for a picture that is not a
# long scroll; the height is then reported under the path with that query.
MEASURE = """<script>
  addEventListener('load', () => setTimeout(() => {
    const query = new URLSearchParams(location.search);
    const press = query.get('press');
    if (press) document.getElementById(press).click();
    setTimeout(() => {
      const count = Number(query.get('rows')) || 0;
      const rows = count > 0 ? document.querySelectorAll('#effects .x') : [];
      const end = rows.length >= count && count > 0 ? rows[count - 1] : document.querySelector('main');
      const key = location.pathname + (count > 0 ? '?rows=' + count : '');
      fetch('/stub/height?path=' + encodeURIComponent(key), {
        method: 'POST', body: String(Math.ceil(end.getBoundingClientRect().bottom) + 16) });
    }, 300);
  }, 400));
</script>
"""
VERSION = sys.argv[2] if len(sys.argv) > 2 else "1.1.0.0"
PLAYING = "garland-combination"
SAMPLE_NAME = "csigarland.local"
SAMPLE_IP = "192.168.1.50"
SAMPLE_FINGERPRINT = "7F6BBCE4" + "0" * 56
UNTIL = 1893456000  # A date to show, 2030.

heights = {}


def texts(path):
    return {name: body for name, _, body in DEFINITION.findall(path.read_text(encoding="utf-8"))}


def effect_list():
    catalog = json.loads((EFFECTS / "catalog.json").read_text(encoding="utf-8"))
    groups = {group["id"]: group["name"] for group in catalog["groups"]}
    effects = []
    for entry in catalog["effects"]:
        if entry.get("todo"):
            continue
        spec = json.loads((EFFECTS / f"{entry['id']}.json").read_text(encoding="utf-8"))
        effects.append(
            {
                "id": entry["id"],
                "name": spec["name"],
                "group": groups[entry["group"]],
                "favorite": bool(entry.get("favorite")),
                "active": entry["id"] == PLAYING,
                "uploaded": False,
                "built_in": True,
            }
        )
    return effects


ALL_EFFECTS = effect_list()
# To try the page with uploads in it: one of the owner's own, and a replacement for a built-in effect.
if os.environ.get("CSIGARLAND_STUB_UPLOADS"):
    ALL_EFFECTS[1].update(group="Uploaded", uploaded=True, name="My Waves")
    ALL_EFFECTS.append(dict(ALL_EFFECTS[0], id="my-snow", name="My Snow", group="Uploaded", favorite=False, active=False, uploaded=True, built_in=False))
PLAYING_NAME = next(effect["name"] for effect in ALL_EFFECTS if effect["id"] == PLAYING)
FAVORITES = sum(effect["favorite"] for effect in ALL_EFFECTS)
LEDS = {"count": 32, "setting": 0, "probe": "present", "estimate": 32, "kept": 0, "max": 256}
STATE = {"power": True, "brightness": 75, "effect": PLAYING, "effect_name": PLAYING_NAME, "speed": 1, "playlist": True, "playlist_at": 1, "playlist_size": 3, "leds": LEDS}
# One moment for everything the pages show of time: Friday 25 December 2026, 18:42, as this computer's
# clock would read it, so that the garland's time, the next action and the next 24 hours agree.
STUB_NOW = int(time.mktime((2026, 12, 25, 18, 42, 7, 0, 0, -1)))
# What the sample actions do next from then: the 21:00 step.
NEXT = {"clock": True, "at": "Fri 25 Dec, 21:00", "brightness": 40, "speed": 0.5}
ENTRIES = [
    {"enabled": True, "time": "17:00", "days": 127, "power": "on", "brightness": 75, "effect": PLAYING, "effect_name": PLAYING_NAME},
    {"enabled": True, "time": "21:00", "days": 127, "brightness": 40, "speed": 0.5},
    {"enabled": True, "time": "23:30", "days": 127, "power": "off"},
    {"enabled": True, "time": "07:00", "days": 62, "power": "rise", "brightness": 30},
    {"enabled": True, "time": "17:00", "days": 65, "playlist": True},
]
PLAYLIST = {"effects": [PLAYING] + [effect["id"] for effect in ALL_EFFECTS if effect["id"] != PLAYING][1:3], "dwell": 120, "cross_fade": 1500, "playing": False, "max": 16, "revision": 1}


# The next day as the garland would work it out: the sample actions, from the sample moment.
def timeline():
    now = STUB_NOW // 60
    events = []
    for minute in range(now + 1, now + 24 * 60 + 1):
        local = time.localtime(minute * 60)
        hhmm = f"{local.tm_hour:02d}:{local.tm_min:02d}"
        day = (local.tm_wday + 1) % 7
        due = [i for i, entry in enumerate(ENTRIES) if entry["time"] == hhmm and entry["days"] & (1 << day)]
        if due:
            event = {"at": minute * 60, "time": hhmm, "day": day, "entries": due}
            for i in due:
                event.update({key: value for key, value in ENTRIES[i].items() if key in ("power", "brightness", "speed", "effect", "playlist")})
            events.append(event)
    return {"synced": True, "now": STUB_NOW, "power": True, "events": events}
TIME = {
    "synced": True,
    "now": "2026-12-25 18:42",
    "zone": "CET-1CEST,M3.5.0,M10.5.0/3",
    "zone_set": True,
    "rtc": {"present": True, "trusted": True, "source": "ntp", "now": "2026-12-25 18:42:07"},
}
WIFI = {"connected": True, "configured": True, "ssid": "Home", "hostname": SAMPLE_NAME, "ip": SAMPLE_IP, "setup_network": False}
INFO = {
    **STATE,
    "model": "ESP32-C6-LCD-1.3",
    "chip": "ESP32-C6 rev 0.1",
    "mac": "00:00:5E:00:53:01",
    "ram_free": 150 * 1024,
    "ram_total": 320 * 1024,
    "ram_lowest": 106 * 1024,
    "flash_total": 4096 * 1024,
    "firmware_used": 1500 * 1024,
    "firmware_slot": 1920 * 1024,
    "ip": SAMPLE_IP,
    "setup_network": False,
    "hostname": SAMPLE_NAME,
    "https": True,
    "led_output": True,
    "certificate_names": "csigarland.local,192.168.4.1",
    "certificate_domain": "",
    "root_fingerprint": SAMPLE_FINGERPRINT,
    "certificate_until": UNTIL,
    "effects": len(ALL_EFFECTS),
    "favorites": FAVORITES,
    "next": NEXT,
    "domain": "",
}
ANSWERS = {
    "/api/state": STATE,
    "/api/effects": {"effects": ALL_EFFECTS},
    "/api/effects/store": {"max_file_bytes": 4096, "available": True, "uploaded": 0, "max": 32, "free_heap": 150 * 1024},
    "/api/effects/planned": {"planned": []},
    "/api/ota": {"version": VERSION, "on_trial": False, "trial_left": 0, "effects": len(ALL_EFFECTS), "pack_digest": "0" * 64, "image": "0" * 64, "boot": "0a1b2c3d"},
    "/api/time": TIME,
    "/api/schedule": {"entries": ENTRIES, "max": 32, "revision": 7, "next": NEXT},
    "/api/display": {"saver": "starfield", "delay": 60, "screen_brightness": 80, "saver_brightness": 30, "clock_module": True},
    "/api/wifi": WIFI,
    "/api/wifi/scan": {"scanning": False, "networks": [{"ssid": "Home", "rssi": -52, "secure": True}, {"ssid": "Neighbours", "rssi": -78, "secure": True}]},
    "/api/info": INFO,
    "/api/leds": STATE,
    "/api/leds/power": {
        "supply_ma": 2000, "led_white_ma": 40, "led_idle_ma": 1, "margin_percent": 20, "board_ma": 250, "limit_ma": 1400,
        "leds": 32, "peak_ma": 1312, "undimmed_leds": 34, "supply_min": 500, "supply_max": 20000, "led_white_min": 5,
        "led_white_max": 100, "led_idle_max": 5, "margin_min": 5, "margin_max": 20,
    },
    "/api/auth/devices": {"devices": [{"id": 1, "label": "Chrome Windows", "this": True, "expires": UNTIL}, {"id": 2, "label": "Safari iOS", "this": False, "expires": UNTIL}]},
    "/api/auth/request": {"request": "sample", "code": "4827", "seconds": 60},
    "/api/auth/wait": {"state": "waiting"},
    "/api/playlist": PLAYLIST,
    "/api/playlist/play": PLAYLIST,
}


def page(path):
    assets = texts(SRC / "portal_assets.h")
    name, header = PAGES[path]
    top = assets["kPortalPageTop"]
    if os.environ.get("CSIGARLAND_STUB_SHOTS"):
        top += "<script>window.EventSource = null;</script>"
    return top + texts(SRC / header)[name] + MEASURE + assets["kPortalPageBottom"]


class Stub(BaseHTTPRequestHandler):
    def reply(self, body, kind):
        data = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def answer(self):
        path = self.path.split("?")[0]
        if path == "/api/schedule/timeline":
            self.reply(json.dumps(timeline()), "application/json")
        elif path in ANSWERS:
            self.reply(json.dumps(ANSWERS[path]), "application/json")
        else:
            self.send_error(404)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/portal.css":
            self.reply(texts(SRC / "portal_assets.h")["kPortalStyle"], "text/css")
        elif path == "/portal.js":
            self.reply(texts(SRC / "portal_assets.h")["kPortalScript"], "text/javascript")
        elif path in PAGES:
            self.reply(page(path), "text/html")
        elif path == "/stub/heights":
            self.reply(json.dumps(heights), "application/json")
        elif path == "/api/events":
            self.events()
        else:
            self.answer()

    # Push updates as the garland sends them: the state at once, then a comment now and then.
    def events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            self.wfile.write(f"event: state\ndata: {json.dumps(STATE)}\n\n".encode())
            self.wfile.flush()
            if os.environ.get("CSIGARLAND_STUB_SHOTS"):
                return
            while True:
                time.sleep(15)
                self.wfile.write(b"event: alive\ndata: {}\n\n")
                self.wfile.flush()
        except OSError:
            pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
        if self.path.startswith("/stub/height?path="):
            try:
                heights[unquote(self.path.split("=", 1)[1])] = int(body)
            except ValueError:
                self.send_error(400)
                return
            self.reply("{}", "application/json")
        else:
            self.answer()

    # Deleting an uploaded effect: nothing here to delete, and nothing to say.
    def do_DELETE(self):
        self.reply("{}", "application/json")

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Stub).serve_forever()
