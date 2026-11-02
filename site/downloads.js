/*
  Copyright (c) 2026 Anton Bespalov. All rights reserved.
  Licensed under the terms specified by the MIT License
  Part of the csiGarland project https://github.com/ABespalov/csigarland

  Purpose: Draws the downloads from downloads/manifest.json, inventing no link it does not hold
  Contributors: None
*/

'use strict';

function element(tag, className, text) {
  const made = document.createElement(tag);
  if (className) made.className = className;
  if (text !== undefined) made.textContent = text;
  return made;
}

function artifactCard(artifact) {
  const card = element('div', 'download' + (artifact.available ? '' : ' unavailable'));
  card.append(element('h3', '', artifact.name));
  const meta = [artifact.platform, artifact.version, artifact.size].filter(Boolean).join(' · ');
  card.append(element('p', 'meta', artifact.available ? meta : `${artifact.platform}: not yet`));
  // Only an https link the manifest names: an artifact without one is shown, not linked.
  if (artifact.available && typeof artifact.url === 'string' && artifact.url.startsWith('https://')) {
    const link = element('a', 'button button-primary', 'Download');
    link.href = artifact.url;
    link.rel = 'noopener noreferrer';
    card.append(link);
  }
  if (artifact.available && artifact.sha256) card.append(element('p', 'hash', `SHA-256 ${artifact.sha256}`));
  return card;
}

async function showDownloads() {
  const target = document.getElementById('downloads');
  try {
    const response = await fetch('downloads/manifest.json', { cache: 'no-store' });
    if (!response.ok) throw new Error(response.statusText);
    const manifest = await response.json();
    target.replaceChildren(...manifest.artifacts.map(artifactCard));
    document.getElementById('downloads-updated').textContent = manifest.updated ? `Updated ${manifest.updated}.` : '';
  } catch (error) {
    const fallback = element('p', '', 'The list could not be read. Every file is on ');
    const link = element('a', '', 'GitHub Releases');
    link.href = 'https://github.com/ABespalov/csigarland/releases';
    fallback.append(link, '.');
    target.replaceChildren(fallback);
  }
}

showDownloads();
