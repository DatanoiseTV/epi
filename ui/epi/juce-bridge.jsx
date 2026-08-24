/* ============================================================
   Epi · JUCE <-> React bridge
   ============================================================
   Hooks that read live state from APVTS via the JUCE 8
   WebSliderRelay API. Every Epi parameter is a float, so
   WebSliderRelay is the only relay kind in use — there are no
   toggle or combo relays on the C++ side.

   Two-way bound: the host can drive any control (automation,
   preset load) and the UI reflects it; user edits write back
   through the relay so the model hears them.

   When there is no native backend (opened in a plain browser
   for design work / headless UI verification), a mock Juce
   object stands in and a small kinematic model fabricates the
   `levels` feed, so the whole UI runs with believable state.
   ============================================================ */

(function (global) {
  const { useState, useEffect, useCallback } = React;

  /* ---- Parameter range maps (mirror src/ParameterIDs.h) ----
     Relay values are always normalised 0..1; `to` denormalises to the
     displayed unit, `from` is its inverse (used to express the C++
     defaults in real units instead of pre-computed magic numbers). */
  function linPair(min, max) {
    return { to: (n) => min + (max - min) * n,
             from: (v) => (v - min) / (max - min) };
  }
  // JUCE's NormalisableRange::setSkewForCentre places `centre` at n = 0.5.
  function skewPair(min, max, centre) {
    const skew = Math.log(0.5) / Math.log((centre - min) / (max - min));
    return { to: (n) => min + (max - min) * Math.pow(n, 1 / skew),
             from: (v) => Math.pow((v - min) / (max - min), skew) };
  }

  const M = {
    unit:      linPair(0, 1),
    tune:      linPair(-100, 100),
    barTune:   linPair(-24, 24),
    tremRate:  skewPair(0.1, 12, 4),
    phaserRate: skewPair(0.02, 8, 0.8),
    db12:      linPair(-12, 12),
    outGain:   linPair(-24, 12),
    pickupPos: linPair(-1, 1),
  };

  /* Choice lists. Order must match the C++ StringArrays in
     src/epi/ParameterIDs.h. */
  // Must match epi::ids::instrumentNames.
  const INSTRUMENTS = ['Tine', 'E-Grand', 'Reed', 'Grand', 'Clav'];
  const PICKUP_SEL  = ['MAGNETIC', 'NATIVE', 'ELECTRO', 'CONTACT'];
  const BODY_MATERIALS = ['STOCK', 'SPRUCE', 'MAPLE', 'BIRCH PLY',
                          'ALUMINIUM', 'STEEL', 'BRASS', 'CARBON'];
  const MATERIALS   = ['MUSIC WIRE', 'STAINLESS', 'BRONZE', 'BRASS',
                       'TITANIUM', 'ALUMINIUM', 'TUNGSTEN', 'NYLON'];
  /* Transducer facts per material: which pickups can hear it at all. */
  const MAT_FERRO   = [true, true, false, false, false, false, false, false];
  const MAT_COND    = [true, true, true,  true,  true,  true,  true,  false];

  const pct  = (n) => Math.round(n * 100) + '%';
  const msOf = (m) => (n) => { const v = m.to(n); return (v < 10 ? v.toFixed(1) : Math.round(v)) + ' ms'; };
  const hzOf = (m) => (n) => { const v = m.to(n); return (v < 1 ? v.toFixed(2) : v.toFixed(1)) + ' Hz'; };
  const semi = (m) => (n) => { const v = m.to(n); return (v > 0 ? '+' : '') + v.toFixed(1) + ' st'; };
  const cent = (m) => (n) => { const v = Math.round(m.to(n)); return (v > 0 ? '+' : '') + v + ' ct'; };
  const dbOf = (m) => (n) => { const v = m.to(n); return (v > 0 ? '+' : '') + v.toFixed(1) + ' dB'; };

  /* The whole parameter contract in one table: label, range map, default
     (written in display units, normalised here) and display formatter.
     Panels, knob defaults and the browser mock all read from this. */
  const PARAMS = {
    tune:        { label: 'Tune',      map: M.tune,      def: M.tune.from(0),     format: cent(M.tune), bipolar: true },

    velCurve:    { label: 'Vel Curve', map: M.unit,      def: 0.50, format: pct },
    hammerHard:  { label: 'Hammer',    map: M.unit,      def: 0.50, format: pct },
    hammerMass:  { label: 'Tip Mass',  map: M.unit,      def: 0.50, format: pct },
    escapement:  { label: 'Escapement',map: M.unit,      def: 0.40, format: pct },
    strikeNoise: { label: 'Key Noise', map: M.unit,      def: 0.22, format: pct },
    damperGrip:  { label: 'Damper',    map: M.unit,      def: 0.60, format: pct },

    tipMass:     { label: 'Spring',    map: M.unit,      def: 0.50, format: pct },
    resDamp:     { label: 'Damping',   map: M.unit,      def: 0.35, format: pct },
    barCouple:   { label: 'Tone Bar',  map: M.unit,      def: 0.60, format: pct },
    barTune:     { label: 'Bar Tune',  map: M.barTune,   def: M.barTune.from(0),  format: semi(M.barTune), bipolar: true },
    bodyMix:     { label: 'Body',      map: M.unit,      def: 0.25, format: pct },
    nonlinAmt:   { label: 'Bloom',     map: M.unit,      def: 0.50, format: pct },

    /* The voicing screw. Shown in millimetres because that is what it is:
       the adjustment range on the instrument is about two millimetres either
       side of the pole centreline. */
    pickupPos:   { label: 'Height',    map: M.pickupPos, def: M.pickupPos.from(-0.35),
                   format: (n) => (M.pickupPos.to(n) * 2).toFixed(2) + ' mm', bipolar: true },
    pickupDist:  { label: 'Gap',       map: M.unit,      def: 0.35,
                   // Tracks staticGap in RhodesVoice::configure.
                   format: (n) => (0.6 + 4.4 * n).toFixed(2) + ' mm' },
    coilFreq:    { label: 'Coil Peak', map: M.unit,      def: 0.50,
                   format: (n) => Math.round(900 + 5600 * n) + ' Hz' },
    coilQ:       { label: 'Coil Q',    map: M.unit,      def: 0.50, format: pct },
    coilSat:     { label: 'Core Sat',  map: M.unit,      def: 0.25, format: pct },

    preampDrive: { label: 'Drive',     map: M.unit,      def: 0.30, format: pct },
    bass:        { label: 'Bass',      map: M.db12,      def: M.db12.from(0),     format: dbOf(M.db12), bipolar: true },
    treble:      { label: 'Treble',    map: M.db12,      def: M.db12.from(0),     format: dbOf(M.db12), bipolar: true },
    tremRate:    { label: 'Rate',      map: M.tremRate,  def: M.tremRate.from(5.5), format: hzOf(M.tremRate) },
    tremDepth:   { label: 'Tremolo',   map: M.unit,      def: 0.00, format: pct },
    tremStereo:  { label: 'Width',     map: M.unit,      def: 1.00,
                   format: (n) => (n > 0.99 ? 'Pan' : n < 0.01 ? 'Amp' : Math.round(n * 100) + '% pan') },
    phaserMix:   { label: 'Phaser',    map: M.unit,      def: 0.00, format: pct },
    phaserRate:  { label: 'Ph Rate',   map: M.phaserRate, def: M.phaserRate.from(0.40),
                   format: (n) => M.phaserRate.to(n).toFixed(2) + ' Hz' },
    phaserDepth: { label: 'Ph Depth',  map: M.unit,      def: 0.70, format: pct },
    phaserFb:    { label: 'Ph Res',    map: M.unit,      def: 0.50, format: pct },
    cabMix:      { label: 'Cabinet',   map: M.unit,      def: 0.50, format: pct },

    clarity:     { label: 'Clarity',   map: M.db12,      def: M.db12.from(0),     format: dbOf(M.db12), bipolar: true },
    clavBrill:   { label: 'Brilliant', map: M.unit, def: 0, format: (x) => x >= 0.5 ? 'ON' : 'OFF' },
    clavTreb:    { label: 'Treble',    map: M.unit, def: 0, format: (x) => x >= 0.5 ? 'ON' : 'OFF' },
    clavMed:     { label: 'Medium',    map: M.unit, def: 1, format: (x) => x >= 0.5 ? 'ON' : 'OFF' },
    clavSoft:    { label: 'Soft',      map: M.unit, def: 0, format: (x) => x >= 0.5 ? 'ON' : 'OFF' },
    bodySize:    { label: 'Body Size', map: M.unit, def: 0.5, format: (x) => Math.pow(1.43, 2 * x - 1).toFixed(2) + 'x' },
    // Tangent-rubber wear: mint at zero, gigged-hard at one. Shown as the
    // condition rather than a percentage, because that is how the knob
    // reads on a real instrument.
    wearAmount:  { label: 'Wear',      map: M.unit, def: 0.0,
                   format: (x) => x < 0.02 ? 'MINT'
                                : x < 0.35 ? 'PLAYED ' + Math.round(x * 100) + '%'
                                : x < 0.75 ? 'WORN ' + Math.round(x * 100) + '%'
                                : 'NOTCHED ' + Math.round(x * 100) + '%' },
    spaceMix:    { label: 'Space',     map: M.unit,      def: 0.15, format: pct },
    spaceSize:   { label: 'Size',      map: M.unit,      def: 0.40, format: pct },
    outGain:     { label: 'Output',    map: M.outGain,   def: M.outGain.from(0),  format: dbOf(M.outGain), bipolar: true },
  };

  /* ---- Browser mock -------------------------------------------------------
     The JUCE frontend library defines a placeholder window.__JUCE__ even with
     no plugin behind it, so its presence proves nothing -- an EMPTY relay list
     is the real tell. With no backend, this stands in for one so the interface
     can be designed and checked in a plain browser. */
  const nativeBackend = !!(global.__JUCE__ && global.__JUCE__.initialisationData
                           && (global.__JUCE__.initialisationData.__juce__sliders || []).length > 0);

  if (!nativeBackend) {
    global.__EPI_MOCK__ = true;
    const sliders = {}, combos = {};
    const mk = () => {
      const ls = new Map(); let nid = 0;
      return { addListener: (f) => { ls.set(++nid, f); return nid; },
               removeListener: (i) => { ls.delete(i); },
               fire: () => ls.forEach((f) => f()) };
    };

    Object.keys(PARAMS).forEach((id) => {
      let v = PARAMS[id].def;
      const ev = mk();
      sliders[id] = { getNormalisedValue: () => v, setNormalisedValue: (n) => { v = n; ev.fire(); }, valueChangedEvent: ev };
    });
    [['pickupSel', 1], ['instrument', 0], ['material', 0], ['clavSwitch', 0], ['bodyMat', 0], ['damperFelt', 0], ['keyBed', 0], ['hammerMat', 0], ['roomProfile', 0], ['softMode', 0]].forEach(([id, d]) => {
      let i = d; const ev = mk();
      combos[id] = { getChoiceIndex: () => i, setChoiceIndex: (n) => { i = n; ev.fire(); }, valueChangedEvent: ev };
    });

    const listeners = {};
    const backend = {
      addEventListener: (n, f) => { (listeners[n] = listeners[n] || []).push(f); },
      removeEventListener: (n, f) => { listeners[n] = (listeners[n] || []).filter((g) => g !== f); },
      emitEvent: () => {},
    };
    const emit = (n, p) => (listeners[n] || []).forEach((f) => f(p));

    /* The pickup's field, computed the same way the C++ does: a Coulombian
       integral over a wedge-shaped pole face. Drawing a made-up bell curve
       here would make the interface lie about the model. */
    const FIELD_N = 96;
    const field = (() => {
      const half = 3.0e-3, flat = 0.5e-3, depth = 1.6e-3, gap = 1.5e-3, span = 4 * half;
      const at = (v) => {
        let sum = 0; const steps = 96, dv = 2 * half / steps;
        for (let k = 0; k < steps; k++) {
          const x = -half + (k + 0.5) * dv;
          const a = Math.abs(x);
          const gs = a <= flat ? 0 : depth * Math.min(1, (a - flat) / (half - flat));
          const dz = gap + gs, d = v - x, r2 = d * d + dz * dz;
          sum += dz / Math.max(1e-15, r2 * Math.sqrt(r2)) * dv;
        }
        return sum;
      };
      const ref = at(0), out = [];
      for (let i = 0; i < FIELD_N; i++) out.push(at(-span + 2 * span * i / (FIELD_N - 1)) / ref);
      return out;
    })();

    const TRACE_N = 128;
    let t = 0, env = 0, phase = 0, strikes = 0;
    setInterval(() => {
      t += 0.016;
      const trig = (t % 2.4) < 0.02;
      if (trig) { env = 1; strikes++; }
      env *= 0.985;
      const f0 = 82;
      phase += 2 * Math.PI * (f0 / 22) * 0.016;
      const off = sliders.pickupPos.getNormalisedValue() * 2 - 1;
      const tip = 0.55 * env * Math.sin(phase);
      const depth = sliders.tremDepth.getNormalisedValue();
      const rate = 0.1 + 11.9 * sliders.tremRate.getNormalisedValue();
      const lfo = 0.5 + 0.5 * Math.sin(2 * Math.PI * rate * t * 0.35);
      /* Four cycles of tine motion, as the engine sends. Slightly clipped at
         the peaks, which is what a hard-struck tine actually looks like. */
      const trace = [];
      for (let i = 0; i < TRACE_N; i++) {
        const a = 2 * Math.PI * 4 * i / TRACE_N + phase;
        trace.push(env * (Math.sin(a) + 0.10 * Math.sin(2 * a + 0.6)) * 2.2e-3);
      }

      /* The whole harp: a held chord plus a little sympathetic movement in
         everything else, since with the pedal down the frame shakes the lot.
         Swing falls with pitch the way it does on the instrument, which is
         what the drawing is there to show. */
      const harp = [];
      const chord = [19, 26, 31, 38, 45];
      /* The same chord held down, packed as the engine packs it. */
      const mockKeys = [0, 0, 0];
      chord.forEach((i) => { mockKeys[i >> 5] |= (1 << (i & 31)); });
      for (let i = 0; i < 88; i++) {
        const near = 1 - i / 87;
        /* The measured sympathetic hierarchy at full BODY: octave partner
           -19 dB under the struck string, twelfth -26 dB, everything else
           a -38 dB wash. The mock shows the ratios the engine produces. */
        let own = 0.012 + 0.006 * Math.abs(Math.sin(i * 1.7 + t));
        if (chord.includes(i)) own = 1;
        else if (chord.includes(i - 12)) own = 0.11;
        else if (chord.includes(i - 19)) own = 0.05;
        harp.push(env * own * (0.06 + 0.94 * near * near) * 3000);
      }

      emit('levels', {
        out: [-90 + 84 * env, -90 + 84 * env],
        field, tip, offset: off, trace, noteHz: f0, strikes,
        harp, keys: mockKeys, pedal: true, lastNote: 21 + 31, loNote: 21,
        flux: tip, voices: env > 0.02 ? 4 : 0,
        vibL: 1 - depth * (1 - lfo), vibR: 1 - depth * lfo,
      });
      emit('presetInfo', { name: 'Suitcase', dirty: false });
    }, 16);

    global.Juce = {
      getSliderState: (id) => {
        if (!sliders[id]) throw new Error('mock: unknown slider id ' + id);
        return sliders[id];
      },
      getToggleState: () => ({ getValue: () => false, setValue: () => {}, valueChangedEvent: mk() }),
      getComboBoxState: (id) => {
        if (!combos[id]) throw new Error('mock: unknown combo id ' + id);
        return combos[id];
      },
      backend,
      getNativeFunction: (n) => () => Promise.resolve(
        n === 'listFactoryPresets'
          ? ['Suitcase', 'Bell', 'Mellow', 'Dirty Bass', 'Ballad', 'Funk', 'Glass Tine', 'Detuned Bar']
          : n === 'getTineMods'
          ? Array.from({ length: 176 }, () => 1)
          : n === 'getPickupMods'
          ? Array.from({ length: 264 }, (_, k) => (k % 3 === 2 ? 1 : 0))
          : n === 'getStringMods'
          ? Array.from({ length: 176 }, () => 1)
          : n === 'getCabMods'
          ? [0.74, 0.59, 0.5, 0.25, 0.5]
          : n === 'getMicMods'
          ? [1, 0, 0, 1, 1]
          : n === 'getMicStage'
          ? [0,
             1, -0.5, 1.2, 0.6, 0, -0.7,
             1,  0.5, 1.2, 0.6, 0,  0.7,
             0,  0.0, 2.5, 1.0, 0,  0.0,
             0, -1.2, 0.4, 0.3, -6, -1,
             0,  1.2, 0.4, 0.3, -6,  1]
          : n === 'getVelMap'
          ? [0, 0.25, 0.5, 0.75, 1]
          : n === 'getGrandMods'
          ? Array.from({ length: 176 }, () => 1)
          : []),
    };
  }

  /* UI -> C++, fire and forget. Guarded because the same page runs in a plain
     browser with no backend behind it. */
  function emitNative(name, payload) {
    try {
      if (global.__JUCE__ && global.__JUCE__.backend)
        global.__JUCE__.backend.emitEvent(name, payload || {});
    } catch (_) {}
  }

  function useJuceSlider(id) {
    const relay = global.Juce.getSliderState(id);
    const [v, setV] = useState(relay.getNormalisedValue());
    useEffect(() => {
      /* The listener MUST come off again. Panels remount on every
         instrument switch and every workshop open, and a listener left on
         the relay keeps firing into unmounted components forever -- after a
         session of switching, every parameter change fanned out to dozens
         of dead closures. */
      const lid = relay.valueChangedEvent.addListener(() => setV(relay.getNormalisedValue()));
      return () => relay.valueChangedEvent.removeListener(lid);
    }, [id]);
    const set = useCallback((nv) => {
      const c = Math.max(0, Math.min(1, nv));
      relay.setNormalisedValue(c);
      setV(c);
    }, [id]);
    return [v, set];
  }

  // Native events emitted by the editor on its 30 Hz UI timer.
  function useJuceEvent(name, initial) {
    const [v, setV] = useState(initial);
    useEffect(() => {
      const cb = (e) => setV(e);
      global.Juce.backend.addEventListener(name, cb);
      return () => global.Juce.backend.removeEventListener(name, cb);
    }, [name]);
    return v;
  }

  /* Subscribes a ref to an event without re-rendering — for anything that
     paints from a rAF loop at display rate (meters, the instrument view). */
  function useJuceToggle(id) {
    const relay = global.Juce.getToggleState(id);
    const [v, setV] = useState(!!relay.getValue());
    useEffect(() => {
      const lid = relay.valueChangedEvent.addListener(() => setV(!!relay.getValue()));
      return () => relay.valueChangedEvent.removeListener(lid);
    }, [id]);
    const set = useCallback((b) => { relay.setValue(!!b); setV(!!b); }, [id]);
    return [v, set];
  }

  function useJuceChoice(id, options) {
    const relay = global.Juce.getComboBoxState(id);
    const [idx, setIdx] = useState(relay.getChoiceIndex());
    useEffect(() => {
      const lid = relay.valueChangedEvent.addListener(() => setIdx(relay.getChoiceIndex()));
      return () => relay.valueChangedEvent.removeListener(lid);
    }, [id]);
    const setIndex = useCallback((ni) => {
      const c = Math.max(0, Math.min(options.length - 1, ni));
      relay.setChoiceIndex(c);
      setIdx(c);
    }, [id, options]);
    return [idx, setIndex];
  }

  function useEventRef(name, initial) {
    const ref = React.useRef(initial);
    useEffect(() => {
      const cb = (e) => { if (e) ref.current = e; };
      global.Juce.backend.addEventListener(name, cb);
      return () => global.Juce.backend.removeEventListener(name, cb);
    }, [name]);
    return ref;
  }

  global.JuceBridge = { useJuceSlider, useJuceToggle, useJuceChoice, useJuceEvent, useEventRef,
                        emitNative, PARAMS, INSTRUMENTS, PICKUP_SEL, MATERIALS, BODY_MATERIALS, MAT_FERRO, MAT_COND, M };
  global.PARAMS = PARAMS;
  global.INSTRUMENTS = INSTRUMENTS;
  global.PICKUP_SEL = PICKUP_SEL;
  global.MATERIALS = MATERIALS;
  global.BODY_MATERIALS = BODY_MATERIALS;
  global.FELTS = ['STOCK', 'FRESH', 'WORN', 'HARDENED'];
  global.KEYBEDS = ['STOCK', 'FRESH FELT', 'LEATHER', 'WORN'];
  global.HAMMERS = ['STOCK', 'SOFT FELT', 'HARD FELT', 'LACQUERED', 'LEATHER', 'WOOD'];
  // Room.h profiles: CUSTOM is the shipped size-mapped room; the rest are
  // surveyed spaces with Eyring decays from published absorption data.
  global.ROOMS = ['CUSTOM', 'BOOTH', 'STUDIO', 'STAGE', 'HALL', 'CHURCH'];
  global.MAT_FERRO = MAT_FERRO;
  global.MAT_COND = MAT_COND;
})(window);
