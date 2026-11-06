/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's Device page: the LED count and the screen
 * Contributors: None
 */

#pragma once

namespace garland {

inline constexpr char kPageDevice[] = R"html(
<section aria-labelledby="leds-title">
  <h2 id="leds-title">LED count</h2>
  <p class="m" id="current" aria-live="polite">Loading...</p>
  <form id="form">
    <label><input type="radio" name="mode" value="auto" id="auto"> Detect automatically</label>
    <label><input type="radio" name="mode" value="manual" id="manual"> Set manually</label>
    <input type="number" id="count" min="1" step="1" inputmode="numeric" aria-label="Number of LEDs">
    <button data-g="save" type="submit" id="save">Save</button>
  </form>
  <p class="s" id="status" role="status"></p>
</section>

<section aria-labelledby="screen-title">
  <h2 id="screen-title">Screen</h2>
  <p class="m">The screensaver starts when no button has been pressed for this long. Any button wakes the screen.</p>
  <form id="saver-form">
    <label for="screen-brightness" class="h">Screen brightness, %</label>
    <input type="number" id="screen-brightness" min="5" max="100" step="1" inputmode="numeric" required>
    <label for="saver" class="h g">Screensaver</label>
    <select id="saver">
      <option value="none">None</option>
      <option value="starfield">Starfield</option>
      <option value="flying-clock">Flying Clock</option>
      <option value="breathing-dot">Breathing Dot</option>
      <option value="black-screen">Black Screen</option>
    </select>
    <label for="saver-delay" class="h g">Delay, seconds</label>
    <input type="number" id="saver-delay" min="10" max="600" step="1" inputmode="numeric" required>
    <label for="saver-brightness" class="h g">Brightness during the screensaver, %</label>
    <input type="number" id="saver-brightness" min="5" max="100" step="1" inputmode="numeric" required>
    <button data-g="save" type="submit" id="saver-save" class="g">Save</button>
  </form>
  <p class="s" id="saver-status" role="status"></p>
</section>
<script>
  /* LED count */
  function describe(leds) {
    if (leds.kept !== 0) return `Could not change to ${leds.count} LEDs. Still sending to ${leds.kept}: restart the garland, or choose another number.`;
    if (leds.setting !== 0) return `Using ${leds.count} LEDs, set manually.`;
    if (leds.probe === 'present' && leds.estimate > 0) return `Using about ${leds.count} LEDs, as detected.`;
    if (leds.probe === 'present') return `LEDs found, but their number could not be measured. Using ${leds.count}.`;
    if (leds.probe === 'absent') return `No LEDs detected. Using ${leds.count}.`;
    return `Checking for LEDs. Using ${leds.count}.`;
  }

  function syncInput() {
    $('count').disabled = $('auto').checked;
  }

  /* A form takes no edits while it is saved, and what was typed before the first reading came is
     not replaced by it. */
  let ledsTyped = false;
  let saverTyped = false;
  $('form').addEventListener('input', () => { ledsTyped = true; });
  $('saver-form').addEventListener('input', () => { saverTyped = true; });

  function showState(state) {
    $('current').textContent = describe(state.leds);
    if (ledsTyped) return;
    $('count').max = state.leds.max;
    $('count').value = state.leds.count;
    $('manual').checked = state.leds.setting !== 0;
    $('auto').checked = state.leds.setting === 0;
    syncInput();
  }

  $('auto').addEventListener('change', syncInput);
  $('manual').addEventListener('change', syncInput);

  $('form').addEventListener('submit', async (event) => {
    event.preventDefault();
    report('status', '', false);
    if ($('manual').checked && !$('count').reportValidity()) return;
    const count = $('manual').checked ? Number($('count').value) : 0;
    $('form').inert = true;
    try {
      const saved = await request('/api/leds', { count });
      ledsTyped = false;
      showState(saved);
      report('status', 'Saved.', false);
    } catch (error) {
      report('status', `Could not save: ${error.message}`, true);
    } finally {
      $('form').inert = false;
    }
  });

  /* Screensaver */
  function syncSaverInputs() {
    const type = $('saver').value;
    $('saver-delay').disabled = type === 'none';
    // Black Screen turns the backlight off, so it has no level to set.
    $('saver-brightness').disabled = type === 'none' || type === 'black-screen';
  }

  function showSaver(display) {
    if (saverTyped) return;
    $('saver').value = display.saver;
    $('saver-delay').value = display.delay;
    $('screen-brightness').value = display.screen_brightness;
    $('saver-brightness').value = display.saver_brightness;
    syncSaverInputs();
  }

  $('saver').addEventListener('change', syncSaverInputs);

  $('saver-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    report('saver-status', '', false);
    $('saver-form').inert = true;
    try {
      const saved = await request('/api/display', {
        saver: $('saver').value,
        delay: Number($('saver-delay').value),
        screen_brightness: Number($('screen-brightness').value),
        saver_brightness: Number($('saver-brightness').value),
      });
      saverTyped = false;
      showSaver(saved);
      report('saver-status', 'Saved.', false);
    } catch (error) {
      report('saver-status', `Could not save: ${error.message}`, true);
    } finally {
      $('saver-form').inert = false;
    }
  });

  request('/api/state').then(showState).catch((error) => {
    $('current').textContent = '';
    report('status', `Could not read the garland state: ${error.message}`, true);
  });
  // Closed until the garland has said what it saved: a form left at its defaults would save them back.
  $('saver-form').inert = true;
  request('/api/display').then((display) => {
    showSaver(display);
    $('saver-form').inert = false;
  }).catch((error) => report('saver-status', error.message, true));
</script>
)html";

}  // namespace garland
