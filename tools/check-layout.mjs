// Nothing in the interface may overflow the panel it lives in.
//
//   node tools/check-layout.mjs <url> [chrome-binary]
//
// This exists because a pair of selectors overflowed the Action panel by
// 26 px and shipped: the interface is shared byte for byte by the plugin, the
// headless host and the browser build, so one CSS mistake is wrong in three
// places at once, and nothing was looking. Every control is measured against
// its panel's content box, for every instrument, at the design width.
//
// Node's built-in WebSocket drives Chrome directly; no Puppeteer, no install.
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import net from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const URL_ = process.argv[2];
const CHROME = process.argv[3] || process.env.CHROME_BIN ||
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
// Asked for, not assumed. A fixed debugging port is shared with every other
// browser on the machine -- and with a parallel CI job -- and connecting to
// somebody else's Chrome does not fail, it hangs, which is much worse.
async function freePort () {
  return new Promise ((resolve, reject) => {
    const srv = net.createServer();
    srv.on ('error', reject);
    srv.listen (0, '127.0.0.1', () => {
      const { port } = srv.address();
      srv.close (() => resolve (port));
    });
  });
}
const PORT = Number (process.env.EPI_CDP_PORT || 0) || await freePort();

if (!URL_) { console.error ('usage: check-layout.mjs <url> [chrome]'); process.exit (2); }

// Chrome is driven over the global WebSocket, which Node did not have until
// 22. Saying so beats "WebSocket is not defined" from somewhere in the middle
// of a workflow that has already built the whole site.
if (typeof WebSocket === 'undefined') {
  console.error (`  check-layout: this Node (${process.version}) has no global WebSocket; needs 22 or newer`);
  process.exit (2);
}

const profile = mkdtempSync (join (tmpdir(), 'epi-layout-'));
const chrome = spawn (CHROME, [
  '--headless=new', `--remote-debugging-port=${PORT}`, '--no-first-run',
  '--no-default-browser-check', '--disable-gpu', '--no-sandbox',
  `--user-data-dir=${profile}`, '--window-size=1224,880',
  '--autoplay-policy=no-user-gesture-required', 'about:blank',
], { stdio: 'ignore' });

const sleep = (ms) => new Promise ((r) => setTimeout (r, ms));
const step = (m) => { if (process.env.EPI_LAYOUT_DEBUG) console.error ('  ..' + m); };

// A hung browser must fail the build, not hang it.
const HARD_LIMIT = Number (process.env.EPI_LAYOUT_TIMEOUT || 90000);
const killer = setTimeout (() => {
  console.error (`  check-layout: gave up after ${HARD_LIMIT / 1000}s`);
  try { chrome.kill ('SIGKILL'); } catch {}
  process.exit (1);
}, HARD_LIMIT);
killer.unref?.();

async function targetUrl () {
  for (let i = 0; i < 80; i++) {
    try {
      const tabs = await (await fetch (`http://127.0.0.1:${PORT}/json`)).json();
      const page = tabs.find ((t) => t.type === 'page');
      if (page) return page.webSocketDebuggerUrl;
    } catch {}
    await sleep (250);
  }
  throw new Error (`Chrome never came up on port ${PORT} — is it in use by something else?`);
}

