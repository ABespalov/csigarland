/*
  Copyright (c) 2026 Anton Bespalov. All rights reserved.
  Licensed under the terms specified by the MIT License
  Part of the csiGarland project https://github.com/ABespalov/csigarland

  Purpose: The interactive picture preview, with zoom, pan and paging through a gallery
  Contributors: None
*/

'use strict';

// The pages themselves are in English or, under ru/, in Russian; only what this script draws is here.
const UI = {
  en: {
    'lightbox.zoomOut': 'Zoom out',
    'lightbox.zoomIn': 'Zoom in',
    'lightbox.fit': 'Fit',
    'lightbox.oneToOne': '1:1',
    'lightbox.prev': 'Previous',
    'lightbox.next': 'Next',
    'lightbox.close': 'Close'
  },
  ru: {
    'lightbox.zoomOut': 'Уменьшить',
    'lightbox.zoomIn': 'Увеличить',
    'lightbox.fit': 'Вписать',
    'lightbox.oneToOne': '1:1',
    'lightbox.prev': 'Предыдущая',
    'lightbox.next': 'Следующая',
    'lightbox.close': 'Закрыть'
  }
};

function uiText(key) {
  const table = document.documentElement.lang === 'ru' ? UI.ru : UI.en;
  return table[key] || UI.en[key] || '';
}

/* Interactive image preview (lightbox) with zoom and pan */
let lightbox = null;
let lbStage = null;
let lbImg = null;
let lbCaption = null;
let lbZoomText = null;
let lbBtnOut = null;
let lbBtnIn = null;
let lbBtnFit = null;
let lbBtnOne = null;
let lbBtnClose = null;
let lbBtnPrev = null;
let lbBtnNext = null;
let lbTrigger = null;
// What the open gallery took out of reach, to give back as it closes.
let lbInert = [];
let lbGroup = [];
let lbIndex = 0;

let natWidth = 0;
let natHeight = 0;
let currentZoom = 1;
let fitZoom = 1;
const MIN_ZOOM = 0.2;
const MAX_ZOOM = 6;

function updateLightboxLabels() {
  if (!lightbox) return;
  lbBtnOut.setAttribute('aria-label', uiText('lightbox.zoomOut'));
  lbBtnOut.title = uiText('lightbox.zoomOut');
  lbBtnIn.setAttribute('aria-label', uiText('lightbox.zoomIn'));
  lbBtnIn.title = uiText('lightbox.zoomIn');
  lbBtnFit.textContent = uiText('lightbox.fit');
  lbBtnOne.textContent = uiText('lightbox.oneToOne');
  lbBtnPrev.setAttribute('aria-label', uiText('lightbox.prev'));
  lbBtnPrev.title = uiText('lightbox.prev');
  lbBtnNext.setAttribute('aria-label', uiText('lightbox.next'));
  lbBtnNext.title = uiText('lightbox.next');
  lbBtnClose.setAttribute('aria-label', uiText('lightbox.close'));
  lbBtnClose.title = uiText('lightbox.close');
}

function makeButton(text, className, onClick) {
  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = className;
  btn.textContent = text;
  btn.addEventListener('click', onClick);
  return btn;
}

function ensureLightbox() {
  if (lightbox) return;

  lightbox = document.createElement('div');
  lightbox.className = 'lightbox';
  lightbox.hidden = true;
  lightbox.setAttribute('role', 'dialog');
  lightbox.setAttribute('aria-modal', 'true');

  const bar = document.createElement('div');
  bar.className = 'lightbox-bar';

  lbCaption = document.createElement('p');
  lbCaption.className = 'lightbox-caption';

  const controls = document.createElement('div');
  controls.className = 'lightbox-controls';

  lbBtnOut = makeButton('−', 'lightbox-btn', () => stepZoom(1 / 1.35));
  lbZoomText = document.createElement('span');
  lbZoomText.className = 'lightbox-zoom';
  lbBtnIn = makeButton('+', 'lightbox-btn', () => stepZoom(1.35));
  lbBtnFit = makeButton('Fit', 'lightbox-btn', () => zoomToFit());
  lbBtnOne = makeButton('1:1', 'lightbox-btn', () => setZoom(1));
  lbBtnClose = makeButton('✕', 'lightbox-btn', () => closeLightbox());
  lbBtnPrev = makeButton('‹', 'lightbox-btn', () => stepImage(-1));
  lbBtnNext = makeButton('›', 'lightbox-btn', () => stepImage(1));

  controls.append(lbBtnPrev, lbBtnNext, lbBtnOut, lbZoomText, lbBtnIn, lbBtnFit, lbBtnOne, lbBtnClose);
  bar.append(lbCaption, controls);

  lbStage = document.createElement('div');
  lbStage.className = 'lightbox-stage';

  const canvas = document.createElement('div');
  canvas.className = 'lightbox-canvas';

  lbImg = document.createElement('img');
  lbImg.className = 'lightbox-img';
  canvas.append(lbImg);
  lbStage.append(canvas);
  lightbox.append(bar, lbStage);
  document.body.append(lightbox);

  updateLightboxLabels();
  bindLightboxEvents(canvas);
}

