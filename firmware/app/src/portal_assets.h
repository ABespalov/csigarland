/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What every portal page shares, self-contained because the portal may have no internet
 * Contributors: None
 *
 * Not compiled as it stands: scripts/portal_pack.py gzips this and the pages into generated/portal_web.h.
 * Class names are one letter, as every byte here is a byte in flash: see kPortalStyle.
 */

#pragma once

namespace garland {

inline constexpr char kPortalPageTop[] = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>csiGarland</title>
<link rel="stylesheet" href="/portal.css">
<script src="/portal.js"></script>
</head>
<body>
<main>
  <h1><svg viewBox="0 0 59 11" width="177" height="33" fill="none" shape-rendering="crispEdges" role="img" aria-label="csiGarland"><path stroke="#fff" d="M14 0.5h1M1 2.5h3M7 2.5h3M13 2.5h2M0 3.5h1M6 3.5h1M14 3.5h1M0 4.5h1M7 4.5h3M14 4.5h1M0 5.5h1M4 5.5h1M10 5.5h1M14 5.5h1M1 6.5h3M6 6.5h4M13 6.5h3"/><path stroke="#3b82f6" transform="translate(18)" d="M1 0.5h3M19 0.5h2M40 0.5h1M0 1.5h1M4 1.5h1M20 1.5h1M40 1.5h1M0 2.5h1M7 2.5h3M12 2.5h1M14 2.5h2M20 2.5h1M25 2.5h3M30 2.5h1M32 2.5h2M37 2.5h2M40 2.5h1M0 3.5h1M2 3.5h3M10 3.5h1M12 3.5h2M16 3.5h1M20 3.5h1M28 3.5h1M30 3.5h2M34 3.5h1M36 3.5h1M39 3.5h2M0 4.5h1M4 4.5h1M7 4.5h4M12 4.5h1M20 4.5h1M25 4.5h4M30 4.5h1M34 4.5h1M36 4.5h1M40 4.5h1M0 5.5h1M4 5.5h1M6 5.5h1M10 5.5h1M12 5.5h1M20 5.5h1M24 5.5h1M28 5.5h1M30 5.5h1M34 5.5h1M36 5.5h1M40 5.5h1M1 6.5h4M7 6.5h4M12 6.5h1M19 6.5h3M25 6.5h4M30 6.5h1M34 6.5h1M37 6.5h4"/><linearGradient id="t"><stop stop-color="#fff"/><stop offset="1" stop-color="#3b82f6"/></linearGradient><text y="10.5" fill="url(#t)" stroke="none" font-size="3" font-family="system-ui,sans-serif" textLength="59">color your weekdays</text></svg><small id="ver"></small></h1>
  <nav id="nav" aria-label="Pages"></nav>
)html";

inline constexpr char kPortalPageBottom[] = R"html(</main>
</body>
</html>
)html";

