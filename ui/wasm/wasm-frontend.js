/*
   Epi — the browser side of the WebAssembly build.

   A drop-in replacement for juce-framework-frontend.js, exactly as the
   headless host's shim is. The page asks for that filename and gets this
   instead, so every other file the interface is made of is byte-identical to
   the plugin's and nothing in ui/epi knows which of the three hosts is behind
   it: the plugin's WebView, the headless server, or this.

   What is different here is that there is no host. The instrument is running
   in an AudioWorklet a few metres of memory away, so what the other two shims
   send over IPC or HTTP this one posts to a worklet port, and what they
   receive as events arrives as a packed Float32Array.

   Two consequences shape the file:

   getNormalisedValue is SYNCHRONOUS -- the interface calls it while
   rendering -- so every value is cached here and the worklet is told
   afterwards. The knob follows the pointer; the audio catches up in the next
   quantum.

   And a browser will not start an AudioContext without a gesture, so the
   instrument cannot exist until someone clicks. The interface is drawn and
   fully interactive before that; the first click starts the audio and the
   overlay goes away.
*/
(function (global) {
  'use strict';

  var BASE = (function () {
    var s = document.currentScript;
    return s ? s.src.replace (/[^/]*$/, '') : './';
  })();

  var TELEMETRY_HZ = 30;

  // ---- the parameter layout, as data ------------------------------------
  // Dumped from the running instrument (epi-headless --dump-parameters), so
  // the ranges here are the ranges, not a transcription of them.
  var params = [], byId = {}, presets = [], presetByName = {};
  var raw = [], norm = {};
  var node = null, ctx = null, ready = false;
  var pending = [];        // commands raised before the audio existed

  function post (m) { if (node) node.port.postMessage (m); else pending.push (m); }

  // ---- juce::NormalisableRange, reimplemented ---------------------------
  // Verified against the real thing at a hundred points per parameter by
  // tools/check-param-map.mjs. The two rounding modes differ on purpose --
  // see ui/wasm/epi-params.mjs, which this mirrors.
  function roundToInt (v) {
    var f = Math.floor (v), frac = v - f;
    if (frac > 0.5) return f + 1;
    if (frac < 0.5) return f;
    return (f % 2 === 0) ? f : f + 1;
  }

  function fromNormalised (p, proportion) {
    proportion = proportion < 0 ? 0 : (proportion > 1 ? 1 : proportion);
    if (p.kind === 'choice') {
      var end = p.choices.length - 1;
      var cv = Math.fround (Math.fround (proportion) * Math.fround (end));
      return roundToInt (Math.max (0, Math.min (end, cv)));
    }
    var value;
    if (!p.symmetricSkew) {
      var q = proportion;
      if (p.skew !== 1 && q > 0) q = Math.exp (Math.log (q) / p.skew);
      value = p.start + (p.end - p.start) * q;
    } else {
      var d = 2 * proportion - 1;
      if (p.skew !== 1 && d !== 0)
        d = Math.exp (Math.log (Math.abs (d)) / p.skew) * (d < 0 ? -1 : 1);
      value = p.start + (p.end - p.start) / 2 * (1 + d);
    }
    if (p.interval > 0)
      value = p.start + p.interval * Math.floor ((value - p.start) / p.interval + 0.5);
    return (value <= p.start || p.end <= p.start) ? p.start
         : (value >= p.end ? p.end : value);
  }

  // ---- relays ------------------------------------------------------------
  function listenerList () {
    var map = {}, next = 0;
    return {
      addListener: function (fn) { map[++next] = fn; return next; },
      removeListener: function (id) { delete map[id]; },
      fire: function () { for (var k in map) if (map.hasOwnProperty (k)) map[k](); }
    };
  }

  var sliders = {}, combos = {}, toggles = {};

  function applyParam (id, n, fromHost) {
    var p = byId[id];
    if (!p) return;
    norm[id] = n;
    raw[p.__index] = fromNormalised (p, n);
    if (!fromHost) post ({ t: 'param', i: p.__index, v: raw[p.__index] });
  }

  function makeSlider (id) {
    var ev = listenerList();
    return {
      getNormalisedValue: function () { return norm[id]; },
      setNormalisedValue: function (n) { applyParam (id, n, false); ev.fire(); },
      __fromHost: function (n) { if (n !== norm[id]) { applyParam (id, n, false); ev.fire(); } },
      valueChangedEvent: ev,
      properties: { start: 0, end: 1, interval: 0, name: id, label: '', numSteps: 0 }
    };
  }

  function makeCombo (id) {
    var ev = listenerList();
    var p = function () { return byId[id]; };
    return {
      getChoiceIndex: function () {
        var q = p(); return q ? Math.round (norm[id] * (q.choices.length - 1)) : 0;
      },
      setChoiceIndex: function (i) {
        var q = p(); if (!q) return;
        var n = q.choices.length > 1 ? i / (q.choices.length - 1) : 0;
        applyParam (id, n, false); ev.fire();
      },
      __fromHost: function (n) { if (n !== norm[id]) { applyParam (id, n, false); ev.fire(); } },
      valueChangedEvent: ev,
      properties: { name: id, parameterIndex: 0, choices: [] }
    };
  }

  // ---- the event channel -------------------------------------------------
  var listeners = {};
  var backend = {
    addEventListener: function (n, f) { (listeners[n] = listeners[n] || []).push (f); },
    removeEventListener: function (n, f) {
      listeners[n] = (listeners[n] || []).filter (function (g) { return g !== f; });
    },
    emitEvent: function (n, payload) { handleEvent (n, payload || {}); }
  };
  function dispatch (n, p) {
    var l = listeners[n] || [];
    for (var i = 0; i < l.length; ++i) l[i] (p);
  }

  // ---- presets -----------------------------------------------------------
  var currentPreset = '', dirty = false;

  function loadPreset (name) {
    var pr = presetByName[name];
    if (!pr) return;
    // Defaults-first, exactly as the plugin's preset manager does it: a
    // preset is a complete snapshot, so anything it does not name goes back
    // to its default rather than keeping the last value.
    params.forEach (function (p) {
      var v = (pr.values && pr.values[p.id] !== undefined) ? pr.values[p.id] : p.default;
      applyParam (p.id, v, false);
      var r = p.kind === 'choice' ? combos[p.id] : sliders[p.id];
      if (r) r.valueChangedEvent.fire();
    });
    if (pr.tineMods)   pr.tineMods.forEach   (function (_, i) { if (i % 2 === 0) post ({ t: 'tineMod',   i: i / 2, a: pr.tineMods[i],   b: pr.tineMods[i + 1] }); });
    if (pr.stringMods) pr.stringMods.forEach (function (_, i) { if (i % 2 === 0) post ({ t: 'stringMod', i: i / 2, a: pr.stringMods[i], b: pr.stringMods[i + 1] }); });
    if (pr.grandMods)  pr.grandMods.forEach  (function (_, i) { if (i % 2 === 0) post ({ t: 'grandMod',  i: i / 2, a: pr.grandMods[i],  b: pr.grandMods[i + 1] }); });
    if (pr.pickupMods) pr.pickupMods.forEach (function (_, i) { if (i % 3 === 0) post ({ t: 'pickupMod', i: i / 3, a: pr.pickupMods[i], b: pr.pickupMods[i + 1], c: pr.pickupMods[i + 2] }); });
    if (pr.cabMods)  post ({ t: 'cabMod',  v: pr.cabMods });
    if (pr.micMods)  post ({ t: 'micMod',  v: pr.micMods });
    if (pr.micStage) post ({ t: 'micStage', v: pr.micStage });
    if (pr.velMap)   post ({ t: 'velMap',  v: pr.velMap });
    currentPreset = name; dirty = false;
    emitPresetInfo();
  }

  function emitPresetInfo () { dispatch ('presetInfo', { name: currentPreset, dirty: dirty }); }

  function presetIndex () {
    for (var i = 0; i < presets.length; ++i) if (presets[i].name === currentPreset) return i;
    return 0;
  }

  // ---- what the interface asks the host to do ----------------------------
  function handleEvent (name, m) {
    switch (name) {
      case 'ui_note':
        startAudio();
        post ({ t: 'note', note: m.note, velocity: m.velocity, on: !!m.on });
        break;
      case 'preset_load':   loadPreset (m.name); break;
      case 'preset_next':   loadPreset (presets[(presetIndex() + 1) % presets.length].name); break;
      case 'preset_prev':   loadPreset (presets[(presetIndex() - 1 + presets.length) % presets.length].name); break;
      // Saving to a server there isn't would be a button that lies. The web
      // build has no user presets; panels.jsx only shows what it is given.
      case 'preset_save':
      case 'preset_delete': break;

      case 'tine_mod':    post ({ t: 'tineMod',   i: m.index, a: m.len, b: m.dia }); break;
      case 'string_mod':  post ({ t: 'stringMod', i: m.index, a: m.len, b: m.dia }); break;
      case 'grand_mod':   post ({ t: 'grandMod',  i: m.index, a: m.len, b: m.dia }); break;
      case 'pickup_mod':  post ({ t: 'pickupMod', i: m.index, a: m.h, b: m.g, c: m.s }); break;
      case 'tine_mod_reset':   resetPairs ('tineMod',   1, 1); break;
      case 'string_mod_reset': resetPairs ('stringMod', 1, 1); break;
      case 'grand_mod_reset':  resetPairs ('grandMod',  1, 1); break;
      case 'pickup_mod_reset': resetTriples(); break;
      case 'cab_mod':     post ({ t: 'cabMod', v: [m.box, m.cone, m.dist, m.angle, m.susp] }); break;
      case 'cab_mod_reset': post ({ t: 'cabMod', v: [0.74, 0.59, 0.5, 0.25, 0.5] }); break;
      case 'mic_mod':     post ({ t: 'micMod', v: [m.spread, m.bias, m.dist, m.lvlL, m.lvlR] }); break;
      case 'mic_mod_reset': post ({ t: 'micMod', v: [1, 0, 0, 1, 1] }); break;
      case 'mic_stage':   if (m.v && m.v.length === 31) post ({ t: 'micStage', v: m.v }); break;
      case 'vel_map':     post ({ t: 'velMap', v: [m.y0, m.y1, m.y2, m.y3, m.y4] }); break;
      case 'vel_map_reset': post ({ t: 'velMap', v: [0, 0.25, 0.5, 0.75, 1] }); break;
      default: break;
    }
    if (name !== 'ui_note') { dirty = true; emitPresetInfo(); }
  }

  function resetPairs (kind, a, b) {
    for (var i = 0; i < 88; ++i) post ({ t: kind, i: i, a: a, b: b });
  }
  function resetTriples () {
    for (var i = 0; i < 88; ++i) post ({ t: 'pickupMod', i: i, a: 0, b: 0, c: 1 });
  }

  // ---- native functions --------------------------------------------------
  var readSeq = 0, readWaiting = {};

  function readBack (what) {
    return new Promise (function (resolve) {
      if (!node) { resolve ([]); return; }
      var id = ++readSeq;
      readWaiting[id] = resolve;
      node.port.postMessage ({ t: 'read', id: id, what: what });
      setTimeout (function () {
        if (readWaiting[id]) { delete readWaiting[id]; resolve ([]); }
      }, 2000);
    });
  }

  function getNativeFunction (name) {
    return function () {
      switch (name) {
        case 'listFactoryPresets': return Promise.resolve (presets.map (function (p) { return p.name; }));
        case 'listUserPresets':    return Promise.resolve ([]);
        case 'reloadUI':           location.reload(); return Promise.resolve (null);
        case 'getTineMods':   return readBack ('tineMods');
        case 'getStringMods': return readBack ('stringMods');
        case 'getGrandMods':  return readBack ('grandMods');
        case 'getPickupMods': return readBack ('pickupMods');
        case 'getCabMods':    return readBack ('cabMods');
        case 'getMicMods':    return readBack ('micMods');
        case 'getMicStage':   return readBack ('micStage');
        case 'getVelMap':     return readBack ('velMap');
        default:              return Promise.resolve (null);
      }
    };
  }

  // ---- telemetry ---------------------------------------------------------
  // Unpacked from the layout src/wasm/epi_wasm.cpp packs, whose sizes the
  // worklet reports at startup so neither side hard-codes the other's.
  var shape = { traceLen: 0, fieldPoints: 0, numTines: 0, loNote: 21 };

  function onTelemetry (f, keys) {
    var S = 2, T = S + 10;
    var F = T + shape.traceLen, H = F + shape.fieldPoints;
    dispatch ('levels', {
      out: [f[0], f[1]],
      noteHz: f[S + 0], strikes: f[S + 1], tip: f[S + 2], flux: f[S + 3],
      offset: f[S + 4], vibL: f[S + 5], vibR: f[S + 6], voices: f[S + 7],
      lastNote: f[S + 8], pedal: f[S + 9] > 0.5,
      trace: Array.prototype.slice.call (f.subarray (T, F)),
      field: Array.prototype.slice.call (f.subarray (F, H)),
      harp:  Array.prototype.slice.call (f.subarray (H, H + shape.numTines)),
      keys: keys,
      loNote: shape.loNote
    });
  }

  // ---- bringing the instrument up ---------------------------------------
  var wasmModule = null, starting = false;

  function startAudio () {
    if (ctx || starting || !wasmModule) return;
    starting = true;

    ctx = new (global.AudioContext || global.webkitAudioContext)({ latencyHint: 'interactive' });
    ctx.audioWorklet.addModule (BASE + 'epi-worklet.js').then (function () {
      node = new AudioWorkletNode (ctx, 'epi-processor', {
        numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
        processorOptions: { wasmModule: wasmModule, rawParams: raw, telemetryHz: TELEMETRY_HZ }
      });
      node.port.onmessage = function (e) {
        var m = e.data;
        if (m.t === 'telemetry') onTelemetry (m.f, m.k);
        else if (m.t === 'ready') { shape = m; ready = true; }
        else if (m.t === 'read' && readWaiting[m.id]) { readWaiting[m.id] (m.v); delete readWaiting[m.id]; }
      };
      node.connect (ctx.destination);
      pending.forEach (function (m) { node.port.postMessage (m); });
      pending = [];
      if (ctx.state === 'suspended') ctx.resume();
      hideOverlay();
    }).catch (function (err) {
      starting = false;
      showOverlay ('Audio could not start: ' + err.message);
    });
  }

  function overlay () { return document.getElementById ('epi-start-overlay'); }
  function hideOverlay () { var o = overlay(); if (o) o.remove(); }
  function showOverlay (text) {
    var o = overlay();
    if (!o) {
      o = document.createElement ('div');
      o.id = 'epi-start-overlay';
      o.style.cssText = 'position:fixed;inset:0;z-index:9990;display:flex;' +
        'align-items:center;justify-content:center;background:rgba(8,8,10,0.86);' +
        'color:#d9c48a;font:500 15px/1.6 system-ui,sans-serif;letter-spacing:0.14em;' +
        'text-transform:uppercase;cursor:pointer;backdrop-filter:blur(3px)';
      document.body.appendChild (o);
    }
    o.textContent = text;
  }

  // ---- boot --------------------------------------------------------------
  //
  // The API objects MUST exist before this file returns. juce-bridge.jsx runs
  // in the next script tag and, finding no __JUCE__, installs its browser
  // mock -- after which the real one arriving later is simply ignored and the
  // page plays nothing while looking entirely healthy.
  //
  // So the layout and the presets are not fetched: they are written above
  // this file by the build, exactly as the headless host writes the current
  // parameter values above its own shim. Only the WebAssembly stays async,
  // which is free, because audio cannot start before a click anyway.
  function boot () {
    var data = global.__EPI_PARAMS__, bank = global.__EPI_PRESETS__;
    if (!data || !data.parameters) {
      showOverlay ('Parameter layout missing');
      global.__epiMountError = 'no __EPI_PARAMS__';
      return;
    }

    params = data.parameters;
    presets = (bank && bank.presets) || [];

    params.forEach (function (p, i) {
      p.__index = i;
      byId[p.id] = p;
      norm[p.id] = p.default;
      raw[i] = fromNormalised (p, p.default);
      if (p.kind === 'choice') combos[p.id] = makeCombo (p.id);
      else sliders[p.id] = makeSlider (p.id);
    });
    presets.forEach (function (p) { presetByName[p.name] = p; });

    global.__JUCE__ = {
      backend: backend,
      initialisationData: {
        __juce__sliders: Object.keys (sliders),
        __juce__comboBoxes: Object.keys (combos),
        __juce__toggles: [],
        __juce__functions: []
      },
      getAndroidUserScripts: function () { return ''; }
    };
    global.Juce = {
      getSliderState: function (id) { return sliders[id] || (sliders[id] = makeSlider (id)); },
      getComboBoxState: function (id) { return combos[id] || (combos[id] = makeCombo (id)); },
      getToggleState: function () {
        var ev = listenerList();
        return { getValue: function () { return false; }, setValue: function () {}, valueChangedEvent: ev };
      },
      backend: backend,
      getNativeFunction: getNativeFunction
    };
    global.juce = global.Juce;

    if (presets.length) { currentPreset = presets[0].name; loadPreset (currentPreset); }

    // What the host can do, for the parts of the web build that are not the
    // instrument: Web MIDI and the settings panel. They are separate files
    // because they are separate concerns, and they are host chrome rather
    // than interface -- ui/epi does not know they exist, which is what keeps
    // it byte-identical across all three hosts.
    global.__EPI_HOST__ = {
      params: params,
      byId: byId,
      isWasmBuild: true,

      // Moves the parameter AND the knob. Anything arriving from outside the
      // interface has to do both, or the display lies about the instrument.
      setNormalised: function (id, n) {
        var p = byId[id];
        if (!p) return;
        applyParam (id, n, false);
        var relay = p.kind === 'choice' ? combos[id] : sliders[id];
        if (relay) relay.valueChangedEvent.fire();
        dirty = true;
      },
      getNormalised: function (id) { return norm[id]; },

      note: function (n, v, on) { startAudio(); post ({ t: 'note', note: n, velocity: v, on: !!on }); },
      sustain: function (v) { post ({ t: 'sustain', v: v }); },
      sostenuto: function (b) { post ({ t: 'sostenuto', v: b }); },
      soft: function (b) { post ({ t: 'soft', v: b }); },
      allNotesOff: function () { post ({ t: 'allOff' }); },
      expression: function (v) { post ({ t: 'expression', v: v }); },
      pitchBend: function (semis) { post ({ t: 'bend', v: semis }); },

      loadPreset: loadPreset,
      presetNames: function () { return presets.map (function (p) { return p.name; }); },
      currentPreset: function () { return currentPreset; },
      startAudio: function () { startAudio(); },
      audioRunning: function () { return !!node; },
      onLevels: function (fn) { backend.addEventListener ('levels', fn); }
    };

    // The preset line is pushed on a timer rather than only when it changes.
    // The plugin and the headless host both send it at their telemetry rate,
    // and the interface relies on that: it subscribes when its panels mount,
    // which is AFTER this file has run, so a single emit at boot arrives
    // before anyone is listening and the name stays blank.
    setInterval (emitPresetInfo, 250);

    showOverlay ('Loading');
    fetch (BASE + 'epi-standalone.wasm')
      .then (function (r) {
        if (!r.ok) throw new Error ('HTTP ' + r.status);
        return r.arrayBuffer();
      })
      .then (function (b) { return WebAssembly.compile (b); })
      .then (function (m) {
        wasmModule = m;
        showOverlay ('Click to start');
        document.addEventListener ('pointerdown', startAudio);
        document.addEventListener ('keydown', startAudio);
        global.__epiWasmReady = true;
      })
      .catch (function (err) {
        showOverlay ('Could not load the instrument: ' + err.message);
        global.__epiMountError = String (err);
      });
  }

  boot();
}) (window);
