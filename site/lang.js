/*
  Copyright (c) 2026 Anton Bespalov. All rights reserved.
  Licensed under the terms specified by the MIT License
  Part of the csiGarland project https://github.com/ABespalov/csigarland

  Purpose: Sends a visitor of an English page to its Russian twin under ru/ when they chose Russian,
  or chose nothing and their browser prefers Russian to English; remembers the toggle's choice
  Contributors: None
*/

'use strict';

(function () {
  const KEY = 'csigarland-lang';

  function saved() {
    try {
      const value = localStorage.getItem(KEY);
      return value === 'ru' || value === 'en' ? value : null;
    } catch (_) {
      return null;
    }
  }

  function prefersRussian() {
    const list = Array.isArray(navigator.languages) && navigator.languages.length > 0
      ? navigator.languages
      : [navigator.language || ''];
    for (const tag of list) {
      const primary = String(tag || '').toLowerCase().split(/[-_]/)[0];
      if (primary === 'ru') return true;
      if (primary === 'en') return false;
    }
    return false;
  }

  // True once it is kept: storage may be refused, as in a private window.
  function remember(value) {
    try {
      localStorage.setItem(KEY, value);
      return localStorage.getItem(KEY) === value;
    } catch (_) {
      return false;
    }
  }

  // A link with the choice in it, which the page it leads to takes from there.
  function withChoice(link, value) {
    const url = new URL(link.getAttribute('href'), location.href);
    url.searchParams.set('lang', value);
    link.setAttribute('href', url.pathname + url.search + url.hash);
  }

  // In the toggle's own address, so that one opened in a new tab carries it too.
  function markToggle() {
    const toggle = document.getElementById('lang-switch');
    if (toggle) withChoice(toggle, toggle.getAttribute('hreflang') === 'ru' ? 'ru' : 'en');
  }

  // Once the page's links are there, however late this runs.
  function whenReady(work) {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', work);
    } else {
      work();
    }
  }

  document.addEventListener('click', (event) => {
    const toggle = event.target instanceof Element ? event.target.closest('#lang-switch') : null;
    if (toggle) remember(toggle.getAttribute('hreflang') === 'ru' ? 'ru' : 'en');
  });

  const params = new URLSearchParams(location.search);
  const asked = params.get('lang');
  if (asked === 'ru' || asked === 'en') {
    if (remember(asked)) {
      params.delete('lang');
      const rest = params.toString();
      history.replaceState(null, '', location.pathname + (rest ? '?' + rest : '') + location.hash);
    } else {
      // Not kept, so every link of this site on the page carries it on, the way it came.
      whenReady(() => {
        for (const link of document.querySelectorAll('a[href]')) {
          const href = link.getAttribute('href');
          if (link.id !== 'lang-switch' && href && !/^([a-z][a-z0-9+.-]*:|\/\/|#)/i.test(href)) withChoice(link, asked);
        }
      });
    }
  }
  whenReady(markToggle);

  // A Russian page stays one, as whoever reached it by its address asked for it.
  if (document.documentElement.lang !== 'en') return;
  const choice = asked === 'ru' || asked === 'en' ? asked : saved();
  if (choice === 'en' || (choice === null && !prefersRussian())) return;
  const page = location.pathname.split('/').pop();
  location.replace('ru/' + (page === 'index.html' ? '' : page) + location.search + location.hash);
})();
