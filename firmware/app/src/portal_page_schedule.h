/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's Schedule page: the next action, the actions, the time zone and the clock module
 * Contributors: None
 */

#pragma once

namespace garland {

inline constexpr char kPageSchedule[] = R"html(
<section aria-labelledby="day-title">
  <h2 id="day-title">The next 24 hours</h2>
  <p class="m" id="clock" aria-live="polite"></p>
  <p class="m">Lit or dark as the actions leave it, from now. Where actions meet in one minute, the later in the list win, step by step.</p>
  <svg id="day-bar" viewBox="0 0 1440 28" width="100%" height="28" preserveAspectRatio="none" role="img" aria-label="The next 24 hours"></svg>
  <ul id="day"></ul>
</section>

<section aria-labelledby="actions-title">
  <h2 id="actions-title">Actions</h2>
  <ul id="actions"></ul>
  <button data-g="add" type="button" class="g" id="action-new" disabled>Add action</button>

  <form id="action-form" class="g" hidden>
    <label for="action-time" class="h">Time</label>
    <input type="time" id="action-time" required>
    <fieldset class="d" id="action-days"><legend>Days</legend></fieldset>
    <label><input type="radio" name="action-kind" id="kind-steps" checked> Do these steps</label>
    <label><input type="radio" name="action-kind" id="kind-off"> Turn off (nothing else can go with it)</label>
    <fieldset id="action-steps">
      <label><input type="checkbox" id="step-on"> Turn on</label>
      <label><input type="checkbox" id="step-rise"> Rising from dark over a minute</label>
      <label><input type="checkbox" id="step-brightness"> Brightness, %</label>
      <input type="number" id="action-brightness" min="1" max="100" value="50" aria-label="Brightness in percent">
      <label><input type="checkbox" id="step-speed"> Speed</label>
      <select id="action-speed" aria-label="Speed">
        <option value="0.25">0.25x</option><option value="0.5">0.5x</option><option value="0.75">0.75x</option><option value="1">1x</option><option value="1.5">1.5x</option><option value="2">2x</option><option value="3">3x</option>
      </select>
      <label><input type="checkbox" id="step-effect"> Start effect (also turns on)</label>
      <select id="action-effect" aria-label="Effect"></select>
      <label><input type="checkbox" id="step-playlist"> Start the playlist (also turns on)</label>
    </fieldset>
    <div class="r">
      <button data-g="save" type="submit" id="action-save">Save action</button>
      <button data-g="close" type="button" class="q" id="action-cancel">Cancel</button>
    </div>
  </form>
  <p class="s" id="schedule-status" role="status"></p>
</section>

<section aria-labelledby="zone-title">
  <h2 id="zone-title">Time zone</h2>
  <select id="zone" aria-labelledby="zone-title"></select>
  <p class="m" id="zone-hint"></p>
  <button data-g="save" type="button" id="zone-save">Save time zone</button>
  <p class="s" id="zone-status" role="status"></p>
</section>

<section aria-labelledby="rtc-title" id="rtc-box" hidden>
  <h2 id="rtc-title">Clock module</h2>
  <p class="m" id="rtc-state"></p>
  <label for="rtc-local" class="h">Date and time, for setting by hand</label>
  <input type="datetime-local" id="rtc-local" step="1">
  <div class="r">
    <button data-g="computer" type="button" class="q" id="rtc-browser">Set from device</button>
    <button data-g="globe" type="button" class="q" id="rtc-ntp">Set from internet</button>
    <button data-g="edit" type="button" class="q" id="rtc-manual">Set by hand</button>
  </div>
  <p class="s" id="rtc-status" role="status"></p>
</section>
<script>
  function zoneLabel(value) {
    const found = ZONES.find((zone) => zone[1] === value);
    return found ? found[0] : value;
  }

  /* Each answer of the clock is shown only if no answer to a later request was shown before it: of two
     saves, or a look and a save, the one asked last stands, in whatever order they come back. */
  let timeAsked = 0;
  let timeShown = 0;
  function showTimeAsked(asked, time) {
    if (asked < timeShown) return false;
    timeShown = asked;
    showTime(time);
    return true;
  }

