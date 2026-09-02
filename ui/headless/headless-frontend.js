/*
   Epi -- the browser side of the headless host.

   This is a drop-in replacement for juce-framework-frontend.js. The page
   asks for that filename and the headless server answers with this instead,
   so every other file the interface is made of is served byte-identical to
   the plugin's. Nothing in ui/epi knows which host it is talking to.

   What it has to provide is small and fixed by how the interface consumes
   it (see juce-bridge.jsx):

     __JUCE__.initialisationData.__juce__sliders   non-empty, or the
                                                   interface decides there is
                                                   no backend and mocks one
     Juce.getSliderState(id)    getNormalisedValue / setNormalisedValue
     Juce.getComboBoxState(id)  getChoiceIndex / setChoiceIndex
     Juce.getToggleState(id)    getValue / setValue
       ...each with a valueChangedEvent carrying addListener/removeListener
     Juce.backend               addEventListener / removeEventListener /
                                emitEvent
     Juce.getNativeFunction(n)  (...args) => Promise

   Two things about the transport are worth stating, because they are the
   whole design:

   getNormalisedValue is SYNCHRONOUS -- the interface calls it during render
   -- so nothing can be fetched on demand. Every value is held in a local
   cache, primed before this file runs by a block the server writes above
   it, and kept current by the event stream. A control the player moves
   updates the cache immediately and posts afterwards, so the knob never
   waits for the network to redraw.

   And the traffic is one-directional per channel: EventSource carries the
   host's telemetry and its parameter echoes, plain POSTs carry the player's
   edits. That needs no WebSocket handshake and no framing, which on a small
   board is a real saving in both code and dependencies.
*/
(function (global) {
  'use strict';

  // The server writes window.__EPI_INIT__ immediately above this file.
  var INIT = global.__EPI_INIT__ || { sliders: {}, combos: {}, toggles: {} };

  function listenerList() {
    var map = {}, next = 0;
    return {
      addListener: function (fn) { map[++next] = fn; return next; },
      removeListener: function (id) { delete map[id]; },
      fire: function () { for (var k in map) if (map.hasOwnProperty (k)) map[k](); }
    };
  }

  function post (path, body) {
    return fetch (path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify (body || {})
    });
  }

  var sliders = {}, combos = {}, toggles = {};

  function makeSlider (id, initial) {
    var v = initial, ev = listenerList();
    return {
      getNormalisedValue: function () { return v; },
      setNormalisedValue: function (n) {
        // Cache first, network second: a knob must follow the pointer at
        // display rate whatever the link is doing.
        v = n; ev.fire();
        post ('/api/set', { kind: 'slider', id: id, value: n });
      },
      // Used when the HOST is the one that moved it (a preset load), so it
      // must not echo back and start a loop.
      __fromHost: function (n) { if (n !== v) { v = n; ev.fire(); } },
      valueChangedEvent: ev,
      properties: { start: 0, end: 1, interval: 0, name: id, label: '', numSteps: 0 }
    };
  }

  function makeIndexed (id, initial, kind) {
    var i = initial, ev = listenerList();
    var o = {
      valueChangedEvent: ev,
      __fromHost: function (n) { if (n !== i) { i = n; ev.fire(); } },
      properties: { name: id, parameterIndex: 0 }
    };
    if (kind === 'combo') {
      o.getChoiceIndex = function () { return i; };
      o.setChoiceIndex = function (n) {
        i = n; ev.fire();
        post ('/api/set', { kind: 'combo', id: id, value: n });
      };
      o.properties.choices = [];
    } else {
      o.getValue = function () { return !!i; };
      o.setValue = function (b) {
        i = b ? 1 : 0; ev.fire();
        post ('/api/set', { kind: 'toggle', id: id, value: i });
      };
    }
    return o;
  }

  var k;
  for (k in INIT.sliders) if (INIT.sliders.hasOwnProperty (k)) sliders[k] = makeSlider (k, INIT.sliders[k]);
  for (k in INIT.combos)  if (INIT.combos.hasOwnProperty (k))  combos[k]  = makeIndexed (k, INIT.combos[k], 'combo');
  for (k in INIT.toggles) if (INIT.toggles.hasOwnProperty (k)) toggles[k] = makeIndexed (k, INIT.toggles[k], 'toggle');

  // ---- the event channel --------------------------------------------------
  var listeners = {};
  var backend = {
    addEventListener: function (n, f) { (listeners[n] = listeners[n] || []).push (f); },
    removeEventListener: function (n, f) {
      listeners[n] = (listeners[n] || []).filter (function (g) { return g !== f; });
    },
    // The interface emits these to ask the host to do something; they go out
    // as ordinary posts.
    emitEvent: function (n, payload) { post ('/api/emit/' + encodeURIComponent (n), payload || {}); }
  };
  function dispatch (n, p) {
    var l = listeners[n] || [];
    for (var i = 0; i < l.length; ++i) l[i] (p);
  }

  function connect () {
    var es = new EventSource ('/api/events');
    es.onmessage = function (m) {
      var msg;
      try { msg = JSON.parse (m.data); } catch (e) { return; }
      if (msg.t === 'p') {
        // A parameter the HOST moved. Update the cache without posting back.
        var reg = msg.kind === 'combo' ? combos : msg.kind === 'toggle' ? toggles : sliders;
        if (reg[msg.id]) reg[msg.id].__fromHost (msg.value);
      } else if (msg.t === 'e') {
        dispatch (msg.name, msg.payload);
      }
    };
    // A board that goes to sleep, a laptop lid, a dropped wifi link: the
    // page has to come back on its own rather than sit there looking alive
    // with a frozen meter.
    es.onerror = function () { es.close(); setTimeout (connect, 1000); };
  }
  connect();

  // ---- native functions ---------------------------------------------------
  function getNativeFunction (name) {
    return function () {
      var args = Array.prototype.slice.call (arguments);
      return post ('/api/native/' + encodeURIComponent (name), { args: args })
        .then (function (r) { return r.json(); })
        .then (function (j) { return j.result; })
        // A dropped link must look to the interface exactly like a host that
        // answered with nothing -- every caller already handles that -- rather
        // than an unhandled rejection.
        .catch (function () { return null; });
    };
  }

  global.__JUCE__ = {
    backend: backend,
    initialisationData: {
      // Non-empty is the signal juce-bridge.jsx reads to decide a real host
      // is behind it; an empty list means the mock.
      __juce__sliders: Object.keys (sliders),
      __juce__comboBoxes: Object.keys (combos),
      __juce__toggles: Object.keys (toggles),
      __juce__functions: INIT.functions || []
    },
    getAndroidUserScripts: function () { return ''; }
  };

  global.Juce = {
    getSliderState: function (id) {
      if (! sliders[id]) sliders[id] = makeSlider (id, 0);
      return sliders[id];
    },
    getComboBoxState: function (id) {
      if (! combos[id]) combos[id] = makeIndexed (id, 0, 'combo');
      return combos[id];
    },
    getToggleState: function (id) {
      if (! toggles[id]) toggles[id] = makeIndexed (id, 0, 'toggle');
      return toggles[id];
    },
    backend: backend,
    getNativeFunction: getNativeFunction
  };
  global.juce = global.Juce;
}) (window);
