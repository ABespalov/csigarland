/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's Firmware page
 * Contributors: None
 */

#pragma once

namespace garland {

inline constexpr char kPageFirmware[] = R"html(
<section aria-labelledby="firmware-title">
  <h2 id="firmware-title">Firmware</h2>
  <p class="m" id="firmware-current" aria-live="polite">Loading...</p>
  <form id="firmware-form">
    <label for="firmware-file" class="h">Update file (.ota)</label>
    <input type="file" id="firmware-file" accept=".ota" required>
    <p class="m" id="firmware-contents" aria-live="polite">It may hold firmware, effects, or both. Only what it holds is installed.</p>
    <button data-g="upload" type="submit" id="firmware-install" class="g" disabled>Install</button>
  </form>
  <p class="s" id="firmware-status" role="status"></p>
</section>
<script>
  /* Firmware */
  /* Set at the first send: the page's own first look, answered after it, says nothing of that send. */
  let sending = false;

  // The seconds of its trial left, as the garland counts them.
  const trialLeft = (info) => (info.trial_left ? `within ${info.trial_left} s` : 'any moment now');

  function showFirmware(info) {
    $('firmware-current').textContent = `Running version ${info.version} with ${info.effects} built-in effects.`
      + (info.on_trial ? ` It confirms itself ${trialLeft(info)}, or goes back to the previous version.` : '');
  }

  /* What a file says it holds, from the layout in ota_logic.h. Only the garland checks the
     signature, when the file is installed. */
  async function readUpdateContents(file) {
    const kHeaderBytes = 148;
    const head = new DataView(await file.slice(0, kHeaderBytes).arrayBuffer());
    if (head.byteLength < kHeaderBytes || head.getUint32(0, false) !== 0x43474F54) {
      throw new Error('This is not a csiGarland update file.');
    }
    if (head.getUint32(4, true) !== 3) {
      throw new Error('This update file is in a format this firmware does not read.');
    }
    const packStart = kHeaderBytes + head.getUint32(8, true);
    const packBytes = head.getUint32(12, true);
    const imageBytes = head.getUint32(16, true);
    if (packStart + packBytes + imageBytes !== file.size || packBytes + imageBytes === 0) {
      throw new Error('This update file is incomplete.');
    }
    const contents = { firmware: null, effects: null, version: null, image: null, digest: null };
    if (packBytes > 0) {
      contents.digest = [...new Uint8Array(head.buffer, 20, 32)].map((byte) => byte.toString(16).padStart(2, '0')).join('');
    }
    if (imageBytes > 0) {
      const imageStart = packStart + packBytes;
      const desc = new DataView(await file.slice(imageStart + 32, imageStart + 208).arrayBuffer());
      contents.firmware = 'of unknown version';
      if (desc.byteLength === 176 && desc.getUint32(0, true) === 0xABCD5432) {
        const text = new TextDecoder().decode(new Uint8Array(desc.buffer, 16, 32));
        contents.firmware = text.split('\0')[0];
        contents.version = contents.firmware;
        contents.image = [...new Uint8Array(desc.buffer, 144, 32)].map((byte) => byte.toString(16).padStart(2, '0')).join('');
      }
    }
    if (packBytes > 0) {
      /* After the pack's header: a kind letter, then four NUL-ended strings. */
      const pack = new Uint8Array(await file.slice(packStart, packStart + packBytes).arrayBuffer());
      const known = new DataView(pack.buffer).getUint32(4, true) === 2;
      contents.effects = known ? 0 : 'a newer set of';
      for (let at = 12; known && at < pack.length;) {
        if (pack[at] !== 0x70) contents.effects++;
        at++;
        for (let ended = 0; ended < 4 && at < pack.length; at++) {
          if (pack[at] === 0) ended++;
        }
      }
    }
    return contents;
  }

  let pendingUpdate = null;
  let chosenFile = null;

  /* From the send to the last word on it, the file on its way cannot be swapped or sent twice. */
  function holdFirmwareForm(held) {
    $('firmware-file').disabled = held;
    $('firmware-install').disabled = held || pendingUpdate === null;
  }

  $('firmware-file').addEventListener('change', async () => {
    const file = $('firmware-file').files[0];
    chosenFile = file;
    pendingUpdate = null;
    $('firmware-install').disabled = true;
    glyph($('firmware-install'), 'upload', 'Install');
    report('firmware-status', '', false);
    if (!file) return;
    try {
      const contents = await readUpdateContents(file);
      /* Another choice came while this one was being read. */
      if (chosenFile !== file) return;
      pendingUpdate = { file, contents };
      const firmware = contents.firmware !== null;
      const effects = contents.effects !== null;
      const parts = [];
      if (firmware) parts.push(`firmware ${contents.firmware}`);
      if (effects) parts.push(`${contents.effects} effects`);
      $('firmware-contents').textContent = `This file holds ${parts.join(' and ')}.`
        + (firmware ? '' : ' The firmware stays as it is.')
        + (effects ? '' : ' The effects stay as they are.');
      glyph($('firmware-install'), 'upload', firmware && effects ? 'Install firmware and effects' : (firmware ? 'Install firmware' : 'Install effects'));
      $('firmware-install').disabled = false;
    } catch (error) {
      if (chosenFile === file) $('firmware-contents').textContent = error.message;
    }
  });

  /* Over only when the garland has restarted, runs what was sent and has confirmed it: a first
     answer may come from before the restart, from a firmware still on trial, or from the old one
     after going back. A version may repeat, so the image says which one runs. */
  async function askForUpdateState() {
    const timeout = new AbortController();
    const timer = setTimeout(() => timeout.abort(), 5000);
    try {
      return await request('/api/ota', undefined, timeout.signal);
    } catch (error) {
      return null;
    } finally {
      clearTimeout(timer);
    }
  }

