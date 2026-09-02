/*
   Epi — the settings panel, for the browser build only.

   A gear in the corner that opens MIDI: which inputs are listening, which
   channel, what is arriving, where the pedals are, and the controller map
   with per-parameter MIDI learn.

   It is injected into the DOM by the host rather than added to ui/epi,
   deliberately. The interface is shared byte for byte with the plugin and the
   headless build, and neither of those needs a MIDI device picker -- the
   plugin gets its MIDI from the host, the appliance from ALSA or CoreMIDI. A
   control that only one host can honour belongs to that host, not to the
   interface all three share.

   The styling deliberately borrows the interface's palette rather than
   importing anything from it, so nothing here can break a panel over there.
*/
(function (global) {
  'use strict';

  var MIDI = null, HOST = null, root = null, open = false;

  var CSS = [
    // A labelled pill rather than a bare glyph. An unlabelled gear in a
    // corner is a thing people do not find: it has to say what it opens, and
    // it has to be big enough to hit on a phone -- 44 px is the smallest
    // comfortable touch target.
    '#epi-gear{position:fixed;right:18px;bottom:16px;z-index:9996;height:44px;',
      'display:flex;align-items:center;gap:9px;padding:0 18px 0 15px;',
      'border-radius:22px;border:1px solid rgba(217,196,138,0.55);',
      'background:linear-gradient(180deg,rgba(30,28,24,0.97),rgba(17,16,19,0.97));',
      'color:#e6d3a0;cursor:pointer;font:600 12px/1 ui-sans-serif,system-ui,sans-serif;',
      'letter-spacing:0.16em;text-transform:uppercase;',
      'box-shadow:0 6px 22px rgba(0,0,0,0.55);transition:all .16s ease}',
    '#epi-gear:hover{border-color:rgba(240,221,166,0.95);color:#fdf0c8;',
      'transform:translateY(-1px);box-shadow:0 9px 26px rgba(0,0,0,0.6)}',
    '#epi-gear .glyph{font-size:17px;line-height:1}',
    '#epi-gear .dot{width:6px;height:6px;border-radius:50%;background:rgba(217,196,138,0.45)}',
    '#epi-gear.live{border-color:rgba(130,225,160,0.75);color:#9fe8b6}',
    '#epi-gear.live .dot{background:#7fdca0;box-shadow:0 0 7px rgba(127,220,160,0.9)}',
    // Until it has been opened once, it breathes. It stops for good after
    // that: an attention cue that keeps pulsing is an irritation, not a cue.
    '#epi-gear.hint{animation:epiPulse 2.4s ease-in-out infinite}',
    '@keyframes epiPulse{0%,100%{box-shadow:0 6px 22px rgba(0,0,0,0.55),0 0 0 0 rgba(217,196,138,0.34)}',
      '55%{box-shadow:0 6px 22px rgba(0,0,0,0.55),0 0 0 11px rgba(217,196,138,0)}}',
    '@media (prefers-reduced-motion:reduce){#epi-gear.hint{animation:none;',
      'border-color:rgba(240,221,166,0.9)}}',
    '@media (max-width:640px){#epi-gear{right:12px;bottom:12px}}',

    '#epi-settings{position:fixed;inset:0;z-index:9998;display:flex;align-items:center;',
      'justify-content:center;background:rgba(6,6,8,0.72);backdrop-filter:blur(4px)}',
    '#epi-settings .box{width:min(760px,92vw);max-height:86vh;overflow:auto;background:#0e0e11;',
      'border:1px solid rgba(217,196,138,0.22);border-radius:10px;padding:22px 24px;',
      'box-shadow:0 24px 60px rgba(0,0,0,0.6);color:#ddd6c6;font:400 13px/1.6 ui-sans-serif,system-ui,sans-serif}',
    '#epi-settings h2{margin:0 0 2px;font:600 13px/1.4 ui-sans-serif,system-ui,sans-serif;',
      'letter-spacing:0.18em;text-transform:uppercase;color:#d9c48a}',
    '#epi-settings h3{margin:20px 0 8px;font:600 11px/1.4 ui-sans-serif,system-ui,sans-serif;',
      'letter-spacing:0.16em;text-transform:uppercase;color:#8d8577}',
    '#epi-settings .sub{margin:0 0 4px;color:#7d766a;font-size:12px}',
    '#epi-settings .row{display:flex;align-items:center;gap:10px;padding:5px 0;',
      'border-bottom:1px solid rgba(255,255,255,0.045)}',
    '#epi-settings .row:last-child{border-bottom:0}',
    '#epi-settings .grow{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}',
    '#epi-settings .mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;color:#9a9384}',
    '#epi-settings .num{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;',
      'color:#8d8577;min-width:58px;text-align:right}',
    '#epi-settings button{background:rgba(217,196,138,0.1);border:1px solid rgba(217,196,138,0.3);',
      'color:#d9c48a;border-radius:5px;padding:3px 9px;cursor:pointer;font:500 11px/1.5 inherit;',
      'letter-spacing:0.08em;text-transform:uppercase}',
    '#epi-settings button:hover{background:rgba(217,196,138,0.2)}',
    '#epi-settings button.armed{background:rgba(230,160,90,0.24);border-color:rgba(230,160,90,0.75);color:#f0b878}',
    '#epi-settings select{background:#15151a;color:#cfc7b4;border:1px solid rgba(255,255,255,0.12);',
      'border-radius:5px;padding:3px 6px;font:inherit}',
    '#epi-settings .note{color:#7d766a;font-size:12px;margin:8px 0 0}',
    '#epi-settings .warn{color:#e0a86a}',
    '#epi-settings .close{position:sticky;top:0;float:right}',
    '#epi-settings .pedals{display:flex;gap:18px;font-family:ui-monospace,Menlo,monospace;font-size:12px}',
    '#epi-settings .pill{padding:2px 8px;border-radius:99px;border:1px solid rgba(255,255,255,0.12);color:#7d766a}',
    '#epi-settings .pill.on{border-color:rgba(120,220,150,0.6);color:#8fe0a8}'
  ].join ('');

  function el (tag, cls, text) {
    var e = document.createElement (tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  function render () {
    if (!root) return;
    var s = MIDI.state();
    root.innerHTML = '';

    var box = el ('div', 'box');
    root.appendChild (box);

    var close = el ('button', 'close', 'Close');
    close.onclick = hide;
    box.appendChild (close);

    box.appendChild (el ('h2', null, 'MIDI'));
    box.appendChild (el ('p', 'sub',
      'Epi answers the same controller numbers here as it does on hardware. '
      + 'The full map is in docs/ControlMap.md.'));

    // ---- devices ---------------------------------------------------------
    box.appendChild (el ('h3', null, 'Inputs'));
    if (!s.available) {
      var warn = el ('p', 'note warn', s.reason || 'MIDI is not connected yet.');
      box.appendChild (warn);
      if (navigator.requestMIDIAccess) {
        var b = el ('button', null, 'Connect MIDI');
        b.onclick = function () { MIDI.connect().then (render); };
        box.appendChild (b);
      }
    } else if (s.inputs.length === 0) {
      box.appendChild (el ('p', 'note', 'No MIDI inputs found. Plug something in — the list updates by itself.'));
    } else {
      s.inputs.forEach (function (inp) {
        var row = el ('div', 'row');
        var cb = document.createElement ('input');
        cb.type = 'checkbox'; cb.checked = inp.on;
        cb.onchange = function () { MIDI.setInputEnabled (inp.id, cb.checked); };
        row.appendChild (cb);
        row.appendChild (el ('span', 'grow', inp.name + (inp.manufacturer ? '  ·  ' + inp.manufacturer : '')));
        box.appendChild (row);
      });
    }

    // ---- channel ---------------------------------------------------------
    var chRow = el ('div', 'row');
    chRow.appendChild (el ('span', 'grow', 'Channel'));
    var sel = document.createElement ('select');
    var omni = document.createElement ('option');
    omni.value = '0'; omni.textContent = 'Omni (all)';
    sel.appendChild (omni);
    for (var i = 1; i <= 16; i++) {
      var o = document.createElement ('option');
      o.value = String (i); o.textContent = String (i);
      sel.appendChild (o);
    }
    sel.value = String (MIDI.channel());
    sel.onchange = function () { MIDI.setChannel (parseInt (sel.value, 10)); };
    chRow.appendChild (sel);
    box.appendChild (chRow);

    // ---- what is arriving ------------------------------------------------
    box.appendChild (el ('h3', null, 'Activity'));
    var act = el ('div', 'row');
    act.appendChild (el ('span', 'grow mono', s.lastMessage || 'nothing yet'));
    act.appendChild (el ('span', 'num', s.counted + ' msg'));
    box.appendChild (act);

    var ped = el ('div', 'row');
    var pills = el ('div', 'pedals');
    var sus = el ('span', 'pill' + (s.pedals.sustain > 0.02 ? ' on' : ''),
                  'sustain ' + Math.round (s.pedals.sustain * 100) + '%');
    pills.appendChild (sus);
    pills.appendChild (el ('span', 'pill' + (s.pedals.sostenuto ? ' on' : ''), 'sostenuto'));
    pills.appendChild (el ('span', 'pill' + (s.pedals.soft ? ' on' : ''), 'soft'));
    pills.appendChild (el ('span', 'pill' + (s.notesDown > 0 ? ' on' : ''), s.notesDown + ' held'));
    ped.appendChild (pills);
    box.appendChild (ped);
    box.appendChild (el ('p', 'note',
      'The sustain pedal is read as depth, not as a switch, so half-pedalling works.'));

    // ---- the map ---------------------------------------------------------
    box.appendChild (el ('h3', null, 'Controllers'));
    var learning = MIDI.learning();
    box.appendChild (el ('p', 'sub', learning
      ? 'Move a control on your MIDI device to bind it to ' + learning + '.'
      : 'Every parameter answers a CC and an NRPN. Press Learn to bind a different CC.'));

    var byPanel = {};
    HOST.params.forEach (function (p) { (byPanel[p.panel] = byPanel[p.panel] || []).push (p); });

    Object.keys (byPanel).forEach (function (panel) {
      var h = el ('div', 'row');
      h.appendChild (el ('span', 'grow', panel));
      h.style.opacity = '0.55';
      box.appendChild (h);

      byPanel[panel].forEach (function (p) {
        var row = el ('div', 'row');
        row.appendChild (el ('span', 'grow', p.name));
        var cc = MIDI.effectiveCc (p.id);
        var stolenBy = MIDI.shadowedBy (p.id);
        var ccSpan = el ('span', 'num', cc === undefined ? 'CC —' : 'CC ' + cc);
        if (MIDI.isOverridden (p.id)) ccSpan.style.color = '#e0a86a';
        if (stolenBy) {
          ccSpan.style.color = '#8a6a5a';
          ccSpan.title = 'CC ' + MIDI.publishedCc (p.id) + ' is now bound to '
                       + ((HOST.byId[stolenBy] || {}).name || stolenBy);
        }
        row.appendChild (ccSpan);
        row.appendChild (el ('span', 'num', 'NRPN ' + MIDI.nrpnFor (p.id)));

        var learn = el ('button', learning === p.id ? 'armed' : null,
                        learning === p.id ? 'Waiting' : 'Learn');
        learn.onclick = function () {
          if (learning === p.id) MIDI.cancelLearn(); else MIDI.learn (p.id);
        };
        row.appendChild (learn);

        if (MIDI.isOverridden (p.id)) {
          var rst = el ('button', null, 'Reset');
          rst.onclick = function () { MIDI.clearOverride (p.id); };
          row.appendChild (rst);
        }
        box.appendChild (row);
      });
    });

    var footer = el ('div', 'row');
    footer.appendChild (el ('span', 'grow', ''));
    var clearAll = el ('button', null, 'Reset all learned CCs');
    clearAll.onclick = function () { MIDI.clearAllOverrides(); };
    footer.appendChild (clearAll);
    box.appendChild (footer);
  }

  function show () {
    if (open) return;
    open = true;
    root = el ('div');
    root.id = 'epi-settings';
    root.onclick = function (e) { if (e.target === root) hide(); };
    document.body.appendChild (root);
    render();
    // Connecting on open rather than on load: a permission prompt that
    // appears before anybody has asked for MIDI is a prompt most people deny.
    if (!MIDI.state().available && navigator.requestMIDIAccess) MIDI.connect().then (render);
  }

  function hide () {
    open = false;
    if (root) { root.remove(); root = null; }
  }

  function install () {
    HOST = global.__EPI_HOST__;
    MIDI = global.__EPI_MIDI__;
    if (!HOST || !MIDI || !HOST.isWasmBuild) return;   // this build only

    MIDI.init (HOST);

    var style = document.createElement ('style');
    style.textContent = CSS;
    document.head.appendChild (style);

    var gear = el ('button');
    gear.id = 'epi-gear';
    gear.title = 'MIDI inputs, controller map and settings';
    gear.setAttribute ('aria-label', 'MIDI settings');
    gear.appendChild (el ('span', 'dot'));
    gear.appendChild (el ('span', 'glyph', '⚙'));
    gear.appendChild (el ('span', null, 'MIDI'));

    var seen = false;
    try { seen = localStorage.getItem ('epi.gearSeen') === '1'; } catch (e) {}
    if (! seen) gear.classList.add ('hint');
    // Opening the settings is also a gesture, and it is the one gesture
    // available while the start overlay is up -- so it starts the audio too,
    // rather than leaving somebody with a panel open and a silent instrument.
    gear.onclick = function (e) {
      e.stopPropagation();
      if (HOST.startAudio) HOST.startAudio();
      gear.classList.remove ('hint');
      try { localStorage.setItem ('epi.gearSeen', '1'); } catch (err) {}
      show();
    };
    document.body.appendChild (gear);

    MIDI.onChange (function (s) {
      gear.classList.toggle ('live', s.available && s.inputs.some (function (i) { return i.on; }));
      if (open) render();
    });

    // Esc closes, which is what everybody tries first.
    document.addEventListener ('keydown', function (e) { if (e.key === 'Escape' && open) hide(); });
  }

  if (document.readyState === 'loading')
    document.addEventListener ('DOMContentLoaded', install);
  else
    install();
}) (window);
