/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's Info page
 * Contributors: None
 */

#pragma once

namespace garland {

inline constexpr char kPageInfo[] = R"html(
<section aria-labelledby="device-title">
  <h2 id="device-title">Device</h2>
  <dl id="info-device"></dl>
</section>
<section aria-labelledby="memory-title">
  <h2 id="memory-title">Memory</h2>
  <dl id="info-memory"></dl>
</section>
<section aria-labelledby="address-title">
  <h2 id="address-title">Network</h2>
  <dl id="info-address"></dl>
</section>
<section aria-labelledby="mode-title">
  <h2 id="mode-title">Effects and mode</h2>
  <dl id="info-mode"></dl>
</section>
<section aria-labelledby="next-title">
  <h2 id="next-title">Next scheduled action</h2>
  <p id="info-next"></p>
  <p class="s" id="info-status" role="status"></p>
</section>
<section aria-labelledby="browsers-title">
  <h2 id="browsers-title">Browsers with access</h2>
  <ul id="browsers"></ul>
  <p class="m g">Holding POWER and SPEED on the garland for 10 s forgets them all.</p>
  <p class="s" id="browsers-status" role="status"></p>
</section>
<script>
  const INFO_POLL_MS = 10000;

  function kilobytes(bytes) {
    return `${Math.round(bytes / 1024)} KB`;
  }

  function showInfo(info) {
    showFacts('info-device', [['Model', info.model], ['Chip', info.chip], ['MAC', info.mac]]);
    showFacts('info-memory', [
      ['RAM', `${kilobytes(info.ram_free)} free of ${kilobytes(info.ram_total)}`],
      ['RAM, lowest', `${kilobytes(info.ram_lowest)} free since the start`],
      ['Flash', `${kilobytes(info.flash_total)} in all`],
      ['Firmware', `${kilobytes(info.firmware_used)} of its ${kilobytes(info.firmware_slot)} slot, ${kilobytes(info.firmware_slot - info.firmware_used)} free`],
    ]);
    showFacts('info-address', [
      ['IP address', info.ip || 'Not connected to a home network'],
      ...(info.setup_network ? [['Setup network', '192.168.4.1']] : []),
      ['mDNS name', info.hostname],
      ...(info.https ? [] : [['HTTPS', 'Not working: the portal is on plain HTTP. Install a new firmware from the Firmware page.']]),
      ...(info.led_output ? [] : [['LED output', 'Not working: frames are not reaching all of the garland. Restart the garland if it stays so.']]),
      ...(info.root_fingerprint ? [['Root SHA-256', info.root_fingerprint], ['Portal certificate', `Until ${day(info.certificate_until)}`]] : []),
    ]);
    showFacts('info-mode', [['Effects', `${info.effects}, ${info.favorites} in favourites`], ...modeFacts(info)]);
    $('info-next').textContent = describeNext(info.next);
  }

  function day(epoch) {
    return epoch ? new Date(epoch * 1000).toLocaleDateString() : 'when the clock is set';
  }

  function showBrowsers(data) {
    $('browsers').replaceChildren(...data.devices.map((device) => {
      const row = document.createElement('li');
      row.className = 'a';
      const what = document.createElement('span');
      what.className = 'w';
      const name = document.createElement('span');
      name.className = 't';
      name.textContent = device.label + (device.this ? ' (this one)' : '');
      what.append(name, `Until ${day(device.expires)}`);
      const forget = glyphButton('unlink', 'Forget', `Forget ${device.label}`, 'z', async () => {
        try {
          const left = await request('/api/auth/forget', { id: device.id });
          showBrowsers(left);
          if (device.this) location.href = '/pair';
        } catch (error) {
          report('browsers-status', error.message, true);
        }
      });
      row.append(what, forget);
      return row;
    }));
  }

  async function loadInfo() {
    if (document.hidden) return;
    try {
      showInfo(await request('/api/info'));
      report('info-status', '', false);
    } catch (error) {
      report('info-status', error.message, true);
    }
  }

  loadInfo();
  request('/api/auth/devices').then(showBrowsers).catch((error) => report('browsers-status', error.message, true));
  setInterval(loadInfo, INFO_POLL_MS);
</script>
)html";

}  // namespace garland
