/*
   Epi — the MIDI file player, for the browser build.

   Drop a .mid on the page, or open one from the settings, and it plays on the
   instrument. The score is handed to the AudioWorklet and played from the
   audio clock, so every note lands on the sample the file asks for rather than
   on whichever 128-frame boundary a timer happened to catch.

   Which parts get played is decided by ui/wasm/epi-smf.js, scoring each
   channel of each track on its program number, its name and its range. The
   choice is always shown and always overridable: a heuristic that cannot be
   corrected is worse than no heuristic.

   Host chrome, like the settings gear -- injected rather than added to ui/epi,
   because the plugin and the appliance get their notes from a host and a MIDI
   port and neither wants a file player.
*/
(function (global) {
  'use strict';

  var HOST = null, SMF = null;
  var parsed = null, selected = [], scoreLen = 0;
  var playing = false, position = 0, duration = 0, fileName = '';
  var bar = null, partsOpen = false, filePill = null;

  var CSS = [
    '#epi-player{position:fixed;left:50%;transform:translateX(-50%);bottom:16px;z-index:9995;',
      'display:flex;align-items:center;gap:12px;padding:9px 14px;max-width:min(720px,92vw);',
      'border-radius:24px;border:1px solid rgba(217,196,138,0.45);',
      'background:linear-gradient(180deg,rgba(30,28,24,0.98),rgba(16,15,18,0.98));',
      'box-shadow:0 10px 34px rgba(0,0,0,0.6);color:#ddd6c6;',
      'font:400 12px/1 ui-sans-serif,system-ui,sans-serif}',
    '#epi-player button{background:none;border:1px solid rgba(217,196,138,0.35);color:#e6d3a0;',
      'border-radius:6px;padding:5px 9px;cursor:pointer;font:600 11px/1 inherit;',
      'letter-spacing:0.1em;text-transform:uppercase;white-space:nowrap}',
    '#epi-player button:hover{border-color:rgba(240,221,166,0.9);color:#fdf0c8}',
    '#epi-player .play{width:38px;height:30px;font-size:13px;letter-spacing:0}',
    '#epi-player .name{max-width:190px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;',
      'color:#b8ae99}',
    '#epi-player .time{font-family:ui-monospace,Menlo,monospace;font-size:11px;color:#8d8577;',
      'white-space:nowrap;font-variant-numeric:tabular-nums}',
    '#epi-player input[type=range]{flex:1;min-width:120px;accent-color:#d9c48a;height:3px}',
    '#epi-parts{position:fixed;left:50%;transform:translateX(-50%);bottom:64px;z-index:9995;',
      'width:min(520px,92vw);max-height:44vh;overflow:auto;padding:14px 16px;border-radius:10px;',
      'border:1px solid rgba(217,196,138,0.25);background:#0e0e11;',
      'box-shadow:0 16px 44px rgba(0,0,0,0.6);color:#ddd6c6;',
      'font:400 12px/1.6 ui-sans-serif,system-ui,sans-serif}',
    '#epi-parts h4{margin:0 0 2px;font:600 11px/1.4 inherit;letter-spacing:0.16em;',
      'text-transform:uppercase;color:#d9c48a}',
    '#epi-parts .sub{margin:0 0 10px;color:#7d766a;font-size:11px}',
    '#epi-parts label{display:flex;align-items:center;gap:9px;padding:4px 0;',
      'border-bottom:1px solid rgba(255,255,255,0.05);cursor:pointer}',
    '#epi-parts .grow{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}',
    '#epi-parts .why{color:#7d766a;font-size:11px;white-space:nowrap}',
    '#epi-parts .n{font-family:ui-monospace,Menlo,monospace;font-size:11px;color:#8d8577;',
      'min-width:64px;text-align:right}',
    '#epi-drop{position:fixed;inset:0;z-index:9999;display:flex;align-items:center;',
      'justify-content:center;background:rgba(8,8,10,0.82);color:#e6d3a0;pointer-events:none;',
      'font:600 14px/1.6 ui-sans-serif,system-ui,sans-serif;letter-spacing:0.16em;',
      'text-transform:uppercase;border:2px dashed rgba(217,196,138,0.5)}',
    '#epi-buttons{position:fixed;right:18px;bottom:16px;z-index:9996;',
      'display:flex;align-items:center;gap:9px}',
    '.epi-pill{height:44px;display:flex;align-items:center;gap:9px;padding:0 18px 0 15px;',
      'border-radius:22px;border:1px solid rgba(217,196,138,0.55);',
      'background:linear-gradient(180deg,rgba(30,28,24,0.97),rgba(17,16,19,0.97));',
      'color:#e6d3a0;cursor:pointer;font:600 12px/1 ui-sans-serif,system-ui,sans-serif;',
      'letter-spacing:0.16em;text-transform:uppercase;',
      'box-shadow:0 6px 22px rgba(0,0,0,0.55);transition:all .16s ease}',
    '.epi-pill:hover{border-color:rgba(240,221,166,0.95);color:#fdf0c8;',
      'transform:translateY(-1px);box-shadow:0 9px 26px rgba(0,0,0,0.6)}',
    '.epi-pill .glyph{font-size:16px;line-height:1}',
    '#epi-file.loaded{border-color:rgba(130,225,160,0.75);color:#9fe8b6}',
    // The transport is centred and the pills are on the right; on a narrow
    // window they would sit on top of each other, so the transport moves up.
    '@media (max-width:900px){#epi-player{bottom:70px}}',
    '@media (max-width:640px){#epi-player .name{max-width:90px}',
      '.epi-pill{padding:0 13px 0 11px;letter-spacing:0.1em}}'
  ].join ('');

  // Both pills live in one bar so they cannot drift apart or overlap, and
  // either file may install first, so it is get-or-create rather than owned.
  function buttonBar () {
    var b = document.getElementById ('epi-buttons');
    if (!b) {
      b = document.createElement ('div');
      b.id = 'epi-buttons';
      document.body.appendChild (b);
    }
    return b;
  }
  global.__EPI_BUTTONS__ = buttonBar;

  function el (tag, cls, text) {
    var e = document.createElement (tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  function clock (s) {
    if (!isFinite (s) || s < 0) s = 0;
    var m = Math.floor (s / 60), r = Math.floor (s % 60);
    return m + ':' + (r < 10 ? '0' : '') + r;
  }

  // ---- loading -----------------------------------------------------------
  function loadArrayBuffer (buf, name) {
    var p;
    try { p = SMF.parse (buf); }
    catch (err) { alert ('Could not read that file: ' + err.message); return; }

    if (!p.events.length) { alert ('That file has no notes in it.'); return; }

    parsed = p;
    fileName = name;
    if (filePill) {
      filePill.classList.add ('loaded');
      filePill.lastChild.textContent = 'MIDI File';
      filePill.title = name;
    }
    duration = p.duration;
    var chosen = SMF.choosePiano (p);
    selected = chosen.selected;
    sendScore();
    build();
    // When the guess was not confident, open the part list rather than
    // quietly playing something the file did not ask for.
    if (!chosen.confident && p.parts.length > 1) showParts();
  }

  function sendScore () {
    if (!parsed) return;
    var evs = SMF.schedule (parsed, selected);
    scoreLen = evs.length;

    var times = new Float64Array (evs.length);
    var types = new Uint8Array (evs.length);
    var notes = new Uint8Array (evs.length);
    var values = new Float32Array (evs.length);

    for (var i = 0; i < evs.length; i++) {
      var e = evs[i];
      times[i] = e.time;
      notes[i] = e.note;
      values[i] = e.value;
      types[i] = e.kind === 'off' ? 0
               : e.kind === 'on' ? 1
               : e.note === 64 ? 2
               : e.note === 66 ? 3 : 4;
      if (e.kind === 'cc') notes[i] = 0;
    }
    HOST.sendScore ({ times: times, types: types, notes: notes, values: values });
  }

  // ---- transport ---------------------------------------------------------
  function setPlaying (on) {
    if (on) HOST.startAudio();
    playing = on;
    HOST.transport ({ playing: on });
    render();
  }

  function seek (t) {
    position = t;
    HOST.transport ({ seek: t });
    render();
  }

  // ---- the bar -----------------------------------------------------------
  var playBtn, scrub, timeLabel, nameLabel, scrubbing = false;

  function build () {
    if (bar) bar.remove();
    bar = el ('div');
    bar.id = 'epi-player';

    playBtn = el ('button', 'play', '▶');
    playBtn.title = 'Play';
    playBtn.onclick = function () { setPlaying (!playing); };
    bar.appendChild (playBtn);

    nameLabel = el ('span', 'name', fileName);
    nameLabel.title = fileName;
    bar.appendChild (nameLabel);

    scrub = document.createElement ('input');
    scrub.type = 'range';
    scrub.min = '0';
    scrub.max = String (Math.max (0.1, duration));
    scrub.step = '0.01';
    scrub.value = '0';
    scrub.oninput = function () { scrubbing = true; timeLabel.textContent = label (+scrub.value); };
    scrub.onchange = function () { scrubbing = false; seek (+scrub.value); };
    bar.appendChild (scrub);

    timeLabel = el ('span', 'time', label (0));
    bar.appendChild (timeLabel);

    var partsBtn = el ('button', null, 'Parts');
    partsBtn.onclick = function () { partsOpen ? hideParts() : showParts(); };
    bar.appendChild (partsBtn);

    var close = el ('button', null, '✕');
    close.title = 'Close the player';
    close.onclick = function () {
      setPlaying (false);
      HOST.sendScore (null);
      parsed = null;
      hideParts();
      if (filePill) { filePill.classList.remove ('loaded'); filePill.title = 'Play a MIDI file on the instrument'; }
      if (bar) { bar.remove(); bar = null; }
    };
    bar.appendChild (close);

    document.body.appendChild (bar);
    render();
  }

  function label (t) { return clock (t) + ' / ' + clock (duration); }

  function render () {
    if (!bar) return;
    playBtn.textContent = playing ? '⏸' : '▶';
    playBtn.title = playing ? 'Pause' : 'Play';
    if (!scrubbing) {
      scrub.value = String (Math.min (position, duration));
      timeLabel.textContent = label (position);
    }
  }

  // ---- the part list -----------------------------------------------------
  function showParts () {
    hideParts();
    if (!parsed) return;
    partsOpen = true;

    var box = el ('div');
    box.id = 'epi-parts';
    box.appendChild (el ('h4', null, 'What to play'));
    box.appendChild (el ('p', 'sub',
      'Format ' + parsed.format + ', ' + parsed.numTracks + ' track'
      + (parsed.numTracks === 1 ? '' : 's') + ', ' + parsed.parts.length + ' part'
      + (parsed.parts.length === 1 ? '' : 's') + '. '
      + (parsed.format === 0
          ? 'A format 0 file is one track, so the parts are its channels.'
          : 'Chosen by program number, name and range.')));

    var scored = SMF.scoreParts (parsed);
    scored.sort (function (a, b) { return b.score - a.score; });

    scored.forEach (function (sc) {
      var t = sc.part;
      var lab = document.createElement ('label');
      var cb = document.createElement ('input');
      cb.type = 'checkbox';
      cb.checked = selected.indexOf (t.key) >= 0;
      cb.onchange = function () {
        selected = cb.checked ? selected.concat ([t.key])
                              : selected.filter (function (k) { return k !== t.key; });
        sendScore();
      };
      lab.appendChild (cb);

      var title = (t.name || t.instrument || '').trim();
      if (!title) title = 'Track ' + (t.track + 1);
      lab.appendChild (el ('span', 'grow', title + '  ·  ch ' + (t.channel + 1)));
      lab.appendChild (el ('span', 'why', sc.why.join (', ')));
      lab.appendChild (el ('span', 'n', t.noteCount + ' notes'));
      box.appendChild (lab);
    });

    document.body.appendChild (box);
  }

  function hideParts () {
    partsOpen = false;
    var b = document.getElementById ('epi-parts');
    if (b) b.remove();
  }

  // ---- files in ----------------------------------------------------------
  function pick () {
    var input = document.createElement ('input');
    input.type = 'file';
    input.accept = '.mid,.midi,audio/midi,audio/x-midi';
    input.onchange = function () {
      var f = input.files && input.files[0];
      if (!f) return;
      f.arrayBuffer().then (function (b) { loadArrayBuffer (b, f.name); });
    };
    input.click();
  }

  function installDropTarget () {
    var overlay = null;
    var depth = 0;

    function show () {
      if (overlay) return;
      overlay = el ('div', null, 'Drop a MIDI file');
      overlay.id = 'epi-drop';
      document.body.appendChild (overlay);
    }
    function hide () { if (overlay) { overlay.remove(); overlay = null; } }

    // dragenter and dragleave fire for every child element the pointer
    // crosses, so the overlay flickers unless the nesting is counted.
    window.addEventListener ('dragenter', function (e) {
      e.preventDefault();
      if (++depth === 1) show();
    });
    window.addEventListener ('dragover', function (e) { e.preventDefault(); });
    window.addEventListener ('dragleave', function (e) {
      e.preventDefault();
      if (--depth <= 0) { depth = 0; hide(); }
    });
    window.addEventListener ('drop', function (e) {
      e.preventDefault();
      depth = 0; hide();
      var f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
      if (!f) return;
      f.arrayBuffer().then (function (b) { loadArrayBuffer (b, f.name); });
    });
  }

  function install () {
    HOST = global.__EPI_HOST__;
    SMF = global.__EPI_SMF__;
    if (!HOST || !SMF || !HOST.isWasmBuild) return;

    var style = document.createElement ('style');
    style.textContent = CSS;
    document.head.appendChild (style);

    installDropTarget();

    var pill = el ('button', 'epi-pill');
    pill.id = 'epi-file';
    pill.title = 'Play a MIDI file on the instrument';
    pill.setAttribute ('aria-label', 'Open a MIDI file');
    pill.appendChild (el ('span', 'glyph', '\u266A'));
    pill.appendChild (el ('span', null, 'MIDI File'));
    pill.onclick = function (e) { e.stopPropagation(); HOST.startAudio(); pick(); };
    buttonBar().appendChild (pill);
    filePill = pill;

    HOST.onTransport (function (pos, isPlaying) {
      if (pos !== undefined) position = pos;
      playing = !!isPlaying;
      render();
    });

    global.__EPI_PLAYER__ = { open: pick, loadArrayBuffer: loadArrayBuffer,
                              hasFile: function () { return !!parsed; } };
  }

  if (document.readyState === 'loading')
    document.addEventListener ('DOMContentLoaded', install);
  else
    install();
}) (window);