inline constexpr char kPortalStyle[] = R"css(
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;padding:16px;background:#0f172a;color:#e2e8f0;font:16px/1.4 system-ui,sans-serif}
main{max-width:480px;margin:auto}
h1{display:flex;align-items:center;margin:8px 0 20px}
h1 small{margin-left:auto;font:14px system-ui,sans-serif;color:#94a3b8}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;margin:0}
dt{color:#94a3b8}
dd{margin:0;overflow-wrap:anywhere}
section{margin-bottom:16px;padding:16px;border-radius:8px;background:#1e293b}
h2{margin:0 0 12px;font-size:18px}
h3{margin:20px 0 8px;font-size:16px}
p{margin:0 0 12px}
ul{margin:0;padding:0;list-style:none}
label{display:flex;align-items:center;gap:10px;min-height:44px}
input,select{width:100%;min-height:44px;margin:4px 0 12px;padding:8px 10px;font:inherit;color:inherit;background:#0f172a;border:1px solid #475569;border-radius:6px}
input[type=radio],input[type=checkbox]{flex:none;width:20px;height:20px;min-height:0;margin:0;accent-color:#3b82f6}
button{min-width:44px;min-height:44px;padding:0 12px;font:inherit;font-weight:600;color:#fff;background:#2563eb;border:0;border-radius:6px;cursor:pointer}
button svg{display:block;width:20px;height:20px;margin:auto;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
:disabled{opacity:.5}
:focus-visible{outline:2px solid #93c5fd;outline-offset:2px}
fieldset{min-width:0;margin:0 0 12px;padding:0;border:0}
legend{padding:0;margin-bottom:4px}
summary{min-height:44px;padding:10px 0;cursor:pointer}
nav{display:flex;flex-wrap:wrap;gap:4px 16px;margin:0 0 16px}
nav a{padding:8px 0;color:#93c5fd;text-decoration:none}
nav a[aria-current]{color:#fff;font-weight:600;border-bottom:2px solid #3b82f6}
.q,.z{color:#e2e8f0;background:#334155}
.z{color:#fca5a5}
a{color:#93c5fd}
.b{display:block;padding:11px 12px;text-align:center;text-decoration:none;font-weight:600;color:#fff;background:#2563eb;border-radius:6px}
.l{padding-left:18px;list-style:disc}
.l li{margin-bottom:6px}
.y{font:700 40px ui-monospace,Consolas,monospace;text-align:center}
.m{color:#94a3b8;font-size:14px;overflow-wrap:anywhere}
.e{color:#fca5a5;font-size:14px}
.s{min-height:22px;margin:12px 0 0;font-size:14px}
.h{min-height:32px}
.g{margin-top:12px}
.r{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:12px}
.i{display:inline-flex;align-items:center;justify-content:center;gap:8px}
.i svg{margin:0}
.k{width:100%;display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:4px;text-align:left}
.x,.a{display:flex;align-items:center;gap:8px;min-height:52px;border-top:1px solid #334155}
.x:first-child,.a:first-child{border-top:0}
.x label,.a label{flex:none;min-width:40px;justify-content:center}
.x button,.a button{flex:none;padding:0 8px;font-size:14px}
.n,.w{flex:1;min-width:0;overflow-wrap:anywhere}
.c .n{color:#fbbf24;font-weight:600}
.w{font-size:14px}
.t{display:block;font-size:16px;font-weight:600}
.o .w{opacity:.55}
.d{display:flex;flex-wrap:wrap;gap:4px 12px}
.d legend{width:100%}
.d label{min-height:40px;gap:6px}
.p{columns:2;column-gap:16px;color:#94a3b8;font-size:14px}
.p li{padding:2px 0;break-inside:avoid;overflow-wrap:anywhere}
.u{font-weight:600;color:#e2e8f0;break-after:avoid}
.v{display:flex;align-items:center;gap:10px}
.v input,.v select{flex:1;min-width:0;margin:0}
.v input{padding:0;border:0;background:none;accent-color:#3b82f6}
.v output{flex:none;width:5ch;text-align:right}
)css";

inline constexpr char kPortalScript[] = R"js(
  const $ = (id) => document.getElementById(id);

  function report(id, text, isError) {
    $(id).textContent = text;
    $(id).className = 's ' + (isError ? 'e' : 'm');
  }

  /* A command's limit: one the garland has not answered by then is given up, and what it does is read
     again, as it may have done it or not. */
  function deadline(ms) {
    if (AbortSignal.timeout) return AbortSignal.timeout(ms);
    const controller = new AbortController();
    setTimeout(() => controller.abort(), ms);
    return controller.signal;
  }

  /* What a failed request says: one given up on says that, not the browser's words for it. */
  function failureText(error) {
    return error.name === 'TimeoutError' || error.name === 'AbortError' ? 'The garland did not answer in time: what it does is read again.' : error.message;
  }

  async function request(path, body, signal) {
    const options = body === undefined ? {} : {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    };
    if (signal) options.signal = signal;
    const response = await fetch(path, options);
    if (response.status === 401 && location.pathname !== '/pair') {
      location.href = `/pair?back=${encodeURIComponent(location.pathname)}`;
    }
    if (!response.ok) {
      const failure = new Error((await response.text()) || response.statusText);
      failure.retry = Number(response.headers.get('Retry-After')) || 0;
      failure.status = response.status;
      throw failure;
    }
    return response.json();
  }

  // Drawn for this project. The utility has the same ones, in internal/ui/glyphs_table.go.
  const GLYPHS = {add:'M12 5L12 19M5 12L19 12',bulb:'M9 21L15 21M9 17L15 17M9 17C9 14 5 13 5 9C5 5 8 3 12 3C16 3 19 5 19 9C19 13 15 14 15 17',close:'M6 6L18 18M18 6L6 18',computer:'M3 5L21 5L21 16L3 16ZM9 20L15 20M12 16L12 20',edit:'M4 20L5 15L16 4L20 8L9 19ZM14 6L18 10',globe:'M21 12C21 16.97 16.97 21 12 21C7.03 21 3 16.97 3 12C3 7.03 7.03 3 12 3C16.97 3 21 7.03 21 12ZM3 12L21 12M12 3C7 8 7 16 12 21C17 16 17 8 12 3Z',key:'M12 16C12 18.21 10.21 20 8 20C5.79 20 4 18.21 4 16C4 13.79 5.79 12 8 12C10.21 12 12 13.79 12 16ZM11 13L20 4M16 8L19 11',play:'M7 4L20 12L7 20Z',restore:'M8 5L4 9L8 13M4 9L15 9C22 9 22 20 15 20L8 20',save:'M5 4L16 4L20 8L20 20L5 20ZM8 4L8 9L15 9L15 4M8 20L8 14L16 14L16 20',search:'M17 11C17 14.31 14.31 17 11 17C7.69 17 5 14.31 5 11C5 7.69 7.69 5 11 5C14.31 5 17 7.69 17 11ZM16 16L21 21',trash:'M4 7L20 7M9 7L9 4L15 4L15 7M6 7L7 21L17 21L18 7M10 11L10 17M14 11L14 17',unlink:'M10 14C8 12 8 10 10 8L13 5C15 3 18 3 20 5C22 7 21 10 19 11M14 10C16 12 16 14 14 16L11 19C9 21 6 21 4 19C2 17 3 14 5 13M4 4L20 20',upload:'M12 15L12 4M7 9L12 4L17 9M4 15L4 20L20 20L20 15',wifi:'M2 9C8 3 16 3 22 9M6 13C10 9 14 9 18 13M10 17C11 16 13 16 14 17M12 20L12 20.5'};

  // A button shows a glyph and its words beside it. One on a row of a list is a glyph alone, to keep
  // the row short, and its words become its tooltip and what a screen reader says.
  function glyph(button, name, words, label, beside) {
    button.innerHTML = `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${GLYPHS[name]}"/></svg>`;
    if (beside) {
      button.classList.add('i');
      button.append(words);
      return;
    }
    button.title = words;
    button.setAttribute('aria-label', label || words);
  }

  function glyphButton(name, words, label, kind, onClick) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = kind;
    glyph(button, name, words, label);
    button.addEventListener('click', onClick);
    return button;
  }

  const PAGES = [['/', 'Effects'], ['/schedule', 'Schedule'], ['/device', 'Device'], ['/network', 'Network'], ['/firmware', 'Firmware'], ['/info', 'Info']];

  document.addEventListener('DOMContentLoaded', () => {
    for (const button of document.querySelectorAll('button[data-g]')) glyph(button, button.dataset.g, button.textContent.trim(), '', true);
    $('nav').replaceChildren(...PAGES.map(([path, name]) => {
      const link = document.createElement('a');
      link.href = path;
      link.textContent = name;
      if (path === location.pathname) {
        link.setAttribute('aria-current', 'page');
        document.title = `${name} - csiGarland`;
      }
      return link;
    }));
    request('/api/ota').then((info) => { $('ver').textContent = info.version; }).catch(() => {});
  });

  // Rows of [name, value] into a <dl>.
  function showFacts(id, rows) {
    $(id).replaceChildren(...rows.flatMap(([name, value]) => {
      const term = document.createElement('dt');
      term.textContent = name;
      const detail = document.createElement('dd');
      detail.textContent = value;
      return [term, detail];
    }));
  }

  function modeFacts(mode) {
    return [['Mode', mode.power ? 'On' : 'Off'], ['Effect', mode.effect_name], ['Brightness', `${mode.brightness}%`], ['Speed', `${mode.speed}x`]];
  }

  function describeSteps(action, effectNames = {}) {
    if (action.power === 'off') return 'Turn off';
    const steps = [];
    if (action.power === 'on') steps.push('Turn on');
    if (action.power === 'rise') steps.push('Turn on, rising over a minute');
    if (action.brightness) steps.push(`brightness ${action.brightness}%`);
    if (action.speed) steps.push(`speed ${action.speed}x`);
    const effect = action.effect_name || effectNames[action.effect] || action.effect;
    if (effect) steps.push(`effect ${effect}`);
    if (action.playlist) steps.push('start the playlist');
    return steps.join(', ');
  }

  function describeNext(next) {
    if (next.unread) return 'The schedule could not be read yet: the garland tries again every minute.';
    if (!next.clock) return 'Waiting for the clock to be set.';
    return next.at ? `${next.at}: ${describeSteps(next)}.` : 'Nothing in the coming week.';
  }

  const UPLOADED_GROUP = 'Uploaded';

  // Keeps the catalog's order, of the groups and within them. The uploaded come last, though one
  // that replaces a built-in effect keeps that effect's early place in the catalog.
  function byGroup(effects) {
    const groups = new Map();
    for (const effect of effects) {
      if (!groups.has(effect.group)) groups.set(effect.group, []);
      groups.get(effect.group).push(effect);
    }
    if (groups.has(UPLOADED_GROUP)) {
      const uploaded = groups.get(UPLOADED_GROUP);
      groups.delete(UPLOADED_GROUP);
      groups.set(UPLOADED_GROUP, uploaded);
    }
    return groups;
  }

  /* Time zone */
  const ZONES = [
    ['UTC', 'UTC0', ['UTC', 'Etc/UTC']],
    ['London, Dublin', 'GMT0BST,M3.5.0/1,M10.5.0', ['Europe/London', 'Europe/Dublin']],
    ['Lisbon', 'WET0WEST,M3.5.0/1,M10.5.0', ['Europe/Lisbon']],
    ['Berlin, Paris, Warsaw, Rome', 'CET-1CEST,M3.5.0,M10.5.0/3',
      ['Europe/Berlin', 'Europe/Paris', 'Europe/Warsaw', 'Europe/Rome', 'Europe/Madrid', 'Europe/Prague', 'Europe/Vienna', 'Europe/Amsterdam', 'Europe/Belgrade', 'Europe/Budapest']],
    ['Kyiv, Helsinki, Athens, Riga', 'EET-2EEST,M3.5.0/3,M10.5.0/4',
      ['Europe/Kyiv', 'Europe/Kiev', 'Europe/Helsinki', 'Europe/Athens', 'Europe/Riga', 'Europe/Vilnius', 'Europe/Tallinn', 'Europe/Sofia', 'Europe/Bucharest']],
    ['Jerusalem', 'IST-2IDT,M3.4.4/26,M10.5.0', ['Asia/Jerusalem', 'Asia/Tel_Aviv']],
    ['Moscow, Minsk', 'MSK-3', ['Europe/Moscow', 'Europe/Minsk']],
    ['Istanbul', '<+03>-3', ['Europe/Istanbul']],
    ['Dubai, Yerevan, Tbilisi, Baku', '<+04>-4', ['Asia/Dubai', 'Asia/Yerevan', 'Asia/Tbilisi', 'Asia/Baku', 'Europe/Samara']],
    ['Yekaterinburg, Tashkent, Almaty', '<+05>-5', ['Asia/Yekaterinburg', 'Asia/Tashkent', 'Asia/Almaty']],
    ['Omsk', '<+06>-6', ['Asia/Omsk']],
    ['Novosibirsk, Bangkok', '<+07>-7', ['Asia/Novosibirsk', 'Asia/Krasnoyarsk', 'Asia/Bangkok']],
    ['Irkutsk, Singapore', '<+08>-8', ['Asia/Irkutsk', 'Asia/Singapore']],
    ['Beijing', 'CST-8', ['Asia/Shanghai']],
    ['Tokyo', 'JST-9', ['Asia/Tokyo']],
    ['Vladivostok', '<+10>-10', ['Asia/Vladivostok']],
    ['New York', 'EST5EDT,M3.2.0,M11.1.0', ['America/New_York', 'America/Toronto']],
    ['Chicago', 'CST6CDT,M3.2.0,M11.1.0', ['America/Chicago']],
    ['Denver', 'MST7MDT,M3.2.0,M11.1.0', ['America/Denver']],
    ['Los Angeles', 'PST8PDT,M3.2.0,M11.1.0', ['America/Los_Angeles']],
  ];

  // The browser's own zone as a POSIX TZ string, which is what the garland needs.
  // A zone outside the table gets its current offset, with no clock changes.
  function browserZone() {
    try {
      const name = Intl.DateTimeFormat().resolvedOptions().timeZone;
      const found = ZONES.find((zone) => zone[2].includes(name));
      if (found) return found[1];
    } catch (error) {
      // Offsets below work without Intl.
    }
    const west = new Date().getTimezoneOffset();
    if (west === 0) return 'UTC0';
    const hours = Math.floor(Math.abs(west) / 60);
    const minutes = Math.abs(west) % 60;
    const pad = (value) => String(value).padStart(2, '0');
    const name = (west < 0 ? '+' : '-') + pad(hours) + (minutes ? pad(minutes) : '');
    return `<${name}>${west < 0 ? '-' : ''}${hours}${minutes ? ':' + pad(minutes) : ''}`;
  }

  // Until the owner picks a zone, the first browser to open any page sets it. The Schedule
  // page does this itself, to say so.
  if (location.pathname !== '/schedule') {
    request('/api/time').then((time) => {
      if (!time.zone_set) return request('/api/time', { zone: browserZone() });
      return null;
    }).catch(() => {});
  }
)js";

}  // namespace garland
