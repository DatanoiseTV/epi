/*
   Epi — Web MIDI for the browser build.

   The same control surface the headless host speaks, in JavaScript: the CC
   and NRPN numbers come from the parameter layout, which carries them from
   src/epi/ControlMap.h, so a controller template written against
   docs/ControlMap.md drives the web build and the appliance identically.
   There is one published map, not one per host.

   Three details are carried over from src/epi/MidiControlSurface.h because
   getting them wrong is silent rather than loud:

     * Data entry (CC 6 and 38) is shared between RPN and NRPN, and belongs to
       whichever selector arrived LAST, per channel. Tracking only one of them
       means an ordinary NRPN sweep walks into the pitch bend range.

     * A controller that sends a data MSB and no LSB is common and must still
       work, so the value is applied on the MSB with a zero low byte and
       applied again if the LSB follows.

     * The sustain pedal is read as DEPTH, not as a switch. The damper is a
       contact damping term, so half-pedalling is a real position rather than
       a threshold.

   Web MIDI is Chrome, Edge and Firefox; Safari does not implement it. That is
   reported rather than worked around -- the on-screen keyboard and the
   computer keyboard both still play.
*/
(function (global) {
  'use strict';

  var HOST = null;
  var access = null;
  var enabled = {};            // input id -> bool
  var overrides = {};          // param id -> cc, from MIDI learn
  var learnTarget = null;
  var channelFilter = 0;       // 0 = omni, else 1..16

  var ccToParam = {};          // built-in map
  var nrpnToParam = {};

  var state = {
    available: false,
    reason: '',
    inputs: [],
    lastMessage: '',
    pedals: { sustain: 0, sostenuto: false, soft: false },
    notesDown: 0,
    counted: 0
  };
  var listeners = [];
  function changed () { listeners.forEach (function (f) { try { f (state); } catch (e) {} }); }

  // ---- persistence -------------------------------------------------------
  function load (key, fallback) {
    try { var v = localStorage.getItem ('epi.' + key); return v ? JSON.parse (v) : fallback; }
    catch (e) { return fallback; }
  }
  function save (key, v) {
    try { localStorage.setItem ('epi.' + key, JSON.stringify (v)); } catch (e) {}
  }

  // ---- the map -----------------------------------------------------------
  function buildMap () {
    ccToParam = {}; nrpnToParam = {};
    HOST.params.forEach (function (p) {
      if (p.cc >= 0) ccToParam[p.cc] = p.id;
      nrpnToParam[p.nrpn] = p.id;
    });
    // A learned CC wins over the published one, and releases whatever it
    // displaced so two parameters never answer the same controller.
    Object.keys (overrides).forEach (function (id) {
      var cc = overrides[id];
      Object.keys (ccToParam).forEach (function (k) { if (ccToParam[k] === id) delete ccToParam[k]; });
      ccToParam[cc] = id;
    });
  }

  // ---- per-channel registered-parameter state ----------------------------
  var nrpnMsb = [], nrpnLsb = [], dataMsb = [], nrpnActive = [], lastValue = {};
  for (var c = 0; c < 16; c++) { nrpnMsb[c] = 0; nrpnLsb[c] = 0; dataMsb[c] = 0; nrpnActive[c] = false; }

  function applyByIndex (id, norm) {
    HOST.setNormalised (id, Math.max (0, Math.min (1, norm)));
  }

  function handleController (ch, cc, value) {
    // Learn takes the next controller that is not a pedal or a selector.
    if (learnTarget !== null && cc !== 64 && cc !== 66 && cc !== 67 &&
        cc !== 6 && cc !== 38 && cc !== 98 && cc !== 99 && cc !== 100 && cc !== 101) {
      overrides[learnTarget] = cc;
      save ('ccOverrides', overrides);
      buildMap();
      learnTarget = null;
      changed();
      return true;
    }

    switch (cc) {
      case 64: state.pedals.sustain = value / 127; HOST.sustain (value / 127); changed(); return true;
      case 66: state.pedals.sostenuto = value >= 64; HOST.sostenuto (value >= 64); changed(); return true;
      case 67: state.pedals.soft = value >= 64; HOST.soft (value >= 64); changed(); return true;
      case 11: HOST.expression (0.25 + 0.75 * value / 127); return true;
      case 120: case 123: HOST.allNotesOff(); state.notesDown = 0; changed(); return true;

      case 99: nrpnMsb[ch] = value; nrpnActive[ch] = true; return true;
      case 98: nrpnLsb[ch] = value; nrpnActive[ch] = true; return true;
      case 100: case 101: nrpnActive[ch] = false; return false;

      case 6:
        if (!nrpnActive[ch]) return false;
        dataMsb[ch] = value;
        return emitNrpn (ch, dataMsb[ch] << 7);
      case 38:
        if (!nrpnActive[ch]) return false;
        return emitNrpn (ch, (dataMsb[ch] << 7) | value);

      case 96: case 97: {
        if (!nrpnActive[ch]) return false;
        var idInc = nrpnMsb[ch] === 0 ? nrpnToParam[nrpnLsb[ch]] : undefined;
        if (!idInc) return true;
        var cur = lastValue[idInc] !== undefined ? lastValue[idInc]
                                                 : Math.round (HOST.getNormalised (idInc) * 16383);
        return emitNrpn (ch, Math.max (0, Math.min (16383, cur + (cc === 96 ? 128 : -128))));
      }
      default: break;
    }

    var id = ccToParam[cc];
    if (id === undefined) return false;
    lastValue[id] = Math.round ((value / 127) * 16383);
    applyByIndex (id, value / 127);
    return true;
  }

  function emitNrpn (ch, v14) {
    if (nrpnMsb[ch] !== 0) return true;               // bank 0 only
    var id = nrpnToParam[nrpnLsb[ch]];
    if (!id) return true;                             // selector points at nothing
    lastValue[id] = v14;
    applyByIndex (id, v14 / 16383);
    return true;
  }

  // ---- the wire ----------------------------------------------------------
  function onMessage (e) {
    var d = e.data;
    if (!d || d.length === 0) return;
    var status = d[0] & 0xf0, ch = d[0] & 0x0f;
    if (d[0] >= 0xf0) return;                         // system messages are not ours
    if (channelFilter && (ch + 1) !== channelFilter) return;

    state.counted++;

    switch (status) {
      case 0x90:
        if (d[2] > 0) { HOST.note (d[1], d[2] / 127, true); state.notesDown++; }
        else { HOST.note (d[1], 0, false); state.notesDown = Math.max (0, state.notesDown - 1); }
        state.lastMessage = 'note ' + d[1] + ' vel ' + d[2] + '  ch ' + (ch + 1);
        break;
      case 0x80:
        HOST.note (d[1], 0, false);
        state.notesDown = Math.max (0, state.notesDown - 1);
        state.lastMessage = 'note off ' + d[1] + '  ch ' + (ch + 1);
        break;
      case 0xb0:
        handleController (ch, d[1], d[2]);
        state.lastMessage = 'CC ' + d[1] + ' = ' + d[2] + '  ch ' + (ch + 1)
                          + (ccToParam[d[1]] ? '  → ' + ccToParam[d[1]] : '');
        break;
      case 0xe0: {
        var bend = ((d[2] << 7) | d[1]) - 8192;
        HOST.pitchBend (bend / 8192 * 2);
        state.lastMessage = 'pitch bend ' + bend;
        break;
      }
      default: return;
    }
    changed();
  }

  // ---- devices -----------------------------------------------------------
  function refreshInputs () {
    if (!access) return;
    state.inputs = [];
    access.inputs.forEach (function (input) {
      state.inputs.push ({ id: input.id, name: input.name || input.id,
                           manufacturer: input.manufacturer || '',
                           on: enabled[input.id] !== false });
      input.onmidimessage = (enabled[input.id] !== false) ? onMessage : null;
    });
    changed();
  }

  function setInputEnabled (id, on) {
    enabled[id] = on;
    save ('midiInputs', enabled);
    refreshInputs();
  }

  function connect () {
    if (!navigator.requestMIDIAccess) {
      state.available = false;
      // Named plainly: Safari has never shipped Web MIDI, and telling
      // somebody their controller is broken would be worse than telling them
      // their browser cannot do it.
      state.reason = 'This browser has no Web MIDI. Chrome, Edge and Firefox do; Safari does not.';
      changed();
      return Promise.resolve (false);
    }
    return navigator.requestMIDIAccess ({ sysex: false }).then (function (a) {
      access = a;
      state.available = true;
      state.reason = '';
      a.onstatechange = refreshInputs;
      refreshInputs();
      return true;
    }).catch (function (err) {
      state.available = false;
      state.reason = 'MIDI access was refused (' + (err && err.message ? err.message : 'no reason given') + ').';
      changed();
      return false;
    });
  }

  // The message handler, reachable without a device. There is no MIDI port on
  // a CI machine and none in headless Chrome, but the decode is the part worth
  // testing and it is the same code whichever way a message arrives.
  global.__epiTestMidi = onMessage;

  global.__EPI_MIDI__ = {
    init: function (host) {
      HOST = host;
      enabled = load ('midiInputs', {});
      overrides = load ('ccOverrides', {});
      channelFilter = load ('midiChannel', 0);
      buildMap();
    },
    connect: connect,
    state: function () { return state; },
    onChange: function (fn) { listeners.push (fn); },
    setInputEnabled: setInputEnabled,
    setChannel: function (n) { channelFilter = n; save ('midiChannel', n); changed(); },
    channel: function () { return channelFilter; },
    // The CC that actually reaches this parameter. A learned binding
    // DISPLACES the published one it collides with, so the displaced
    // parameter has to report that it is no longer bound rather than keep
    // advertising a number that now answers something else.
    effectiveCc: function (id) {
      if (overrides[id] !== undefined) return overrides[id];
      var p = HOST.byId[id];
      if (!p || p.cc < 0) return undefined;
      return ccToParam[p.cc] === id ? p.cc : undefined;
    },
    // Who took it, so the panel can say so.
    shadowedBy: function (id) {
      var p = HOST.byId[id];
      if (!p || p.cc < 0 || overrides[id] !== undefined) return null;
      var owner = ccToParam[p.cc];
      return (owner && owner !== id) ? owner : null;
    },
    publishedCc: function (id) { return (HOST.byId[id] || {}).cc; },
    nrpnFor: function (id) { return (HOST.byId[id] || {}).nrpn; },
    isOverridden: function (id) { return overrides[id] !== undefined; },
    learn: function (id) { learnTarget = id; changed(); },
    learning: function () { return learnTarget; },
    cancelLearn: function () { learnTarget = null; changed(); },
    clearOverride: function (id) {
      delete overrides[id]; save ('ccOverrides', overrides); buildMap(); changed();
    },
    clearAllOverrides: function () {
      overrides = {}; save ('ccOverrides', overrides); buildMap(); changed();
    }
  };
}) (window);