// Every control, against the content box of the panel that contains it, for
// every instrument. Returns the worst overflow and who caused it.
// Overflow is measured in LAYOUT pixels -- scrollWidth against clientWidth --
// not from getBoundingClientRect. The interface scales itself to the window
// with a CSS transform, and rects come back scaled, which puts a sub-pixel
// wobble on every comparison: the first version of this check reported a
// 0.9 px "overflow" on a control that fits exactly. Layout pixels are immune
// to the transform, so the threshold can be tight enough to mean something.
const PROBE = `
(async function () {
  const H = window.__EPI_HOST__;
  if (!H) return JSON.stringify ({ error: 'the host never initialised' });
  const o = document.getElementById ('epi-start-overlay'); if (o) o.remove();

  const names = ['Tine', 'E-Grand', 'Reed', 'Grand', 'Clav'];
  const findings = [];
  const seen = new Set();

  const describe = (el) => {
    const own = (el.querySelector ('.cyclabel') || el.querySelector ('.klabel') || {}).textContent;
    if (own) return own.trim();
    const cls = (el.className || '').toString().split (' ')[0];
    const kids = [].map.call (el.querySelectorAll ('.cyclabel'), (l) => l.textContent.trim());
    return kids.length ? cls + ' [' + kids.join (' + ') + ']' : cls || el.tagName.toLowerCase();
  };

  for (let inst = 0; inst < 5; inst++) {
    H.setNormalised ('instrument', inst / 4);
    await new Promise ((r) => setTimeout (r, 700));

    document.querySelectorAll ('#plugin .panel, #plugin .panel *').forEach ((el) => {
      if (el.offsetWidth === 0 && el.offsetHeight === 0) return;
      // A scroller is allowed to scroll; only things that cannot are bugs.
      const cs = getComputedStyle (el);
      if (cs.overflowX === 'auto' || cs.overflowX === 'scroll') return;

      const over = el.scrollWidth - el.clientWidth;
      if (over <= 1) return;

      const panel = el.closest ('.panel');
      const key = inst + '|' + describe (el) + '|' + over;
      if (seen.has (key)) return;
      seen.add (key);
      findings.push ({
        instrument: names[inst],
        panel: ((panel && panel.querySelector ('.phead')) || {}).textContent
                 ? panel.querySelector ('.phead').textContent.slice (0, 16) : '?',
        control: describe (el).slice (0, 30),
        overflowPx: over,
      });
    });
  }

  const bodyOverflow = document.documentElement.scrollWidth - document.documentElement.clientWidth;
  return JSON.stringify ({ findings, bodyOverflow, panels: document.querySelectorAll ('.panel').length });
})()`;

let ws, nextId = 0;
const pending = new Map();

function send (method, params = {}) {
  const id = ++nextId;
  return new Promise ((resolve, reject) => {
    pending.set (id, { resolve, reject });
    ws.send (JSON.stringify ({ id, method, params }));
  });
}

try {
  step ('waiting for chrome');
  const wsUrl = await targetUrl();
  step ('connected');
  ws = new WebSocket (wsUrl);
  await new Promise ((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  ws.onmessage = (e) => {
    const m = JSON.parse (e.data);
    const p = pending.get (m.id);
    if (p) { pending.delete (m.id); m.error ? p.reject (new Error (m.error.message)) : p.resolve (m.result); }
  };

  await send ('Runtime.enable');
  await send ('Page.enable');
  await send ('Page.navigate', { url: URL_ });
  step ('navigated');
  await sleep (10000);                       // Babel transforms the JSX in-page
  step ('probing');

  const r = await send ('Runtime.evaluate',
                        { expression: PROBE, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw new Error (r.exceptionDetails.exception?.description || 'probe threw');

  const out = JSON.parse (r.result.value);
  if (out.error) throw new Error (out.error);

  console.log (`  measured ${out.panels} panels across 5 instruments at the design width`);

  let bad = 0;
  if (out.bodyOverflow > 1) {
    console.log (`  page scrolls sideways by ${out.bodyOverflow}px`);
    bad++;
  }
  for (const f of out.findings) {
    console.log (`  ${f.instrument.padEnd (8)} ${f.panel.padEnd (18)} ${f.control.padEnd (22)} overflows by ${f.overflowPx}px`);
    bad++;
  }

  if (bad) { console.error (`\n  ${bad} control(s) do not fit their panel`); process.exit (1); }
  console.log ('  every control fits inside its panel');
} catch (err) {
  console.error ('  check-layout:', err.message);
  process.exitCode = 1;
} finally {
  clearTimeout (killer);
  try { ws?.close(); } catch {}
  chrome.kill();
  try { rmSync (profile, { recursive: true, force: true }); } catch {}
  // Chrome leaves handles that keep the loop alive; the work is done, so say so.
  process.exit (process.exitCode || 0);
}