  /* One question at a time, each with its own limit, so no late answer can follow the last word
     and a question never answered cannot outlast the wait. */
  /* unknown: the upload ended with no answer, so the garland may have taken the whole file or not.
     One that took it restarts in moments, so the same boot 15 s on says it did not. bootBefore is the
     boot this send's own look found, never one a slower answer to another look left, and packBefore and
     imageBefore the effects and firmware it found running. */
  async function waitForRestart(expected, unknown, bootBefore, packBefore, imageBefore) {
    const deadline = Date.now() + 720000;
    const settled = Date.now() + 15000;
    const finish = (text, isError) => {
      report('firmware-status', text, isError);
      holdFirmwareForm(false);
    };
    while (true) {
      await new Promise((resolve) => setTimeout(resolve, 3000));
      const info = await askForUpdateState();
      const late = Date.now() > deadline;
      if (info === null) {
        if (late) return finish('The garland did not come back. Check its screen.', true);
        continue;
      }
      if (info.boot === bootBefore) {
        if (unknown && Date.now() > settled) return finish('Not installed: the connection was lost, and the garland runs as before.', true);
        if (late) return finish('The garland did not restart. Check its screen.', true);
        continue;
      }
      showFirmware(info);
      /* With the send's answer lost, a restart alone says nothing: the garland may have restarted for
         another reason, or another update put the same bytes there. Only a trial of this file's image says
         it took it, and for effects alone, effects other than those before now running as the file's, with
         the firmware as it was: another firmware with them may be another update's;
         anything else is told as seen. */
      if (unknown) {
        const tookImage = expected.image !== null && info.on_trial && info.image === expected.image;
        const tookEffects = expected.image === null && expected.digest !== null && packBefore !== expected.digest && info.pack_digest === expected.digest && info.image === imageBefore;
        if (tookImage || tookEffects) {
          unknown = false;
        } else {
          return finish(`The garland restarted and runs ${info.version}${info.on_trial ? ', on trial' : ''}. The send got no answer, so whether it took this file cannot be told from here: check its Info page, and send it again if need be.`, true);
        }
      }
      if (expected.image !== null && info.image !== expected.image) {
        return finish(`Not installed: running ${info.version}, not the firmware in this file. It went back to the previous firmware.`, true);
      }
      if (expected.digest !== null && info.pack_digest !== expected.digest) {
        return finish('Not installed: the garland is not running the effects in this file.', true);
      }
      if (!info.on_trial) {
        if (expected.image !== null) location.assign('/info');
        return finish('Update finished and confirmed.', false);
      }
      if (late) return finish('Running the new firmware, but it has not confirmed itself yet.', true);
      report('firmware-status', `Running the new firmware, which confirms itself ${trialLeft(info)}...`, false);
    }
  }

  $('firmware-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    if (pendingUpdate === null || $('firmware-install').disabled) return;
    const { file, contents } = pendingUpdate;
    holdFirmwareForm(true);
    sending = true;
    report('firmware-status', 'Sending...', false);
    /* The restart to wait for is the one after this send, so the boot to compare is today's: the
       garland may have restarted since this page opened. */
    const before = await askForUpdateState();
    if (before === null) {
      report('firmware-status', 'Not sent: the garland did not answer. Try again.', true);
      holdFirmwareForm(false);
      return;
    }
    showFirmware(before);
    const upload = new XMLHttpRequest();
    upload.open('POST', '/api/ota');
    upload.setRequestHeader('Content-Type', 'application/octet-stream');
    /* A send that moves nothing for 30 s, or a garland that takes it all and then says nothing for a
       minute, is given up on, and what the garland then runs says whether it took the file. */
    const STALL_MS = 30000;
    const ANSWER_MS = 60000;
    let movedAt = Date.now();
    let sentAll = false;
    const watch = setInterval(() => {
      if (Date.now() - movedAt > (sentAll ? ANSWER_MS : STALL_MS)) upload.abort();
    }, 2000);
    upload.addEventListener('loadend', () => clearInterval(watch));
    upload.upload.addEventListener('progress', (progress) => {
      movedAt = Date.now();
      if (progress.lengthComputable) {
        sentAll = progress.loaded >= progress.total;
        report('firmware-status', `Sending ${Math.floor(progress.loaded * 100 / progress.total)}%...`, false);
      }
    });
    upload.addEventListener('abort', () => {
      report('firmware-status', 'The garland stopped answering. Watching whether it took the file...', false);
      waitForRestart(contents, true, before.boot, before.pack_digest, before.image);
    });
    upload.addEventListener('load', () => {
      if (upload.status === 200) {
        report('firmware-status', 'Installed. The garland is restarting...', false);
        waitForRestart(contents, false, before.boot, before.pack_digest, before.image);
      } else {
        report('firmware-status', `Not installed: ${upload.responseText || upload.statusText}`, true);
        holdFirmwareForm(false);
      }
    });
    /* No answer is not a refusal: the garland may have taken all of it and be restarting. Only
       what it then runs can say. */
    upload.addEventListener('error', () => {
      report('firmware-status', 'The connection was lost. Watching whether the garland took the file...', false);
      waitForRestart(contents, true, before.boot, before.pack_digest, before.image);
    });
    upload.send(file);
  });

  request('/api/ota').then((info) => { if (!sending) showFirmware(info); }).catch((error) => { if (!sending) report('firmware-status', error.message, true); });
</script>
)html";

}  // namespace garland