function computeFitZoom() {
  if (!natWidth || !natHeight || !lbStage) return 1;
  const availW = Math.max(120, lbStage.clientWidth - 40);
  const availH = Math.max(120, lbStage.clientHeight - 40);
  const ratio = Math.min(availW / natWidth, availH / natHeight);
  // Small hardware screen captures (240x240) look crisp up to 1.5x; larger renders fit the screen.
  const cap = natWidth <= 320 ? 1.5 : 1.25;
  return Math.max(MIN_ZOOM, Math.min(ratio, cap));
}

function setZoom(nextZoom, focalX, focalY) {
  if (!natWidth || !natHeight) return;
  const clamped = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, nextZoom));

  const stageRect = lbStage.getBoundingClientRect();
  const imgRect = lbImg.getBoundingClientRect();
  const viewX = focalX !== undefined ? focalX : stageRect.left + stageRect.width / 2;
  const viewY = focalY !== undefined ? focalY : stageRect.top + stageRect.height / 2;
  const relX = imgRect.width > 0 ? Math.max(0, Math.min(1, (viewX - imgRect.left) / imgRect.width)) : 0.5;
  const relY = imgRect.height > 0 ? Math.max(0, Math.min(1, (viewY - imgRect.top) / imgRect.height)) : 0.5;

  currentZoom = clamped;
  lbImg.width = Math.max(1, Math.round(natWidth * currentZoom));
  lbImg.height = Math.max(1, Math.round(natHeight * currentZoom));
  lbZoomText.textContent = `${Math.round(currentZoom * 100)}%`;

  const nextImgRect = lbImg.getBoundingClientRect();
  lbStage.scrollLeft += (nextImgRect.left + relX * nextImgRect.width) - viewX;
  lbStage.scrollTop += (nextImgRect.top + relY * nextImgRect.height) - viewY;
}

function stepZoom(factor, focalX, focalY) {
  setZoom(currentZoom * factor, focalX, focalY);
}

function zoomToFit() {
  fitZoom = computeFitZoom();
  setZoom(fitZoom);
}

function bindLightboxEvents(canvas) {
  let dragging = false;
  let moved = false;
  let startX = 0;
  let startY = 0;
  let startScrollLeft = 0;
  let startScrollTop = 0;

  lbStage.addEventListener('mousedown', (event) => {
    if (event.button !== 0) return;
    dragging = true;
    moved = false;
    startX = event.clientX;
    startY = event.clientY;
    startScrollLeft = lbStage.scrollLeft;
    startScrollTop = lbStage.scrollTop;
    lbStage.classList.add('dragging');
    event.preventDefault();
  });

  window.addEventListener('mousemove', (event) => {
    if (!dragging) return;
    const dx = event.clientX - startX;
    const dy = event.clientY - startY;
    if (Math.abs(dx) > 4 || Math.abs(dy) > 4) moved = true;
    lbStage.scrollLeft = startScrollLeft - dx;
    lbStage.scrollTop = startScrollTop - dy;
  });

  window.addEventListener('mouseup', () => {
    if (!dragging) return;
    dragging = false;
    lbStage.classList.remove('dragging');
  });

  // Clicking the dark backdrop outside the image closes the preview.
  lbStage.addEventListener('click', (event) => {
    if (!moved && (event.target === lbStage || event.target === canvas)) {
      closeLightbox();
    }
  });

  // Double-click or double-tap toggles between Fit and zoomed-in detail view.
  lbImg.addEventListener('dblclick', (event) => {
    event.preventDefault();
    const targetZoom = Math.abs(currentZoom - fitZoom) < 0.08 ? Math.max(1, fitZoom * 2.2) : fitZoom;
    setZoom(targetZoom, event.clientX, event.clientY);
  });

  // Wheel zoom around cursor position.
  lbStage.addEventListener('wheel', (event) => {
    event.preventDefault();
    const factor = event.deltaY < 0 ? 1.18 : 1 / 1.18;
    stepZoom(factor, event.clientX, event.clientY);
  }, { passive: false });

  // Two-finger pinch-to-zoom on mobile touchscreens.
  let pinchStartDist = 0;
  let pinchStartZoom = 1;

  lbStage.addEventListener('touchstart', (event) => {
    if (event.touches.length === 2) {
      pinchStartDist = Math.hypot(
        event.touches[0].clientX - event.touches[1].clientX,
        event.touches[0].clientY - event.touches[1].clientY
      );
      pinchStartZoom = currentZoom;
    }
  }, { passive: true });

  lbStage.addEventListener('touchmove', (event) => {
    if (event.touches.length === 2 && pinchStartDist > 0) {
      event.preventDefault();
      const dist = Math.hypot(
        event.touches[0].clientX - event.touches[1].clientX,
        event.touches[0].clientY - event.touches[1].clientY
      );
      const midX = (event.touches[0].clientX + event.touches[1].clientX) / 2;
      const midY = (event.touches[0].clientY + event.touches[1].clientY) / 2;
      setZoom(pinchStartZoom * (dist / pinchStartDist), midX, midY);
    }
  }, { passive: false });

  // A one-finger swipe turns the page, but only while the picture fits and there is nothing to pan.
  let swipeX = 0;
  let swipeY = 0;
  let swiping = false;

  lbStage.addEventListener('touchstart', (event) => {
    swiping = event.touches.length === 1 && lbStage.scrollWidth <= lbStage.clientWidth + 1;
    if (swiping) {
      swipeX = event.touches[0].clientX;
      swipeY = event.touches[0].clientY;
    }
  }, { passive: true });

  lbStage.addEventListener('touchend', (event) => {
    if (event.touches.length < 2) pinchStartDist = 0;
    if (!swiping || event.changedTouches.length !== 1) return;
    swiping = false;
    const dx = event.changedTouches[0].clientX - swipeX;
    const dy = event.changedTouches[0].clientY - swipeY;
    if (Math.abs(dx) > 60 && Math.abs(dy) < 50) stepImage(dx < 0 ? 1 : -1);
  }, { passive: true });

  window.addEventListener('keydown', (event) => {
    if (!lightbox || lightbox.hidden) return;
    if (event.key === 'Escape') {
      event.preventDefault();
      closeLightbox();
    } else if (event.key === '+' || event.key === '=') {
      event.preventDefault();
      stepZoom(1.25);
    } else if (event.key === '-') {
      event.preventDefault();
      stepZoom(1 / 1.25);
    } else if (event.key === '0') {
      event.preventDefault();
      zoomToFit();
    } else if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
      event.preventDefault();
      stepImage(event.key === 'ArrowLeft' ? -1 : 1);
    } else if (event.key === 'Tab') {
      // Round the panel's own buttons, never out to what lies under it.
      const stops = Array.from(lightbox.querySelectorAll('button')).filter((button) => !button.hidden);
      const at = stops.indexOf(document.activeElement);
      event.preventDefault();
      stops[at < 0 ? 0 : (at + (event.shiftKey ? -1 : 1) + stops.length) % stops.length].focus();
    }
  });
}

