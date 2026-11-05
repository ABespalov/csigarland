# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Make the site's Russian pages under site/ru from its English pages and deploy/site-ru.json.
# Contributors: None
#
# What deploy/site-pages.ps1 does, writing the same pages to the byte, for deploy/site-pages.sh and the
# source rules. First every file of the site a page names gets the start of its SHA-256 in its address,
# then each element and attribute marked for translation takes its Russian text from the dictionary.
#
#   csg_site.py <repo> [--check]
from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

from csg_tool import Refusal

PAGES = ("index.html", "roadmap.html", "documents.html", "downloads.html")
ORIGIN = "https://garland.csilab.am/"
TEXT_EXTENSIONS = (".css", ".js", ".svg", ".json", ".txt", ".webmanifest")
TOGGLE = ('<a class="lang-switch" id="lang-switch" href="ru/([^"]*)" hreflang="ru" lang="ru" '
          'aria-label="Русская версия" title="Русская версия">RU</a>')
TOGGLE_BACK = ('<a class="lang-switch" id="lang-switch" href="../\\1" hreflang="en" lang="en" '
               'aria-label="English version" title="English version">EN</a>')
ATTRIBUTES = (("data-i18n-alt", "alt"), ("data-i18n-content", "content"), ("data-i18n-aria", "aria-label"))


def read_text(path: Path) -> str:
    with open(path, encoding="utf-8-sig", newline="") as stream:
        return stream.read()


def write_text(path: Path, text: str) -> None:
    with open(path, "w", encoding="utf-8", newline="") as stream:
        stream.write(text)


def html_text(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def file_version(path: Path) -> str:
    """The start of a file's SHA-256, of its text with lines ended by LF for a text file."""
    data = path.read_bytes()
    if path.suffix in TEXT_EXTENSIONS:
        data = data.decode("utf-8", errors="replace").replace("\r\n", "\n").encode("utf-8")
    return hashlib.sha256(data).hexdigest()[:10]


class Maker:
    def __init__(self, site: Path, dictionary: dict) -> None:
        self.site = site
        self.dictionary = dictionary
        self.used: set = set()
        self.problems: list = []

    def russian(self, key: str, page: str) -> str | None:
        if key not in self.dictionary:
            self.problems.append(f"deploy/site-ru.json: no text for {key}, used by site/{page}")
            return None
        self.used.add(key)
        return html_text(self.dictionary[key])

    def add_versions(self, html: str) -> str:
        """The page with every file of the site it names in its version. Pages, folders and other sites are left."""
        def versioned(match: re.Match) -> str:
            url = match.group(2)
            file = self.site / url
            if re.match(r"^([a-z]+:|/|\.\./)", url, re.I) or url.endswith(".html") or not file.is_file():
                return match.group(0)
            return f' {match.group(1)}="{url}?v={file_version(file)}"'
        return re.sub(r"\s(href|src)=\"([^\"?#]+)(?:\?v=[0-9a-f]+)?\"", versioned, html)

    def russian_page(self, page: str, html: str) -> str | None:
        if '<html lang="en">' not in html:
            self.problems.append(f'site/{page}: no <html lang="en">')
            return None
        html = html.replace('<html lang="en">', '<html lang="ru">')
        html = html.replace("<!DOCTYPE html>", f"<!DOCTYPE html>\n<!-- Made by deploy/site-pages.ps1 from site/{page} and deploy/site-ru.json: edit those, not this. -->")
        if len(re.findall(TOGGLE, html)) != 1:
            self.problems.append(f"site/{page}: not one language toggle leading to ru/")
            return None
        html = re.sub(TOGGLE, TOGGLE_BACK, html)
        html = re.sub(r'(rel="canonical" href="|property="og:url" content=")' + re.escape(ORIGIN), lambda m: f"{m.group(1)}{ORIGIN}ru/", html)

        names = "|".join(re.escape(name) for name in PAGES)

        def up_a_folder(match: re.Match) -> str:
            url = match.group(2)
            if re.match(r"^([a-z]+:|#|/|\.\./)", url, re.I) or re.match(rf"^({names})(#.*)?$", url, re.I):
                return match.group(0)
            return f' {match.group(1)}="../{url}"'
        html = re.sub(r"\s(href|src)=\"([^\"]*)\"", up_a_folder, html)

        expected = len(re.findall(r"\sdata-i18n=\"", html))
        done = [0]

        def translated(match: re.Match) -> str:
            done[0] += 1
            text = self.russian(match.group(3), page)
            if text is None:
                return match.group(0)
            return match.group(1) + text + match.group(5)
        html = re.sub(r"(<(\w+)\b[^>]*\sdata-i18n=\"([^\"]+)\"[^>]*>)([^<]*)(</\2>)", translated, html)
        if done[0] != expected:
            self.problems.append(f"site/{page}: {expected - done[0]} elements with data-i18n hold markup, not text")

        def attributes(match: re.Match) -> str:
            tag = match.group(0)
            for marker, name in ATTRIBUTES:
                key = re.search(rf"\s{marker}=\"([^\"]+)\"", tag)
                if not key:
                    continue
                text = self.russian(key.group(1), page)
                if text is None:
                    continue
                if not re.search(rf"\s{re.escape(name)}=\"[^\"]*\"", tag, re.I):
                    self.problems.append(f'site/{page}: {marker}="{key.group(1)}" on a tag with no {name}')
                    continue
                tag = re.sub(rf"(\s{re.escape(name)}=\")[^\"]*(\")", lambda m, t=text: m.group(1) + t + m.group(2), tag)
            return tag
        return re.sub(r"<[a-zA-Z][^>]*\sdata-i18n-(alt|content|aria)=\"[^>]*>", attributes, html)


def run(repo: Path, check: bool, quiet: bool = False) -> None:
    site = repo / "site"
    target = site / "ru"
    maker = Maker(site, json.loads(read_text(repo / "deploy/site-ru.json")))
    versioned, made = {}, {}
    for page in PAGES:
        versioned[page] = maker.add_versions(read_text(site / page))
        made[page] = maker.russian_page(page, versioned[page])
    for key in maker.dictionary:
        if key not in maker.used:
            maker.problems.append(f"deploy/site-ru.json: {key} is used by no page")
    if maker.problems:
        if not quiet:
            for problem in maker.problems:
                sys.stdout.write(f"{problem}\n")
        raise Refusal(f"{len(maker.problems)} problems with the site's Russian pages.")
    if check:
        stale = [f"site/{page}: versions of the files it names" for page in PAGES if read_text(site / page) != versioned[page]]
        for page in PAGES:
            path = target / page
            if not path.exists() or read_text(path) != made[page]:
                stale.append(f"site/ru/{page}")
        if target.exists():
            for extra in sorted(target.iterdir()):
                if extra.is_file() and extra.name.lower() not in PAGES:
                    stale.append(f"site/ru/{extra.name}: made from no page")
        if stale:
            raise Refusal(f"Not what deploy/site-pages.sh makes, run it: {', '.join(stale)}")
        if not quiet:
            sys.stderr.write("The site's Russian pages are current.\n")
        return
    for page in PAGES:
        write_text(site / page, versioned[page])
    target.mkdir(parents=True, exist_ok=True)
    for page in PAGES:
        write_text(target / page, made[page])
    sys.stderr.write(f"{len(PAGES)} Russian pages written to {target}.\n")


if __name__ == "__main__":
    try:
        run(Path(sys.argv[1]), check="--check" in sys.argv[2:])
    except Refusal as refusal:
        sys.stderr.write(f"{refusal}\n")
        sys.exit(1)