  // Until the owner picks a zone, the first browser to open the portal sets it.
  async function loadTime() {
    let asked = ++timeAsked;
    let time = await request('/api/time');
    if (!time.zone_set) {
      const zone = browserZone();
      asked = ++timeAsked;
      time = await request('/api/time', { zone });
      if (showTimeAsked(asked, time)) {
        $('zone-hint').textContent = `Set from this browser: ${zoneLabel(zone)}. Change it here if that is wrong.`;
      }
      return time;
    }
    showTimeAsked(asked, time);
    return time;
  }

  /* A zone chosen and not saved yet is the owner's, which a look at the garland's clock leaves as it is. */
  let zoneChosen = false;

  function showTime(time) {
    if (time.synced) {
      $('clock').textContent = `Garland time: ${time.now}.`;
    } else {
      $('clock').textContent = 'The clock is not set: it syncs over the internet once the garland joins your home Wi-Fi. Actions wait until then.';
    }
    const select = $('zone');
    if (zoneChosen) {
      showClockModule(time.rtc);
      return;
    }
    select.replaceChildren(...ZONES.map((zone) => {
      const option = document.createElement('option');
      option.value = zone[1];
      option.textContent = zone[0];
      return option;
    }));
    if (!ZONES.some((zone) => zone[1] === time.zone)) {
      const option = document.createElement('option');
      option.value = time.zone;
      option.textContent = time.zone;
      select.append(option);
    }
    select.value = time.zone;
    $('zone-hint').textContent = '';
    showClockModule(time.rtc);
  }

  const CLOCK_SOURCES = { module: 'kept by the module', ntp: 'from the internet', browser: 'from a browser', manual: 'set by hand' };

  function showClockModule(rtc) {
    $('rtc-box').hidden = !rtc.present;
    if (!rtc.present) return;
    if (!rtc.trusted) {
      $('rtc-state').textContent = 'The module lost its time, perhaps with its battery. It is set from the internet as soon as there is a connection, or here.';
      return;
    }
    const source = CLOCK_SOURCES[rtc.source];
    $('rtc-state').textContent = `Module time: ${rtc.now}${source ? `, ${source}` : ''}. It follows the internet time once an hour.`;
  }

  async function setClockModule(body, done) {
    report('rtc-status', '', false);
    const asked = ++timeAsked;
    try {
      showTimeAsked(asked, await request('/api/time/rtc', body));
      report('rtc-status', done, false);
      refreshNext();
    } catch (error) {
      report('rtc-status', `Could not set the clock: ${error.message}`, true);
    }
  }

  $('rtc-browser').addEventListener('click', () => {
    setClockModule({ source: 'browser', epoch: Math.floor(Date.now() / 1000) }, 'Clock set from this device.');
  });

  $('rtc-ntp').addEventListener('click', async () => {
    await setClockModule({ source: 'ntp' }, 'Asked the internet for the time.');
    /* The answer comes in a few seconds, or a little more, and a clock already set says nothing of
       whether it came: the clock and the day are read again every 4 s for half a minute. */
    let looks = 0;
    const look = () => loadTime().then(refreshNext).catch(() => {}).finally(() => {
      if (++looks < 8) setTimeout(look, 4000);
    });
    setTimeout(look, 4000);
  });

  $('rtc-manual').addEventListener('click', () => {
    const local = $('rtc-local').value;
    if (!local) {
      report('rtc-status', 'Pick a date and time first.', true);
      return;
    }
    setClockModule({ source: 'manual', local }, 'Clock set.');
  });

  /* Counts the choices made, so a save's answer stands only for the choice it sent. */
  let zoneChoices = 0;
  $('zone').addEventListener('change', () => { zoneChosen = true; zoneChoices++; });
  /* One save at a time, in the order they were asked: the garland then saves the last one asked last, as
     the page shows it. */
  let zoneSaving = Promise.resolve();
  $('zone-save').addEventListener('click', () => {
    const zone = $('zone').value;
    const sent = zoneChoices;
    zoneSaving = zoneSaving.then(() => saveZone(zone, sent));
  });
  async function saveZone(zone, sent) {
    report('zone-status', '', false);
    const asked = ++timeAsked;
    try {
      const saved = await request('/api/time', { zone }, deadline(10000));
      // Another zone chosen while this went stays the owner's, to save in its turn.
      if (sent === zoneChoices) zoneChosen = false;
      showTimeAsked(asked, saved);
      report('zone-status', 'Time zone saved.', false);
      refreshNext();
    } catch (error) {
      report('zone-status', `Could not save: ${failureText(error)}`, true);
      /* One given up on may have been saved or not: the zone the garland has is read again, and the choice
         here stays chosen, to save once more. */
      loadTime().catch(() => {});
    }
  }

