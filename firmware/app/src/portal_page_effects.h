/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's Effects page
 * Contributors: None
 */

#pragma once

namespace garland {

inline constexpr char kPageEffects[] = R"html(
<section aria-labelledby="now-title">
  <h2 id="now-title">Now</h2>
  <dl>
    <dt>Effects</dt><dd id="now-effects"></dd>
    <dt>Mode</dt><dd id="now-mode"></dd>
    <dt>Effect</dt><dd id="now-effect"></dd>
    <dt><label for="now-brightness">Brightness</label></dt>
    <dd class="v"><input type="range" id="now-brightness" min="1" max="100" disabled><output id="now-brightness-text" for="now-brightness"></output></dd>
    <dt><label for="now-speed">Speed</label></dt>
    <dd class="v"><input type="range" id="now-speed" min="0.25" max="5" step="0.25" disabled><output id="now-speed-text" for="now-speed"></output></dd>
  </dl>
</section>
<section aria-labelledby="effects-title">
  <h2 id="effects-title">Effects</h2>
  <p class="m">Tick the favourites: the EFFECT button cycles through these. Play starts any effect.</p>
  <ul id="effects"></ul>
  <p class="s" id="effects-status" role="status"></p>
  <details id="upload-box">
    <summary>Add an effect from a file</summary>
    <p class="m" id="upload-info"></p>
    <p class="m">The file's name is the effect's id: my-effect.json makes my-effect. A file named as a built-in effect replaces it until deleted.</p>
    <label for="upload-file">Effect file</label>
    <input type="file" id="upload-file" accept=".json,application/json">
    <p class="r">
      <button data-g="bulb" type="button" id="upload-preview" class="q">Preview on the garland</button>
      <button data-g="upload" type="button" id="upload-save">Save on the garland</button>
    </p>
    <p class="s" id="upload-status" role="status"></p>
  </details>
  <details id="planned-box" hidden>
    <summary class="m" id="planned-title"></summary>
    <ul class="p" id="planned"></ul>
  </details>
</section>
<script>
  // What the owner opened or closed stays that way across redraws of the list.
  const groupOpen = new Map();

  // The last list from the garland. Clicks change it here at once, and the garland hears
  // about it behind the scenes.
  let effectList = [];

  function showEffects(data) {
    effectList = data.effects;
    const list = $('effects');
    list.replaceChildren();
    const groups = byGroup(data.effects);
    // There even when empty, as this is where an upload will be found.
    if (!groups.has(UPLOADED_GROUP)) groups.set(UPLOADED_GROUP, []);
    for (const [group, effects] of groups) {
      const item = document.createElement('li');
      const box = document.createElement('details');
      const inUse = effects.some((effect) => effect.favorite || effect.active) || (group === UPLOADED_GROUP && effects.length > 0);
      box.open = groupOpen.has(group) ? groupOpen.get(group) : inUse;
      box.addEventListener('toggle', () => groupOpen.set(group, box.open));
      const title = document.createElement('summary');
      const favorites = effects.filter((effect) => effect.favorite).length;
      title.textContent = `${group} (${favorites ? `${favorites} of ` : ''}${effects.length})`;
      const rows = document.createElement('ul');
      rows.replaceChildren(...effects.map(effectRow));
      box.append(title, rows);
      if (effects.length === 0) {
        const none = document.createElement('p');
        none.className = 'm';
        none.textContent = 'None yet. Add one from a file below.';
        box.append(none);
      }
      item.append(box);
      list.append(item);
    }
    showNow();
  }

  // The state as last heard, which a button on the garland can change at any time.
  let garland = null;

  function showNow() {
    if (garland === null) return;
    const favorites = effectList.filter((effect) => effect.favorite).length;
    $('now-effects').textContent = `${effectList.length}, ${favorites} in favourites`;
    $('now-mode').textContent = garland.power ? 'On' : 'Off';
    $('now-effect').textContent = garland.effect_name;
    $('now-brightness-text').textContent = `${garland.brightness}%`;
    $('now-speed-text').textContent = `${garland.speed}x`;
    // Not under a finger still on a slider, which a look at the garland must not move.
    if (heldSlider !== $('now-brightness')) $('now-brightness').value = garland.brightness;
    if (heldSlider !== $('now-speed')) $('now-speed').value = garland.speed;
    $('now-brightness').disabled = false;
    $('now-speed').disabled = false;
  }

