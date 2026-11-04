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
  <details id="playlist-box">
    <summary id="playlist-title">Playlist</summary>
    <div id="playlist-editor">
    <p class="m">Plays these effects in turn, each for its time, one giving way to the next. Choosing a single effect, here or by a button, stops it. The fade between effects holds for every change of effect.</p>
    <ol id="playlist"></ol>
    <p class="r">
      <select id="playlist-choice" aria-label="Effect to add to the playlist"></select>
      <button data-g="add" type="button" class="q" id="playlist-add">Add</button>
    </p>
    <label for="playlist-dwell">Each effect plays, seconds</label>
    <input type="number" id="playlist-dwell" min="10" max="3600" value="60">
    <label for="playlist-fade">Fade from one effect to the next, ms</label>
    <input type="number" id="playlist-fade" min="0" max="5000" step="100" value="1000">
    <p class="r">
      <button data-g="save" type="button" id="playlist-save">Save playlist</button>
      <button data-g="play" type="button" class="q" id="playlist-play">Play the playlist</button>
    </p>
    </div>
    <p class="s" id="playlist-status" role="status"></p>
    <p class="r"><button data-g="restore" type="button" class="q" id="playlist-read" hidden>Read the playlist again</button></p>
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
    showPlaylist();
  }

  /* A list from the garland marks what played when it was made, which a state heard since may have
     changed: the state heard is newer, unless a choice of this page's is still on its way. */
  function takeEffects(data) {
    if (garland !== null && !effectBeingSent) {
      for (const effect of data.effects) effect.active = effect.id === garland.effect;
    }
    showEffects(data);
  }

  // The state as last heard, which a button on the garland can change at any time.
  let garland = null;

  function showNow() {
    if (garland === null) return;
    const favorites = effectList.filter((effect) => effect.favorite).length;
    $('now-effects').textContent = `${effectList.length}, ${favorites} in favourites`;
    $('now-mode').textContent = (garland.power ? 'On' : 'Off') + (garland.playlist ? `, playlist ${garland.playlist_at}/${garland.playlist_size}` : '');
    glyph($('playlist-play'), garland.playlist ? 'close' : 'play', garland.playlist ? 'Stop the playlist' : 'Play the playlist', undefined, true);
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
  // Moves with every change made here and every state pushed: a state asked for before it is older news.
  let nowChanges = 0;
  /* The state is to be asked for though the stream is open: one pushed was passed over while a change of
     this page's was on its way, whose answer may be older, or the page's own change wants reading back.
     Kept until a look succeeds, through a hidden tab and a failed request. */
  let mustRead = false;

  // As with effects: one request out, and the next carries the latest, however fast the slider
  // moves. The garland keeps up as it goes, and saves once it has settled.
  async function changeMode(change) {
    report('effects-status', '', false);
    garland = { ...garland, ...change };
    showNow();
    wantedMode = { ...wantedMode, ...change };
    nowChanges++;
    if (modeBeingSent) return;
    modeBeingSent = true;
    let failed = false;
    while (wantedMode !== null) {
      const next = wantedMode;
      wantedMode = null;
      try {
        const answer = await request('/api/state', next, deadline(10000));
        if (wantedMode === null) garland = answer;
      } catch (error) {
        failed = true;
        report('effects-status', failureText(error), true);
      }
    }
    modeBeingSent = false;
    if (failed) mustRead = true;
    if (mustRead) refreshNow();
    else showNow();
  }

  for (const [id, field] of [['now-brightness', 'brightness'], ['now-speed', 'speed']]) {
    $(id).addEventListener('pointerdown', () => { heldSlider = $(id); });
    $(id).addEventListener('input', () => changeMode({ [field]: Number($(id).value) }));
  }
  addEventListener('pointerup', () => { heldSlider = null; });
  addEventListener('pointercancel', () => { heldSlider = null; });

  /* Asks for the state while there is no stream, or while mustRead says the stream's may have been
     passed over, as it says each change only once. */
  async function refreshNow() {
    if (document.hidden || effectBeingSent || modeBeingSent) return;
    if (!mustRead && pushed !== null && pushed.readyState === EventSource.OPEN) return;
    const changesBefore = nowChanges;
    try {
      const heard = await request('/api/state');
      // Asked before a change made or pushed since, so it can only take that back: the next look asks again.
      if (changesBefore !== nowChanges) return;
      mustRead = false;
      takeNow(heard);
    } catch (error) {
      // The next look may do better, and the list below says what is wrong.
    }
  }

  // What the garland says it does now, asked for or pushed.
  function takeNow(heard) {
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
  }

  /* Pushed by the garland as it changes. While the stream is open the page asks nothing; when it is
     not, as with a garland of 1.0.0 or two streams open elsewhere, it asks every few seconds. */
  let pushed = null;
  /* The garland says it is there every 15 s: a stream with nothing heard for 45 s has fallen silent,
     and is closed, so the page asks as with none, and opens another a while later. */
  const SILENT_MS = 45000;
  const REOPEN_MS = 60000;
  /* A stream the garland refused, as with two open elsewhere, ends closed, and the browser does not try
     again by itself: the page does, waiting longer each time, up to five minutes. */
  const REFUSED_FIRST_MS = 10000;
  let refusedWait = REFUSED_FIRST_MS;
  let heardAt = 0;
  function openStream() {
    if (!window.EventSource) return;
    heardAt = Date.now();
    pushed = new EventSource('/api/events');
    pushed.addEventListener('open', () => { refusedWait = REFUSED_FIRST_MS; });
    pushed.addEventListener('alive', () => { heardAt = Date.now(); });
    pushed.addEventListener('state', (event) => {
      heardAt = Date.now();
      if (effectBeingSent || modeBeingSent) {
        mustRead = true;
        return;
      }
      try {
        const heard = JSON.parse(event.data);
        nowChanges++;
        takeNow(heard);
      } catch (error) {
        // A word the page cannot read is passed over: the next one, or a look, does better.
      }
    });
  }
  openStream();
  setInterval(() => {
    if (pushed !== null && pushed.readyState === EventSource.CLOSED) {
      pushed = null;
      setTimeout(openStream, refusedWait);
      refusedWait = Math.min(refusedWait * 2, 300000);
      return;
    }
    if (pushed === null || pushed.readyState !== EventSource.OPEN || Date.now() - heardAt < SILENT_MS) return;
    pushed.close();
    pushed = null;
    refreshNow();
    setTimeout(openStream, REOPEN_MS);
  }, 5000);

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
      mustRead = true;
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
    request('/api/effects').then(takeEffects).catch((error) => report('effects-status', error.message, true));
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
    nowChanges++;
    if (effectBeingSent) return;
    effectBeingSent = true;
    while (wantedEffect !== null) {
      const next = wantedEffect;
      wantedEffect = null;
      try {
        await request('/api/effect', { id: next }, deadline(10000));
      } catch (error) {
        report('effects-status', failureText(error), true);
        request('/api/effects').then(takeEffects).catch(() => {});
      }
    }
    effectBeingSent = false;
    mustRead = true;
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

  /* Playlist: edited here, and sent whole with Save. */
  let playlist = { effects: [], dwell: 60, cross_fade: 1000, max: 16 };

  function showPlaylist() {
    const names = Object.fromEntries(effectList.map((effect) => [effect.id, effect.name]));
    $('playlist-title').textContent = `Playlist (${playlist.effects.length} of ${playlist.max})`;
    $('playlist').replaceChildren(...playlist.effects.map((id, index) => {
      const row = document.createElement('li');
      row.className = 'x';
      const name = document.createElement('span');
      name.className = 'n';
      /* One this garland lacks is kept by its id, and played once it is here again. */
      name.textContent = names[id] || `${id} (not on this garland)`;
      row.append(name, glyphButton('trash', 'Remove', `Remove ${names[id] || id} from the playlist`, 'z', () => {
        playlist.effects.splice(index, 1);
        showPlaylist();
      }));
      return row;
    }));
    /* What the owner picked stays picked while it can still be added, however often the list is drawn. */
    const picked = $('playlist-choice').value;
    $('playlist-choice').replaceChildren(...effectList.filter((effect) => !playlist.effects.includes(effect.id)).map((effect) => {
      const option = document.createElement('option');
      option.value = effect.id;
      option.textContent = effect.name;
      return option;
    }));
    if ([...$('playlist-choice').options].some((option) => option.value === picked)) $('playlist-choice').value = picked;
    $('playlist-add').disabled = playlist.effects.length >= playlist.max || $('playlist-choice').options.length === 0;
  }

  /* The editor is out of reach until the garland's list has come: an edit made before it would be lost
     to it, or saved over it. Its title, what went wrong and the way to read it again stay in reach. */
  let playlistRead = false;

  function takePlaylist(data) {
    playlist = data;
    playlistRead = true;
    $('playlist-editor').inert = false;
    $('playlist-read').hidden = true;
    $('playlist-dwell').value = data.dwell;
    $('playlist-fade').value = data.cross_fade;
    showPlaylist();
  }

  $('playlist-add').addEventListener('click', () => {
    if ($('playlist-choice').value) playlist.effects.push($('playlist-choice').value);
    showPlaylist();
  });
  $('playlist-save').addEventListener('click', async () => {
    report('playlist-status', '', false);
    if (!$('playlist-dwell').reportValidity() || !$('playlist-fade').reportValidity()) return;
    /* Out of reach while it is sent: its answer is the list as sent, and replaces the editor's. With the
       revision it was read with, so a list another page or program saved since is not written over. */
    $('playlist-editor').inert = true;
    try {
      takePlaylist(await request('/api/playlist', { effects: playlist.effects, dwell: Number($('playlist-dwell').value), cross_fade: Number($('playlist-fade').value), revision: playlist.revision }));
      report('playlist-status', 'Playlist saved.', false);
    } catch (error) {
      report('playlist-status', `Could not save: ${error.message}`, true);
      $('playlist-read').hidden = false;
    } finally {
      $('playlist-editor').inert = !playlistRead;
    }
  });
  /* Reading it again puts the garland's list in place of what is in the editor. */
  function readPlaylist() {
    $('playlist-editor').inert = true;
    $('playlist-read').hidden = true;
    request('/api/playlist').then(takePlaylist).catch((error) => {
      report('playlist-status', error.message, true);
      $('playlist-read').hidden = false;
      $('playlist-editor').inert = !playlistRead;
    });
  }
  $('playlist-read').addEventListener('click', () => {
    report('playlist-status', '', false);
    readPlaylist();
  });
  $('playlist-play').addEventListener('click', async () => {
    report('playlist-status', '', false);
    try {
      /* Whether it plays, and nothing of the editor's list, which may hold what is not saved yet. */
      playlist.playing = (await request('/api/playlist/play', { play: !(garland && garland.playlist) })).playing;
      mustRead = true;
      refreshNow();
    } catch (error) {
      report('playlist-status', error.message, true);
    }
  });

  const NOW_POLL_MS = 5000;

  loadEffects();
  readPlaylist();
  refreshNow();
  setInterval(refreshNow, NOW_POLL_MS);
  request('/api/effects/planned').then(showPlanned).catch(() => {});
</script>
)html";

}  // namespace garland