  /* Schedule */
  const DAYS = [[1, 'Mon'], [2, 'Tue'], [3, 'Wed'], [4, 'Thu'], [5, 'Fri'], [6, 'Sat'], [0, 'Sun']];
  let actions = [];
  let actionsMax = 0;
  let actionsRevision;
  let savingActions = false;
  let editing = -1;
  let effectNames = {};

  function describeDays(mask) {
    if (mask === 127) return 'every day';
    if (mask === 62) return 'Mon-Fri';
    if (mask === 65) return 'Sat, Sun';
    return DAYS.filter(([bit]) => mask & (1 << bit)).map(([, name]) => name).join(', ');
  }

  /* What is due, after a change of zone or clock: the next 24 hours say it all. */
  function refreshNext() {
    loadDay();
  }

  /* The day ahead as the garland works it out, drawn from whether it is lit now. */
  const WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const SVG = 'http://www.w3.org/2000/svg';

  function svgItem(name, attributes) {
    const item = document.createElementNS(SVG, name);
    for (const [key, value] of Object.entries(attributes)) item.setAttribute(key, value);
    return item;
  }

  /* Only the last asked for is drawn: one asked before a save or a clock set, and answered after the
     next, would draw the day as it was. */
  let dayAsked = 0;
  async function loadDay() {
    const asked = ++dayAsked;
    try {
      /* The state on both sides of the day: a step that came due in between, an Off at the minute, would
         leave the day drawn from the power before it, with the Off already behind its start. Read again
         if it moved, once. */
      const before = await request('/api/state');
      let day = await request('/api/schedule/timeline');
      let after = await request('/api/state');
      if (before.power !== after.power) {
        day = await request('/api/schedule/timeline');
        after = await request('/api/state');
      }
      if (asked === dayAsked) showDay(day, after.power);
    } catch (error) {
      /* Nothing of a day that could not be read is left to pass for it. */
      if (asked === dayAsked) {
        $('day-bar').replaceChildren();
        $('day').replaceChildren();
      }
    }
  }

  function showDay(day, litNow) {
    const bar = $('day-bar');
    const list = $('day');
    bar.replaceChildren(svgItem('rect', { x: 0, y: 4, width: 1440, height: 20, fill: '#1f2937' }));
    if (!day.synced) {
      list.replaceChildren(Object.assign(document.createElement('li'), { className: 'm', textContent: 'Waiting for the clock to be set.' }));
      return;
    }
    // The garland's time, which the events are of, or this one's from a firmware that does not say it.
    const start = day.now || Date.now() / 1000;
    const minuteOf = (event) => Math.max(0, Math.min(1440, Math.round((event.at - start) / 60)));
    let lit = litNow;
    let from = 0;
    const lights = [];
    for (const event of day.events) {
      const now = event.power === 'off' ? false : (event.power || event.effect || event.playlist ? true : lit);
      if (now !== lit) {
        if (lit) lights.push([from, minuteOf(event)]);
        lit = now;
        from = minuteOf(event);
      }
    }
    if (lit) lights.push([from, 1440]);
    for (const [a, b] of lights) bar.append(svgItem('rect', { x: a, y: 4, width: Math.max(b - a, 1), height: 20, fill: '#3b82f6' }));
    for (const event of day.events) {
      bar.append(svgItem('rect', { x: minuteOf(event) - 2, y: 0, width: 4, height: 28, fill: event.entries.length > 1 ? '#f59e0b' : '#e5e7eb' }));
    }
    list.replaceChildren(...day.events.map((event) => {
      const row = document.createElement('li');
      const when = document.createElement('span');
      when.className = 't';
      when.textContent = `${WEEKDAYS[event.day]} ${event.time}`;
      const meet = event.entries.length > 1 ? ` (${event.entries.length} actions meet here)` : '';
      row.append(when, ` ${describeSteps(event, effectNames)}${meet}`);
      return row;
    }));
    if (day.events.length === 0) {
      list.replaceChildren(Object.assign(document.createElement('li'), { className: 'm', textContent: 'Nothing in the next 24 hours.' }));
    }
  }

