/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's Network page: Wi-Fi and the mDNS name
 * Contributors: None
 */

#pragma once

namespace garland {

inline constexpr char kPageNetwork[] = R"html(
<section aria-labelledby="wifi-title">
  <h2 id="wifi-title">Wi-Fi</h2>
  <p class="m" id="wifi-current" aria-live="polite">Loading...</p>
  <button data-g="search" type="button" class="q" id="scan">Find networks</button>
  <ul id="networks" class="g"></ul>
  <form id="wifi-form">
    <label for="ssid" class="h g">Network name</label>
    <input type="text" id="ssid" maxlength="32" autocomplete="off" autocapitalize="none" required>
    <label for="password" class="h">Password</label>
    <input type="password" id="password" maxlength="63" autocomplete="off">
    <div class="r">
      <button data-g="wifi" type="submit" id="connect">Connect</button>
      <button data-g="unlink" type="button" class="z" id="forget">Forget</button>
    </div>
  </form>
  <p class="s" id="wifi-status" role="status"></p>
</section>
<section aria-labelledby="name-title">
  <h2 id="name-title">mDNS name</h2>
  <p class="m" id="name-current"></p>
  <form id="name-form">
    <label for="name" class="h">Name, without .local</label>
    <input type="text" id="name" maxlength="32" pattern="[a-z0-9]([a-z0-9\-]*[a-z0-9])?" title="Lowercase letters, digits and hyphens, with no hyphen at either end" autocomplete="off" autocapitalize="none" spellcheck="false" required>
    <button data-g="save" type="submit" id="name-save">Save name</button>
  </form>
  <p class="s" id="name-status" role="status"></p>
</section>
<section aria-labelledby="cert-title">
  <h2 id="cert-title">HTTPS certificate</h2>
  <p class="m">This garland is its own small certificate authority. Install its root once, and browsers on that device trust the portal without a warning. The root can vouch only for .local names, the setup network's address and the one domain or address below, for nothing else on the internet.</p>
  <p class="m" id="cert-state"></p>
  <a class="b g" href="/csigarland-root.crt" download>Download the root certificate</a>
  <details class="g">
    <summary>How to install it</summary>
    <ul class="m l">
      <li><b>First</b> compare the certificate's SHA-256 fingerprint, not the file's, with the one above: <code>certutil -dump</code> on Windows shows it as Cert Hash(sha256), <code>openssl x509 -noout -fingerprint -sha256 -in</code> elsewhere.</li>
      <li><b>Windows:</b> open the file, Install Certificate, Current User, Place all in: Trusted Root Certification Authorities.</li>
      <li><b>macOS:</b> open the file, add it to the login keychain, open it there, Trust, When using this certificate: Always Trust.</li>
      <li><b>iPhone, iPad:</b> open the file, Settings, Profile Downloaded, Install. Then General, About, Certificate Trust Settings: turn it on.</li>
      <li><b>Android:</b> Settings, Security, Encryption and credentials, Install a certificate, CA certificate.</li>
      <li><b>Firefox</b> keeps its own list: Settings, Privacy and Security, View Certificates, Authorities, Import.</li>
      <li>Then restart the browser.</li>
    </ul>
  </details>
  <form id="domain-form" class="g">
    <label for="domain" class="h">Your domain or fixed IP address, if the garland is reached by one</label>
    <input type="text" id="domain" maxlength="63" placeholder="garland.example.com or 192.168.1.50" autocomplete="off" autocapitalize="none" spellcheck="false">
    <button data-g="save" type="submit" id="domain-save">Save and restart</button>
  </form>
  <p class="m g">One of them. The keys stay. A change means new certificates on them, and the root has to be installed again on every device. An address only suits a garland that keeps it: reserve it on your router.</p>
  <p class="s" id="domain-status" role="status"></p>
</section>
<script>
  /* Wi-Fi */
  function describeWifi(wifi) {
    if (wifi.connected) return `Connected to ${wifi.ssid}. Open https://${wifi.hostname}. Its address, ${wifi.ip}, works too, with a certificate warning unless it is the fixed address set below.`
      + (wifi.setup_network ? ' The setup network turns off in about a minute.' : '');
    if (wifi.configured) return `Connecting to ${wifi.ssid}...`;
    return 'Not connected to a home network.';
  }

  function showWifi(wifi) {
    $('wifi-current').textContent = describeWifi(wifi);
    $('forget').disabled = !wifi.configured;
    $('name-current').textContent = `The portal answers at https://${wifi.hostname} on the home network.`;
    if (!$('name').value) $('name').value = wifi.hostname.replace(/\.local$/, '');
    if (wifi.configured && !$('ssid').value) $('ssid').value = wifi.ssid;
  }

  let wifiPoll = 0;
  function watchWifi() {
    clearInterval(wifiPoll);
    let remaining = 15;
    wifiPoll = setInterval(async () => {
      try {
        const wifi = await request('/api/wifi');
        showWifi(wifi);
        if (wifi.connected || --remaining <= 0) clearInterval(wifiPoll);
      } catch (error) {
        clearInterval(wifiPoll);
      }
    }, 2000);
  }

