/*
  Copyright (c) 2026 Anton Bespalov. All rights reserved.
  Licensed under the terms specified by the MIT License
  Part of the csiGarland project https://github.com/ABespalov/csigarland

  Purpose: Draws the downloads from downloads/manifest.json, inventing no link it does not hold
  Contributors: None
*/

'use strict';

const DOWNLOADS_RU = {
  products: {
    'firmware': {
      name: 'Обновление прошивки и пакета эффектов',
      about: 'Файл обновления для гирлянды с релизной прошивкой, подписанный корневым ключом проекта: устанавливается через вкладку Firmware веб-портала или из каталога в настольной утилите.'
    },
    'utility': {
      name: 'Утилита csiGarland',
      about: 'Программа для компьютера: управление гирляндой, настройка эффектов с живым предпросмотром, установка обновлений и просмотр системного лога.'
    },
    'root-certificate': {
      name: 'Корневой сертификат релиза',
      about: 'Для воспроизводимой пересборки релиза из исходников и побайтовой сверки.'
    }
  },
  platforms: {
    'Any': 'Любая платформа'
  },
  released: (day) => `выпуск ${day}`,
  notes: 'Описание изменений',
  download: 'Скачать',
  notYet: 'В разработке',
  updated: (day) => `Обновлено ${day}.`,
  macHelp: {
    summary: 'Как открыть утилиту на Mac',
    steps: [
      'Скачайте zip для своего Mac: Apple silicon — для M1 и новее, Intel — для остальных. Нужна macOS 12 или новее.',
      'Откройте zip: Finder распакует приложение csiGarland. Перетащите его в «Программы».',
      'Откройте csiGarland. Приложение не подписано сертификатом Apple Developer, поэтому macOS не даст открыть его сразу и сообщит, что не может проверить его на вредоносное ПО. Нажмите «Готово», не «Переместить в Корзину».',
      'Откройте «Системные настройки» → «Конфиденциальность и безопасность», прокрутите до раздела «Безопасность» и нажмите «Все равно открыть» рядом с csiGarland. Подтвердите паролем или Touch ID и ещё раз нажмите «Все равно открыть». Это нужно только при первом запуске.',
      'Когда утилита спросит, можно ли ей находить устройства в локальной сети, нажмите «Разрешить»: иначе она не увидит гирлянду.',
      'После обновления утилиты macOS один раз для каждой гирлянды спросит доступ к связке ключей, где лежит токен привязки: введите пароль входа и нажмите «Разрешать всегда».'
    ],
    terminal: 'Вместо шагов 3–4 можно снять карантин в Терминале:'
  },
  fallbackPrefix: 'Не удалось загрузить список файлов. Все сборки доступны в ',
  fallbackLink: 'GitHub Releases'
};

const MAC_HELP = {
  summary: 'How to open the utility on a Mac',
  steps: [
    'Download the zip for your Mac: Apple silicon for M1 and later, Intel for the rest. It needs macOS 12 or later.',
    'Open the zip: Finder unpacks the csiGarland app. Drag it to Applications.',
    'Open csiGarland. It is not signed with an Apple Developer ID, so macOS holds it back the first time, saying it cannot check it for malware. Press Done, not Move to Trash.',
    'Open System Settings, Privacy & Security, scroll down to Security and press Open Anyway beside csiGarland. Confirm with your password or Touch ID, and press Open Anyway again. This is needed only the first time.',
    'When it asks to find devices on your local network, press Allow: otherwise it finds no garland.',
    'After an update of the utility, macOS asks once for each garland whether it may read the pairing token from your keychain: type your login password and press Always Allow.'
  ],
  terminal: 'Instead of steps 3 and 4, the quarantine can be taken off in Terminal:'
};
const MAC_QUARANTINE = 'xattr -dr com.apple.quarantine /Applications/csiGarland.app';

let cachedManifest = null;
let loadFailed = false;

function isRussian() {
  return document.documentElement.lang === 'ru';
}

function element(tag, className, text) {
  const made = document.createElement(tag);
  if (className) made.className = className;
  if (text !== undefined) made.textContent = text;
  return made;
}

// Only an https link the manifest names: anything without one is shown, not linked.
function safeLink(url) {
  return typeof url === 'string' && url.startsWith('https://') ? url : null;
}

// Newest first: by the day it was released, then by its version, number by number.
function newerFirst(a, b) {
  const day = String(b.released || '').localeCompare(String(a.released || ''));
  if (day !== 0) return day;
  const parts = (build) => String(build.version || '').split(/[.-]/).map((part) => Number.parseInt(part, 10) || 0);
  const left = parts(a);
  const right = parts(b);
  for (let i = 0; i < Math.max(left.length, right.length); i++) {
    if ((right[i] || 0) !== (left[i] || 0)) return (right[i] || 0) - (left[i] || 0);
  }
  return 0;
}