  let heldSlider = null;
  let wantedMode = null;
  let modeBeingSent = false;
  let modeChanges = 0;

  // As with effects: one request out, and the next carries the latest, however fast the slider
  // moves. The garland keeps up as it goes, and saves once it has settled.
  async function changeMode(change) {
    report('effects-status', '', false);
    garland = { ...garland, ...change };
    showNow();
    wantedMode = { ...wantedMode, ...change };
    modeChanges++;
    if (modeBeingSent) return;
    modeBeingSent = true;
    let failed = false;
    while (wantedMode !== null) {
      const next = wantedMode;
      wantedMode = null;
      try {
        const answer = await request('/api/state', next);
        if (wantedMode === null) garland = answer;
      } catch (error) {
        failed = true;
        report('effects-status', error.message, true);
      }
    }
    modeBeingSent = false;
    if (failed) refreshNow();
    else showNow();
  }

  for (const [id, field] of [['now-brightness', 'brightness'], ['now-speed', 'speed']]) {
    $(id).addEventListener('pointerdown', () => { heldSlider = $(id); });
    $(id).addEventListener('input', () => changeMode({ [field]: Number($(id).value) }));
  }
  addEventListener('pointerup', () => { heldSlider = null; });
  addEventListener('pointercancel', () => { heldSlider = null; });

  async function refreshNow() {
    if (document.hidden || effectBeingSent || modeBeingSent) return;
    const changesBefore = modeChanges;
    try {
      const heard = await request('/api/state');
      // Asked before a change that has been made since, so it can only take that back.
      if (changesBefore !== modeChanges) return;
      garland = heard;
      const active = effectList.find((effect) => effect.active);
      // One not in the list, as a file previewed, marks none, and no look need draw the list again.
      const playing = effectList.some((effect) => effect.id === garland.effect) ? garland.effect : '';
      if (effectList.length > 0 && (active ? active.id : '') !== playing) {
        for (const effect of effectList) effect.active = effect.id === garland.effect;
        showEffects({ effects: effectList });
      } else {
        showNow();
      }
    } catch (error) {
      // The next look may do better, and the list below says what is wrong.
    }
  }

  function effectRow(effect) {
      const row = document.createElement('li');
      row.className = 'x' + (effect.active ? ' c' : '');

      const label = document.createElement('label');
      const box = document.createElement('input');
      box.type = 'checkbox';
      box.checked = effect.favorite;
      box.setAttribute('aria-label', effect.name + ' in the button cycle');
      box.addEventListener('change', () => setFavorite(effect.id, box));
      label.append(box);

      const name = document.createElement('span');
      name.className = 'n';
      name.textContent = effect.name;

      const play = glyphButton('play', effect.active ? 'Playing now' : 'Play', 'Play ' + effect.name, effect.active ? '' : 'q', () => activate(effect.id));

      row.append(label, name, play);
      if (effect.uploaded) {
        const words = effect.built_in ? 'Restore' : 'Delete';
        row.append(glyphButton(effect.built_in ? 'restore' : 'trash', words, `${words} ${effect.name}`, effect.built_in ? 'q' : 'z', () => removeEffect(effect)));
      }
      return row;
  }

  async function removeEffect(effect) {
    const question = effect.built_in ? `Go back to the built-in ${effect.name}?` : `Delete ${effect.name} from the garland?`;
    if (!confirm(question)) return;
    report('effects-status', '', false);
    try {
      await send(`/api/effects/file?id=${encodeURIComponent(effect.id)}`, 'DELETE');
      loadEffects();
    } catch (error) {
      report('effects-status', error.message, true);
    }
  }

