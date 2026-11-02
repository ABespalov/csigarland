/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's page for pairing a browser with the panel's buttons
 * Contributors: None
 */

#pragma once

namespace garland {

inline constexpr char kPagePair[] = R"html(
<section aria-labelledby="pair-title">
  <h2 id="pair-title">Allow this browser</h2>
  <p class="m" id="pair-hint">The garland has to be asked from its own panel once. Access then lasts 100 days on this browser.</p>
  <p class="y" id="pair-code" aria-live="polite" hidden></p>
  <button data-g="key" type="button" id="pair-start">Ask for access</button>
  <p class="s" id="pair-status" role="status"></p>
  <p class="m g">Browser warning about this site? <a href="/csigarland-root.crt" download>Download the garland's root certificate</a> and install it: the Network page says how, after pairing.</p>
</section>
<script>
  const PAIR_POLL_MS = 1500;

  function pairLabel() {
    const agent = navigator.userAgent;
    const browser = /Edg\//.test(agent) ? 'Edge' : /Firefox\//.test(agent) ? 'Firefox' : /Chrome\//.test(agent) ? 'Chrome' : /Safari\//.test(agent) ? 'Safari' : 'Browser';
    const system = /Android/.test(agent) ? 'Android' : /iPhone|iPad/.test(agent) ? 'iOS' : /Windows/.test(agent) ? 'Windows' : /Mac OS/.test(agent) ? 'macOS' : /Linux/.test(agent) ? 'Linux' : '';
    return `${browser} ${system}`.trim();
  }

  // Only a path on this garland, whatever the address says.
  function pairDestination() {
    try {
      // As the browser would read it, which takes /\ for //.
      const back = new URL(new URLSearchParams(location.search).get('back') || '/', location.origin);
      return back.origin === location.origin ? back.pathname + back.search + back.hash : '/';
    } catch (error) {
      return '/';
    }
  }

  function pairEnded(text) {
    $('pair-code').hidden = true;
    $('pair-start').hidden = false;
    $('pair-hint').hidden = false;
    report('pair-status', text, true);
  }

  // The button says why it does nothing, and comes back by itself.
  function pairHoldOff(text, seconds) {
    const button = $('pair-start');
    if (seconds <= 0) {
      button.disabled = false;
      report('pair-status', '', false);
      return;
    }
    button.disabled = true;
    report('pair-status', `${text} You can ask again in ${seconds} s.`, true);
    setTimeout(() => pairHoldOff(text, seconds - 1), 1000);
  }

  async function pairWait(id) {
    try {
      const answer = await request('/api/auth/wait', { request: id });
      if (answer.state === 'approved') {
        location.replace(pairDestination());
      } else if (answer.state === 'waiting') {
        setTimeout(() => pairWait(id), PAIR_POLL_MS);
      } else {
        pairEnded('The request ended without an answer. Ask again.');
      }
    } catch (error) {
      pairEnded(error.message);
    }
  }

  $('pair-start').addEventListener('click', async () => {
    report('pair-status', '', false);
    try {
      const asked = await request('/api/auth/request', { label: pairLabel() });
      $('pair-code').textContent = asked.code;
      $('pair-code').hidden = false;
      $('pair-start').hidden = true;
      $('pair-hint').hidden = true;
      report('pair-status', `If the garland's screen shows this same code, press POWER and SPEED together there, within ${asked.seconds} s. If it shows another code, someone else is asking: press any one button to refuse.`, false);
      pairWait(asked.request);
    } catch (error) {
      if (error.retry) {
        pairHoldOff(error.message, error.retry);
      } else {
        report('pair-status', error.message, true);
      }
    }
  });
</script>
)html";

}  // namespace garland