  function showActions() {
    $('actions-title').textContent = `Actions (${actions.length} of ${actionsMax})`;
    $('action-new').disabled = actions.length >= actionsMax;
    const order = actions.map((action, index) => index)
      .sort((a, b) => actions[a].time.localeCompare(actions[b].time));
    $('actions').replaceChildren(...order.map((index) => {
      const action = actions[index];
      const row = document.createElement('li');
      row.className = 'a' + (action.enabled ? '' : ' o');

      const label = document.createElement('label');
      const box = document.createElement('input');
      box.type = 'checkbox';
      box.checked = action.enabled;
      box.setAttribute('aria-label', `Action at ${action.time} enabled`);
      box.addEventListener('change', () => {
        saveActions(actions.map((other, i) => (i === index ? { ...other, enabled: box.checked } : other)));
      });
      label.append(box);

      const what = document.createElement('span');
      what.className = 'w';
      const when = document.createElement('span');
      when.className = 't';
      when.textContent = `${action.time}, ${describeDays(action.days)}`;
      what.append(when, describeSteps(action, effectNames));

      const edit = glyphButton('edit', 'Edit', `Edit action at ${action.time}`, 'q', () => openForm(index));
      const remove = glyphButton('trash', 'Delete', `Delete action at ${action.time}`, 'z', () => {
        saveActions(actions.filter((other, i) => i !== index));
      });

      row.append(label, what, edit, remove);
      return row;
    }));
    if (actions.length === 0) {
      const empty = document.createElement('li');
      empty.className = 'm';
      empty.textContent = 'No actions yet.';
      $('actions').append(empty);
    }
  }

  /* A form holds its action's place in the list, which says nothing of a list changed elsewhere: it
     closes, rather than save over whatever came to that place. A new action has no place to lose. */
  function takeSchedule(data) {
    if (editing >= 0 && data.revision !== actionsRevision) {
      closeForm();
      report('schedule-status', 'The list was changed elsewhere and read again: open the action again to change it.', true);
    }
    actions = data.entries;
    actionsMax = data.max;
    actionsRevision = data.revision;
    loadDay();
  }

  /* Each change is worked out from the list as it stands, so a second must not start from the list
     before the first: the block stays out of reach until the answer has come. Nor from a list not
     yet read, as what is sent replaces the one on the garland. */
  async function saveActions(list) {
    if (savingActions || actionsRevision === undefined) return false;
    savingActions = true;
    $('actions').inert = true;
    $('action-form').inert = true;
    report('schedule-status', '', false);
    try {
      const saved = await request('/api/schedule', { entries: list, revision: actionsRevision, schema: 2 });
      // Its own list now, which the form has done with.
      closeForm();
      takeSchedule(saved);
      showActions();
      return true;
    } catch (error) {
      report('schedule-status', `Could not save: ${error.message}`, true);
      if (error.status === 409) {
        takeSchedule(await request('/api/schedule').catch(() => ({ entries: actions, max: actionsMax, revision: actionsRevision, next: {} })));
      }
      showActions();
      return false;
    } finally {
      savingActions = false;
      $('actions').inert = false;
      $('action-form').inert = false;
    }
  }

  function syncStepInputs() {
    $('action-steps').disabled = $('kind-off').checked;
    $('step-rise').disabled = !$('step-on').checked;
    $('action-brightness').disabled = !$('step-brightness').checked;
    $('action-speed').disabled = !$('step-speed').checked;
    $('action-effect').disabled = !$('step-effect').checked;
  }

