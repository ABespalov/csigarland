/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's Device page: the LED count, the power budget and the screen
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

<section aria-labelledby="power-title">
  <h2 id="power-title">Power</h2>
  <p class="m">A frame that would draw more than the limit is dimmed as a whole. The limit is what the supply gives beyond <span id="board">250</span> mA for the board, the screen and Wi-Fi, less the margin, a share of that.</p>
  <p class="e" id="power-invalid" role="alert" hidden>The power settings saved could not be used, so the garland runs on the most cautious ones: the smallest supply and the largest margin. Set your own below and save them to fix it.</p>
  <form id="power-form">
    <label for="supply" class="h">Power supply, mA</label>
    <input type="number" id="supply" step="1" inputmode="numeric" required>
    <label for="white" class="h g">One LED at full white, mA, without its idle draw</label>
    <input type="number" id="white" step="1" inputmode="numeric" required>
    <label for="idle" class="h g">One LED dark, mA</label>
    <input type="number" id="idle" step="1" inputmode="numeric" required>
    <label for="margin" class="h g">Margin, % of the supply beyond the board</label>
    <input type="number" id="margin" step="1" inputmode="numeric" required>
    <p class="m g" id="budget" aria-live="polite"></p>
    <button data-g="save" type="submit" id="power-save" class="g">Save</button>
  </form>
  <p class="s" id="power-status" role="status"></p>
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
      <option value="scheduler24" id="saver-scheduler24">Scheduler24, what comes next</option>
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
      /* What the power settings come to depends on the count: read again, its failure said on the power card. */
      loadPower().catch((error) => report('power-status', `Could not read the power settings again: ${error.message}`, true));
    } catch (error) {
      report('status', `Could not save: ${error.message}`, true);
    } finally {
      $('form').inert = false;
    }
  });

  /* Power: what the form would come to, worked out as the garland does, while it is typed. */
  let power = null;
  let powerTyped = false;
  const powerFields = { supply: 'supply_ma', white: 'led_white_ma', idle: 'led_idle_ma', margin: 'margin_percent' };

  function budgetText() {
    const [supply, white, idle, margin] = Object.keys(powerFields).map((id) => $(id).valueAsNumber);
    if (!power || ![supply, white, idle, margin].every(Number.isInteger)) return '';
    const limit = Math.floor(Math.max(0, supply - power.board_ma) * (100 - margin) / 100);
    const dark = power.leds * idle;
    if (dark > limit) return `Limit ${limit} mA. ${power.leds} LEDs draw ${dark} mA dark alone, over the limit, which no dimming takes away, so the LEDs are kept off: fewer LEDs, a lower idle draw, a bigger supply or a smaller margin lets them on.`;
    const peak = power.leds * (white + idle);
    const held = peak > limit ? ', so it is dimmed to the limit' : ', under the limit';
    return `Limit ${limit} mA. ${power.leds} LEDs at full white draw ${peak} mA${held}. Full white stays undimmed on up to ${Math.floor(limit / (white + idle))} LEDs.`;
  }

  function showBudget() {
    $('budget').textContent = budgetText();
  }

  function showPower(answer) {
    power = answer;
    $('board').textContent = answer.board_ma;
    $('power-invalid').hidden = answer.invalid_record !== true;
    const ranges = { supply: ['supply_min', 'supply_max'], white: ['led_white_min', 'led_white_max'], idle: [null, 'led_idle_max'], margin: ['margin_min', 'margin_max'] };
    for (const [id, [low, high]] of Object.entries(ranges)) {
      $(id).min = low ? answer[low] : 0;
      $(id).max = answer[high];
      if (!powerTyped) $(id).value = answer[powerFields[id]];
    }
    showBudget();
  }

  function loadPower() {
    return request('/api/leds/power').then(showPower);
  }

  $('power-form').addEventListener('input', () => { powerTyped = true; showBudget(); });

  $('power-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    report('power-status', '', false);
    const body = {};
    for (const [id, name] of Object.entries(powerFields)) body[name] = $(id).valueAsNumber;
    $('power-form').inert = true;
    try {
      const saved = await request('/api/leds/power', body);
      powerTyped = false;
      showPower(saved);
      report('power-status', 'Saved.', false);
    } catch (error) {
      report('power-status', `Could not save: ${error.message}`, true);
    } finally {
      $('power-form').inert = false;
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
    // Scheduler24 only with the clock module, unless it is the one chosen already.
    const scheduler = $('saver-scheduler24');
    scheduler.disabled = display.clock_module === false && display.saver !== 'scheduler24';
    scheduler.textContent = scheduler.disabled ? 'Scheduler24, with the clock module only' : 'Scheduler24, what comes next';
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
  $('power-form').inert = true;
  loadPower().then(() => { $('power-form').inert = false; }).catch((error) => report('power-status', error.message, true));
  $('saver-form').inert = true;
  request('/api/display').then((display) => {
    showSaver(display);
    $('saver-form').inert = false;
  }).catch((error) => report('saver-status', error.message, true));
</script>
)html";

}  // namespace garland