// One release of a product for one platform: its version, when it was released, its notes, its file.
function releaseRow(build, ru) {
  const row = element('div', 'download');
  const releasedText = build.released
    ? (ru ? DOWNLOADS_RU.released(build.released) : `released ${build.released}`)
    : '';
  const meta = element('p', 'meta', [build.version, releasedText, build.size].filter(Boolean).join(' · '));
  const notes = safeLink(build.notes);
  if (notes) {
    const link = element('a', '', ru ? DOWNLOADS_RU.notes : 'Release notes');
    link.href = notes;
    link.rel = 'noopener noreferrer';
    meta.append(' · ', link);
  }
  row.append(meta);
  const url = safeLink(build.url);
  if (url) {
    const download = element('a', 'button button-primary', ru ? DOWNLOADS_RU.download : 'Download');
    download.href = url;
    download.rel = 'noopener noreferrer';
    row.append(download);
  }
  if (build.sha256) row.append(element('p', 'hash', `SHA-256 ${build.sha256}`));
  return row;
}

// A platform of a product, with its releases newest first, or "Not yet" when it has none.
function platformBlock(platform, builds, ru) {
  const released = builds.filter((build) => build.available).sort(newerFirst);
  const block = element('div', 'platform' + (released.length ? '' : ' unavailable'));
  const title = ru && DOWNLOADS_RU.platforms[platform] ? DOWNLOADS_RU.platforms[platform] : platform;
  block.append(element('h4', '', title));
  if (released.length === 0) {
    block.append(element('p', 'meta', ru ? DOWNLOADS_RU.notYet : 'Not yet'));
    return block;
  }
  block.append(...released.map((build) => releaseRow(build, ru)));
  return block;
}

// How to open a download on a Mac, folded, as the app is signed by no one Apple knows.
function macHelp(ru) {
  const help = ru ? DOWNLOADS_RU.macHelp : MAC_HELP;
  const details = element('details', 'howto');
  details.append(element('summary', '', help.summary));
  const steps = element('ol');
  steps.append(...help.steps.map((step) => element('li', '', step)));
  const terminal = element('p', '', help.terminal);
  const command = element('pre');
  command.append(element('code', '', MAC_QUARANTINE));
  details.append(steps, terminal, command);
  return details;
}

// A product, such as the utility, with a block for each platform it is, or will be, built for, in the
// order the manifest first names them.
function productCard(product, ru) {
  const card = element('section', 'product');
  const translated = ru && product.id && DOWNLOADS_RU.products[product.id];
  card.append(element('h3', '', translated ? translated.name : product.name));
  const about = translated ? translated.about : product.about;
  if (about) card.append(element('p', 'about', about));
  const platforms = new Map();
  for (const build of product.builds || []) {
    if (!platforms.has(build.platform)) platforms.set(build.platform, []);
    platforms.get(build.platform).push(build);
  }
  const blocks = element('div', 'builds');
  blocks.replaceChildren(...[...platforms].map(([platform, builds]) => platformBlock(platform, builds, ru)));
  card.append(blocks);
  const forMac = (product.builds || []).some((build) => build.available && String(build.platform).startsWith('macOS'));
  if (forMac) card.append(macHelp(ru));
  return card;
}

function renderDownloads() {
  const target = document.getElementById('downloads');
  if (!target) return;
  const ru = isRussian();
  if (loadFailed) {
    const fallback = element('p', '', ru ? DOWNLOADS_RU.fallbackPrefix : 'The list could not be read. Every file is on ');
    const link = element('a', '', ru ? DOWNLOADS_RU.fallbackLink : 'GitHub Releases');
    link.href = 'https://github.com/ABespalov/csigarland/releases';
    fallback.append(link, '.');
    target.replaceChildren(fallback);
    return;
  }
  if (!cachedManifest) return;
  target.replaceChildren(...cachedManifest.products.map((product) => productCard(product, ru)));
  const updated = document.getElementById('downloads-updated');
  if (updated) {
    updated.textContent = cachedManifest.updated
      ? (ru ? DOWNLOADS_RU.updated(cachedManifest.updated) : `Updated ${cachedManifest.updated}.`)
      : '';
  }
}

async function showDownloads() {
  try {
    // From the site's root, as the Russian page under ru/ reads the same list.
    const response = await fetch('/downloads/manifest.json', { cache: 'no-store' });
    if (!response.ok) throw new Error(response.statusText);
    const manifest = await response.json();
    if (!Array.isArray(manifest.products)) throw new Error('not a list of products');
    cachedManifest = manifest;
    loadFailed = false;
  } catch (_) {
    loadFailed = true;
  }
  renderDownloads();
}

showDownloads();