  function openForm(index) {
    editing = index;
    const action = index >= 0 ? actions[index]
      : { time: '18:00', days: 127, enabled: true, power: 'on' };
    $('action-time').value = action.time;
    for (const [bit] of DAYS) $(`day-${bit}`).checked = (action.days & (1 << bit)) !== 0;
    $('kind-off').checked = action.power === 'off';
    $('kind-steps').checked = action.power !== 'off';
    $('step-on').checked = action.power === 'on' || action.power === 'rise';
    $('step-rise').checked = action.power === 'rise';
    $('step-brightness').checked = Boolean(action.brightness);
    $('action-brightness').value = action.brightness || 50;
    $('step-speed').checked = Boolean(action.speed);
    $('action-speed').value = String(action.speed || 1);
    $('step-effect').checked = Boolean(action.effect);
    $('step-playlist').checked = Boolean(action.playlist);
    const pick = $('action-effect');
    pick.querySelector('[data-k]')?.remove();
    if (action.effect) {
      /* One this garland lacks stays as it is, rather than turn into the first listed, or into none. */
      if (![...pick.options].some((option) => option.value === action.effect)) {
        const kept = document.createElement('option');
        kept.value = action.effect;
        kept.textContent = `${action.effect} (not on this garland)`;
        kept.dataset.k = '';
        pick.prepend(kept);
      }
      pick.value = action.effect;
    }
    syncStepInputs();
    $('action-form').hidden = false;
    $('action-new').hidden = true;
    $('action-time').focus();
  }

  function closeForm() {
    editing = -1;
    $('action-form').hidden = true;
    $('action-new').hidden = false;
  }

  function readForm() {
    let days = 0;
    for (const [bit] of DAYS) if ($(`day-${bit}`).checked) days |= 1 << bit;
    if (days === 0) throw new Error('Pick at least one day.');
    const enabled = editing >= 0 ? actions[editing].enabled : true;
    const action = { enabled, time: $('action-time').value, days };
    if ($('kind-off').checked) return { ...action, power: 'off' };
    if ($('step-on').checked) action.power = $('step-rise').checked ? 'rise' : 'on';
    if ($('step-brightness').checked) {
      if (!$('action-brightness').reportValidity()) throw new Error('Brightness must be 1 to 100.');
      action.brightness = Number($('action-brightness').value);
    }
    if ($('step-speed').checked) action.speed = Number($('action-speed').value);
    if ($('step-effect').checked) {
      if (!$('action-effect').value) throw new Error('Choose an effect.');
      action.effect = $('action-effect').value;
    }
    else if ($('step-playlist').checked) action.playlist = true;
    if (Object.keys(action).length === 3) throw new Error('Pick at least one step.');
    return action;
  }

  for (const [bit, name] of DAYS) {
    const label = document.createElement('label');
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.id = `day-${bit}`;
    label.append(box, name);
    $('action-days').append(label);
  }
  for (const id of ['kind-off', 'kind-steps', 'step-on', 'step-brightness', 'step-speed', 'step-effect', 'step-playlist']) {
    $(id).addEventListener('change', syncStepInputs);
  }
  /* An effect or the playlist: one start at a time. */
  $('step-effect').addEventListener('change', () => { if ($('step-effect').checked) $('step-playlist').checked = false; });
  $('step-playlist').addEventListener('change', () => { if ($('step-playlist').checked) $('step-effect').checked = false; syncStepInputs(); });
  $('action-new').addEventListener('click', () => openForm(-1));
  $('action-cancel').addEventListener('click', closeForm);
  $('action-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    let action;
    try {
      action = readForm();
    } catch (error) {
      report('schedule-status', error.message, true);
      return;
    }
    const list = editing >= 0
      ? actions.map((other, index) => (index === editing ? action : other))
      : [...actions, action];
    await saveActions(list);
  });

  function fillEffectChoices(data) {
    effectNames = Object.fromEntries(data.effects.map((effect) => [effect.id, effect.name]));
    $('action-effect').replaceChildren(...[...byGroup(data.effects)].map(([group, effects]) => {
      const choices = document.createElement('optgroup');
      choices.label = group;
      choices.replaceChildren(...effects.map((effect) => {
        const option = document.createElement('option');
        option.value = effect.id;
        option.textContent = effect.name;
        return option;
      }));
      return choices;
    }));
  }

  request('/api/effects').then((data) => {
    fillEffectChoices(data);
    return request('/api/schedule');
  }).then((data) => {
    takeSchedule(data);
    showActions();
  }).catch((error) => report('schedule-status', error.message, true));
  loadTime().catch((error) => report('schedule-status', error.message, true));
  /* The garland syncs its clock by itself, and the day moves on: both are read again each minute while
     the page shows. */
  setInterval(() => {
    if (document.hidden) return;
    loadTime().then(refreshNext).catch(() => {});
  }, 60000);
</script>
)html";

}  // namespace garland