  async function send(path, method, text) {
    const response = await fetch(path, { method, headers: { 'Content-Type': 'application/json' }, body: text });
    // As request() does: access that has run out is asked for again.
    if (response.status === 401) location.href = `/pair?back=${encodeURIComponent(location.pathname)}`;
    if (!response.ok) throw new Error((await response.text()) || response.statusText);
    return response.json();
  }

  async function chosenFile() {
    const file = $('upload-file').files[0];
    if (!file) throw new Error('Choose a file first');
    if (file.size > uploadLimit) throw new Error(`The file is over ${uploadLimit} bytes`);
    return file;
  }

  async function previewFile() {
    report('upload-status', 'Sending...', false);
    try {
      const file = await chosenFile();
      await send('/api/effects/preview', 'POST', await file.text());
      report('upload-status', `Playing ${file.name}. Nothing is saved; choose another effect to end it.`, false);
      refreshNow();
      for (const effect of effectList) effect.active = false;
      showEffects({ effects: effectList });
    } catch (error) {
      report('upload-status', error.message, true);
    }
  }

  async function saveFile() {
    report('upload-status', 'Saving...', false);
    try {
      const file = await chosenFile();
      const id = file.name.replace(/\.json$/i, '');
      await send(`/api/effects/file?id=${encodeURIComponent(id)}`, 'POST', await file.text());
      report('upload-status', `Saved as ${id}.`, false);
      loadEffects();
    } catch (error) {
      report('upload-status', error.message, true);
    }
  }

  let uploadLimit = 8192;

  function loadEffects() {
    request('/api/effects').then(showEffects).catch((error) => report('effects-status', error.message, true));
    request('/api/effects/store').then((store) => {
      uploadLimit = store.max_file_bytes;
      $('upload-save').disabled = !store.available;
      $('upload-info').textContent = store.available
        ? `${store.uploaded} of ${store.max} uploaded. Free memory: ${Math.round(store.free_heap / 1024)} KB.`
        : 'This board needs one flash over USB before it can keep effects. Preview works already.';
    }).catch(() => {});
  }

  $('upload-preview').addEventListener('click', previewFile);
  $('upload-save').addEventListener('click', saveFile);

  async function setFavorite(id, box) {
    report('effects-status', '', false);
    try {
      await request('/api/favorites', { id, favorite: box.checked });
      effectList.find((effect) => effect.id === id).favorite = box.checked;
      showEffects({ effects: effectList });
    } catch (error) {
      box.checked = !box.checked;
      report('effects-status', error.message, true);
    }
  }

  let wantedEffect = null;
  let effectBeingSent = false;

  // However fast the clicks come, only one request is out, and the next carries the latest
  // choice: the ones in between would only make the garland catch up on old news.
  async function activate(id) {
    report('effects-status', '', false);
    for (const effect of effectList) effect.active = effect.id === id;
    showEffects({ effects: effectList });
    wantedEffect = id;
    if (effectBeingSent) return;
    effectBeingSent = true;
    while (wantedEffect !== null) {
      const next = wantedEffect;
      wantedEffect = null;
      try {
        await request('/api/effect', { id: next });
      } catch (error) {
        report('effects-status', error.message, true);
        request('/api/effects').then(showEffects).catch(() => {});
      }
    }
    effectBeingSent = false;
    refreshNow();
  }

  function showPlanned(data) {
    if (data.planned.length === 0) return;
    $('planned-title').textContent = `Planned, not playable yet (${data.planned.length})`;
    const items = [];
    for (const [group, effects] of byGroup(data.planned)) {
      const title = document.createElement('li');
      title.textContent = group;
      title.className = 'u';
      items.push(title);
      for (const effect of effects) {
        const item = document.createElement('li');
        item.textContent = effect.name;
        items.push(item);
      }
    }
    $('planned').replaceChildren(...items);
    $('planned-box').hidden = false;
  }

  const NOW_POLL_MS = 5000;

  loadEffects();
  refreshNow();
  setInterval(refreshNow, NOW_POLL_MS);
  request('/api/effects/planned').then(showPlanned).catch(() => {});
</script>
)html";

}  // namespace garland