  function signalLevel(rssi) {
    return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : 1;
  }

  function signalGlyph(level) {
    const NS = 'http://www.w3.org/2000/svg';
    const glyph = document.createElementNS(NS, 'svg');
    glyph.setAttribute('viewBox', '0 0 19 16');
    glyph.setAttribute('width', '19');
    glyph.setAttribute('height', '16');
    glyph.setAttribute('aria-hidden', 'true');
    for (let bar = 1; bar <= 4; bar += 1) {
      const rect = document.createElementNS(NS, 'rect');
      rect.setAttribute('x', String((bar - 1) * 5));
      rect.setAttribute('y', String(16 - bar * 4));
      rect.setAttribute('width', '4');
      rect.setAttribute('height', String(bar * 4));
      rect.setAttribute('rx', '1');
      rect.setAttribute('fill', 'currentColor');
      rect.setAttribute('opacity', bar <= level ? '1' : '0.25');
      glyph.append(rect);
    }
    return glyph;
  }

  function showNetworks(networks) {
    $('networks').replaceChildren(...networks.map((network) => {
      const row = document.createElement('li');
      const pick = document.createElement('button');
      pick.title = 'Use this network';
      const level = signalLevel(network.rssi);
      pick.type = 'button';
      pick.className = 'q k';
      pick.title = `${network.rssi} dBm`;
      pick.setAttribute('aria-label', `${network.ssid}, signal ${level} of 4${network.open ? ', open' : ''}`);
      const name = document.createElement('span');
      name.textContent = network.open ? `${network.ssid} (open)` : network.ssid;
      pick.append(name, signalGlyph(level));
      pick.addEventListener('click', () => {
        $('ssid').value = network.ssid;
        $('password').value = '';
        $('password').focus();
      });
      row.append(pick);
      return row;
    }));
  }

  const SCAN_POLL_MS = 1000;
  const SCAN_GIVE_UP_MS = 30000;

  // The garland searches in the background and keeps answering meanwhile.
  $('scan').addEventListener('click', async () => {
    $('scan').disabled = true;
    report('wifi-status', 'Searching, this takes a few seconds...', false);
    try {
      const deadline = Date.now() + SCAN_GIVE_UP_MS;
      let data = await request('/api/wifi/scan', {});
      while (data.state === 'running') {
        if (Date.now() > deadline) throw new Error('no answer from the garland');
        await new Promise((resolve) => setTimeout(resolve, SCAN_POLL_MS));
        data = await request('/api/wifi/scan');
      }
      if (data.state === 'failed') throw new Error('the radio stayed busy, try again');
      showNetworks(data.networks);
      report('wifi-status', data.networks.length ? 'Choose a network, or type its name.' : 'No networks found. You can still type the name.', false);
    } catch (error) {
      report('wifi-status', `Search failed: ${error.message}`, true);
    } finally {
      $('scan').disabled = false;
    }
  });

  $('wifi-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    $('connect').disabled = true;
    report('wifi-status', '', false);
    try {
      showWifi(await request('/api/wifi', { ssid: $('ssid').value, password: $('password').value }));
      $('password').value = '';
      watchWifi();
    } catch (error) {
      report('wifi-status', `Could not connect: ${error.message}`, true);
    } finally {
      $('connect').disabled = false;
    }
  });

  $('forget').addEventListener('click', async () => {
    report('wifi-status', '', false);
    try {
      clearInterval(wifiPoll);
      showWifi(await request('/api/wifi', { ssid: '' }));
      $('ssid').value = '';
      report('wifi-status', 'Forgotten. The setup network stays on.', false);
    } catch (error) {
      report('wifi-status', `Could not forget: ${error.message}`, true);
    }
  });

  $('name').addEventListener('input', () => { $('name').value = $('name').value.toLowerCase(); });

  $('name-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    report('name-status', '', false);
    try {
      const answer = await request('/api/network/name', { name: $('name').value });
      report('name-status', answer.restarting
        ? `Saved. The garland is restarting with a certificate for this name: open https://${$('name').value}.local in a moment. There is no need to install the root again.`
        : 'That is its name already.', false);
    } catch (error) {
      report('name-status', `Could not save: ${error.message}`, true);
    }
  });

  function showCertificate(info) {
    if (!info.root_fingerprint) return;
    $('cert-state').textContent = `Portal names: ${info.certificate_names}. Root SHA-256: ${info.root_fingerprint}.`;
    $('domain').value = info.certificate_domain;
  }

  $('domain').addEventListener('input', () => { $('domain').value = $('domain').value.toLowerCase().trim(); });

  $('domain-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    report('domain-status', '', false);
    try {
      await request('/api/network/certificate', { domain: $('domain').value });
      report('domain-status', 'Saved. The garland is restarting; download and install the root again afterwards.', false);
    } catch (error) {
      report('domain-status', `Could not save: ${error.message}`, true);
    }
  });

  request('/api/info').then(showCertificate).catch(() => {});
  request('/api/wifi').then(showWifi).catch((error) => report('wifi-status', error.message, true));
</script>
)html";

}  // namespace garland