function captionFor(img) {
  const fig = img.closest('figure');
  if (fig) {
    const cap = fig.querySelector('figcaption:not([data-hint])');
    if (cap && cap.textContent.trim()) return cap.textContent.trim();
  }
  return img.getAttribute('alt') || '';
}

// The pictures one can page through: those of the same gallery, or the picture alone.
function groupFor(img) {
  const gallery = img.closest('.gallery, .gallery-shots, .screens');
  return gallery ? Array.from(gallery.querySelectorAll('img')) : [img];
}

function stepImage(delta) {
  if (!lightbox || lightbox.hidden || lbGroup.length < 2) return;
  lbIndex = (lbIndex + delta + lbGroup.length) % lbGroup.length;
  showImage(lbGroup[lbIndex]);
}

function openLightbox(img) {
  ensureLightbox();
  lbTrigger = img;
  lbGroup = groupFor(img);
  lbIndex = Math.max(0, lbGroup.indexOf(img));
  lbBtnPrev.hidden = lbGroup.length < 2;
  lbBtnNext.hidden = lbGroup.length < 2;
  lightbox.hidden = false;
  // The page under the panel takes no focus or press while it is open, as a modal dialog's would not.
  lbInert = Array.from(document.body.children).filter((el) => el !== lightbox && !el.inert);
  for (const el of lbInert) el.inert = true;
  document.body.classList.add('lightbox-open');
  showImage(img);
  lbBtnClose.focus();
}

function showImage(img) {
  lbCaption.textContent = captionFor(img);

  const onReady = () => {
    natWidth = lbImg.naturalWidth || 1240;
    natHeight = lbImg.naturalHeight || 930;
    zoomToFit();
  };

  lbImg.onload = onReady;
  lbImg.src = img.currentSrc || img.src;
  lbImg.alt = img.alt || '';
  if (lbImg.complete && lbImg.naturalWidth > 0) {
    onReady();
  }
}

function closeLightbox() {
  if (!lightbox || lightbox.hidden) return;
  lightbox.hidden = true;
  for (const el of lbInert) el.inert = false;
  lbInert = [];
  document.body.classList.remove('lightbox-open');
  if (lbTrigger && typeof lbTrigger.focus === 'function') {
    lbTrigger.focus();
  }
}

function setupPreviewableImages() {
  for (const img of document.querySelectorAll('main img')) {
    img.classList.add('previewable');
    img.tabIndex = 0;
    img.setAttribute('role', 'button');
    img.addEventListener('click', () => openLightbox(img));
    img.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        openLightbox(img);
      }
    });
  }
}

setupPreviewableImages();
