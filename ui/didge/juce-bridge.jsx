/* ============================================================
   Didge · JUCE <-> React bridge
   ============================================================
   Hooks that read live state from APVTS via the JUCE 8
   WebSliderRelay API. Every Didge parameter is a float, so
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
    unit:       linPair(0, 1),
    attack:     skewPair(1, 500, 60),
    release:    skewPair(5, 2000, 200),
    decay:      skewPair(20, 4000, 400),
    vibRate:    skewPair(0.1, 12, 4),
    tension:    linPair(-12, 12),
    bendRange:  linPair(0, 24),
    growlPitch: linPair(0, 36),
    tune:       linPair(-100, 100),
    outGain:    linPair(-24, 12),
  };

  const VOWELS = ['oo', 'oh', 'ah', 'eh', 'ee'];
  /* Short forms for the segmented control — the host sees the full names
     from the parameter itself; these only have to fit the panel. */
  const VEL_TARGETS = ['Off', 'Breath', 'Br+Atk', 'Emb', 'Bright'];
  const BORE_PROFILES = ['Natural', 'Cylinder', 'Cone', 'Flared', 'Horn',
                         'Trumpet', 'Trombone', 'Flugelhorn', 'French Horn', 'Tuba',
                         'Alphorn', 'Contrabass'];
  /* Mirrors boreGeometryFor() in dsp/DidgeModel.h: parallel fraction, bell
     exponent, and the two end scalings. Only used to draw the cutaway in a
     plain browser; the plugin sends the engine's real radii. */
  const BORE_GEOM = {
    0:  [0.00, null, 1.00, 1.00, 0], 1: [1.00, 1.0, 1.00, 0.06, 0],
    2:  [0.00, 1.0,  1.00, 1.00, 0], 3: [0.00, null, 1.00, 1.00, 0],
    4:  [null, 1.6,  1.00, 1.00, 0],
    5:  [0.35, 4.0,  0.62, 0.85, 1], 6: [0.52, 3.6, 0.72, 1.05, 1],
    7:  [0.12, 2.2,  0.78, 1.00, 1], 8: [0.18, 3.2, 0.58, 1.25, 1],
    9:  [0.10, 2.4,  1.30, 1.45, 1], 10: [0.05, 1.15, 1.10, 1.20, 0],
    11: [0.08, 2.0,  1.55, 1.60, 1],
  };
  const CUP_R = 0.0085, THROAT_R = 0.0021;
  const MATERIALS = ['Wood', 'Bamboo', 'Brass', 'Steel', 'Glass'];
  // Order must match didge::Exciter in src/dsp/DidgeModel.h.
  const EXCITERS = ['Lips', 'Single Reed', 'Double Reed', 'Free Reed', 'Air Jet'];

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
    pressure:    { label: 'Breath',       map: M.unit,       def: 0.62,                    format: pct },
    attack:      { label: 'Attack',       map: M.attack,     def: M.attack.from(40),       format: msOf(M.attack) },
    release:     { label: 'Release',      map: M.release,    def: M.release.from(140),     format: msOf(M.release) },
    vibRate:     { label: 'Vib Rate',     map: M.vibRate,    def: M.vibRate.from(4.5),     format: hzOf(M.vibRate) },
    vibDepth:    { label: 'Vib Depth',    map: M.unit,       def: 0.0,                     format: pct },
    breathNoise: { label: 'Noise',        map: M.unit,       def: 0.25,                    format: pct },
    decay:       { label: 'Decay',        map: M.decay,      def: M.decay.from(500),       format: msOf(M.decay) },
    sustain:     { label: 'Sustain',      map: M.unit,       def: 0.0,                     format: pct },
    velAmount:   { label: 'Vel Amount',   map: M.unit,       def: 0.6,                     format: pct },
    humanize:    { label: 'Humanize',     map: M.unit,       def: 0.35,                    format: pct },

    tension:     { label: 'Lip Tension',  map: M.tension,    def: M.tension.from(0),       format: semi(M.tension), bipolar: true },
    lipDamp:     { label: 'Lip Damp',     map: M.unit,       def: 0.18,                    format: pct },
    embouchure:  { label: 'Embouchure',   map: M.unit,       def: 0.5,                     format: pct },
    bendRange:   { label: 'Bend Range',   map: M.bendRange,  def: M.bendRange.from(2),
                   format: (n) => M.bendRange.to(n).toFixed(1) + ' st' },

    tractMix:    { label: 'Voice',        map: M.unit,       def: 0.5,                     format: pct },
    vowelX:      { label: 'Vowel',        map: M.unit,       def: 0.35,
                   format: (n) => VOWELS[Math.max(0, Math.min(4, Math.round(n * 4)))] },
    vowelY:      { label: 'Mouth Open',   map: M.unit,       def: 0.5,                     format: pct },
    growl:       { label: 'Growl',        map: M.unit,       def: 0.0,                     format: pct },
    growlPitch:  { label: 'Growl Pitch',  map: M.growlPitch, def: M.growlPitch.from(19),
                   format: (n) => Math.round(M.growlPitch.to(n)) + ' st' },

    tune:        { label: 'Tune',         map: M.tune,       def: M.tune.from(0),          format: cent(M.tune), bipolar: true },
    bell:        { label: 'Bell',         map: M.unit,       def: 0.4,                     format: pct },
    flare:       { label: 'Flare',        map: M.unit,       def: 0.5,                     format: pct },
    texture:     { label: 'Texture',      map: M.unit,       def: 0.3,                     format: pct },
    wallDamp:    { label: 'Wall Damp',    map: M.unit,       def: 0.3,                     format: pct },
    boreDia:     { label: 'Bore',         map: M.unit,       def: 0.5,
                   format: (n) => (2 * 14 * Math.pow(2, 2 * (n - 0.5))).toFixed(1) + ' mm' },

    spaceMix:    { label: 'Space',        map: M.unit,       def: 0.18,                    format: pct },
    spaceSize:   { label: 'Size',         map: M.unit,       def: 0.4,                     format: pct },
    outGain:     { label: 'Output',       map: M.outGain,    def: M.outGain.from(0),       format: dbOf(M.outGain), bipolar: true },
  };

  /* ---- Browser mock (design/dev mode, no plugin backend) ----
     The JUCE frontend library defines a placeholder window.__JUCE__ when the
     native side didn't inject one, so its presence proves nothing. The real
     plugin registers its slider relays in initialisationData — an empty
     relay list means we're in a plain browser. */
  const nativeBackend = !!(global.__JUCE__
                           && global.__JUCE__.initialisationData
                           && (global.__JUCE__.initialisationData.__juce__sliders || []).length > 0);

  if (!nativeBackend) {
    const mkEvent = () => {
      const ls = [];
      return { addListener: (f) => ls.push(f), removeListener: () => {}, fire: (...a) => ls.forEach((f) => f(...a)) };
    };
    const sliders = {}, toggles = {}, combos = {};

    /* Vocal-tract area functions, glottis -> mouth, cm^2. Coarse eight-section
       approximations of the five vowels the model interpolates between. */
    const VOWEL_AREAS = [
      [1.5, 3.0, 4.6, 3.2, 1.2, 0.6, 0.5, 0.5],  // oo
      [1.6, 3.4, 4.9, 3.6, 1.8, 1.2, 1.4, 1.8],  // oh
      [1.2, 2.2, 1.6, 1.0, 1.6, 3.4, 5.6, 6.8],  // ah
      [1.6, 3.0, 2.6, 1.6, 1.8, 2.6, 3.6, 4.0],  // eh
      [1.4, 3.6, 6.2, 5.0, 1.0, 0.4, 0.6, 1.2],  // ee
    ];

    const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

    /* Stand-in for the C++ engine's telemetry. Derived from the live mock
       parameter values so editing a knob visibly reshapes the instrument —
       that is what makes headless verification of the UI meaningful. */
    function mockLevels(t) {
      const g = (id) => global.Juce.getSliderState(id).getNormalisedValue();

      const bell = g('bell'), flare = g('flare'), texture = g('texture');
      const rMouth = 0.0145;
      const rEnd = 0.030 + 0.050 * bell;
      const prof = global.Juce.getComboBoxState('boreProfile').getChoiceIndex();
      const geo = BORE_GEOM[prof] || BORE_GEOM[0];
      const cylFrac = geo[0] !== null ? geo[0] : 0.25 + 0.30 * (1 - flare);
      const flarePow = geo[1] !== null ? geo[1]
                     : (prof === 3 ? 0.55 + 0.5 * flare : 1 + 3 * flare);
      const rThroat = rMouth * geo[2];
      const rTip = Math.max(rThroat * 1.02, rEnd * geo[3]);
      // Brass profiles carry a mouthpiece: a wide cup narrowing to a tight
      // throat, occupying the first two segments (see setShape in the model).
      const cup = geo[4] === 1;
      const first = cup ? 2 : 0;
      const bore = [];
      if (cup) {
        bore.push(CUP_R * geo[2]);
        bore.push(THROAT_R * geo[2]);
      }
      for (let i = first; i < 16; i++) {
        const u0 = (i - first) / (15 - first);
        const u = cylFrac >= 0.999 ? 0 : Math.max(0, u0 - cylFrac) / (1 - cylFrac);
        let r = rThroat + (rTip - rThroat) * Math.pow(u, flarePow);
        r *= 1 + texture * 0.055 * u0 * Math.sin(u0 * 21 + 1.3);  // irregularity grows toward the bell
        bore.push(r);
      }

      const vx = clamp(g('vowelX'), 0, 1) * 4;
      const i0 = Math.min(4, Math.floor(vx)), i1 = Math.min(4, i0 + 1), fx = vx - i0;
      const open = 0.55 + 0.9 * g('vowelY');
      const tract = [];
      for (let k = 0; k < 8; k++) {
        const a = VOWEL_AREAS[i0][k] * (1 - fx) + VOWEL_AREAS[i1][k] * fx;
        tract.push(clamp(a * (k >= 5 ? open : 1), 0.4, 7));
      }

      const vibHz = M.vibRate.to(g('vibRate'));
      /* Phrased with real gaps, so the display can be checked for what it
         does when nothing is sounding as well as when something is. */
      const gate = (t % 9) < 6 ? 1 : 0;
      const phrase = gate * (0.72 + 0.28 * Math.sin(t * 0.6));
      const vib = 1 + g('vibDepth') * 0.35 * Math.sin(2 * Math.PI * vibHz * t);
      const growl = 1 - g('growl') * 0.5 * Math.abs(Math.sin(2 * Math.PI * 3.5 * t));
      const env = clamp(g('pressure') * phrase * vib * growl, 0, 1);

      const cents = M.tune.to(g('tune'));
      const st = M.tension.to(g('tension'));
      const f0 = 73.42 * Math.pow(2, cents / 1200) * Math.pow(2, st / 24);
      const toot = f0 * (2.6 + 0.5 * g('embouchure'));

      const amp = env * Math.pow(10, M.outGain.to(g('outGain')) / 20);
      const db = 20 * Math.log10(Math.max(1e-5, amp * 0.85));

      /* Standing wave along the bore. A didgeridoo is closed at the lips and
         open at the bell, so pressure has its antinode at the mouth and its
         node at the open end, and volume flow is the exact complement. The
         real engine measures these from the waveguide; the mock reproduces
         the same shapes so the drawing can be checked without a backend. */
      const tootOn = g('embouchure') > 0.7 || Math.sin(t * 0.35) > 0.85;
      const mode = tootOn ? 3 : 1;                    // quarter-wave harmonic
      const press = [], flowSeg = [];
      for (let i = 0; i < 16; i++) {
        const u = i / 15;
        press.push(Math.abs(Math.cos(u * mode * Math.PI / 2)) * env * 900);
        flowSeg.push(Math.abs(Math.sin(u * mode * Math.PI / 2)) * env * 4e-4 + 2e-5 * env);
      }

      /* Wave field: the same standing pattern, but as complex amplitudes, plus
         a travelling component so the reconstruction can be checked doing what
         it exists to do. A real bore is somewhere between the two -- perfectly
         standing only if nothing radiates -- so the mock mixes them, with the
         travelling share growing toward the bell where the radiation is.
         Interleaved real/imaginary, matching what WebEditor sends. */
      const waveP = [], waveD = [];
      for (let i = 0; i < 16; i++) {
        const u = i / 15;
        const k = mode * Math.PI / 2 * u;
        const trav = 0.55 * u * u;                    // radiating end lags
        const pA = Math.cos(k) * env * 900;
        const dA = Math.sin(k) * env * 1.4e-4;
        waveP.push(pA * Math.cos(trav * Math.PI), pA * Math.sin(trav * Math.PI));
        waveD.push(dA * Math.cos(trav * Math.PI - Math.PI / 2),
                   dA * Math.sin(trav * Math.PI - Math.PI / 2));
      }

      /* Output spectrum: 256 log-spaced display points from 40 Hz to 16 kHz,
         matching Spectrum::kBins and its range in dsp/Spectrum.h. The mock
         lays a harmonic series over a falling floor so the drawing, the peak
         hold and the partial tracking can all be checked without a backend. */
      const SPEC_N = 256, SPEC_LO = 40, SPEC_HI = 16000;
      const spec = [], specPk = [];
      const envDb = 20 * Math.log10(Math.max(1e-3, env));
      for (let i = 0; i < SPEC_N; i++) {
        const fb = SPEC_LO * Math.pow(SPEC_HI / SPEC_LO, i / (SPEC_N - 1));
        const hn = fb / f0;
        const near = Math.abs(hn - Math.round(hn));
        const nh = Math.round(hn);
        // Partials are narrow: a display point only lights up when it is
        // genuinely on one, which is the whole point of the finer resolution.
        const width = 0.055 + 0.004 * nh;
        const onPartial = nh >= 1 && near < width;
        const partial = onPartial ? 30 * (1 - near / width) - 2.2 * nh : 0;
        const floor = -74 - 9 * Math.log2(Math.max(1, fb / 200));
        const v = Math.max(-108, floor + partial + envDb + 26);
        spec.push(Math.round(v));
        specPk.push(Math.round(v + 1.5));
      }

      const peaks = [];
      for (let h = 1; h <= 6; h++) {
        peaks.push({ f: f0 * h * (1 + 0.0008 * Math.sin(t + h)),
                     db: -34 - 2.4 * h + envDb + 26 });
      }

      /* Lip opening over ~3 periods. The lips beat shut for part of every
         cycle — that closed phase is what makes the tone buzz, so the trace
         is a clipped swing, never a sine. */
      const lipWave = [];
      for (let i = 0; i < 96; i++) {
        const ph = (i / 96) * 3 * 2 * Math.PI;
        lipWave.push(Math.max(0, 0.0016 * env * (Math.sin(ph) + 0.35)));
      }

      return {
        out: [db, db - 0.6 - 0.4 * Math.sin(t * 1.7)],
        press, flowSeg, waveP, waveD, lipWave, spec, specPk, peaks,
        meanFlow: 2.2e-4 * env,
        turb: g('breathNoise') * env,
        pressure: env,
        lipOpen: 0.004 * env * (0.4 + 0.6 * (1 - g('lipDamp'))) * (0.7 + 0.3 * g('embouchure')),
        flow: 0.001 * env * (0.5 + 0.5 * (1 - g('lipDamp'))),
        f0, toot,
        tootActive: tootOn,
        playing: env > 0.02,
        bore, tract,
      };
    }

    global.Juce = {
      getSliderState: (id) => sliders[id] || (sliders[id] = (() => {
        const spec = PARAMS[id];
        let v = spec ? spec.def : 0.5;
        const ev = mkEvent();
        return {
          getNormalisedValue: () => v,
          setNormalisedValue: (nv) => { v = nv; ev.fire(); },
          valueChangedEvent: ev,
          sliderDragStarted: () => {}, sliderDragEnded: () => {},
        };
      })()),
      backend: (() => {
        const listeners = {};
        let t = 0;
        setInterval(() => {
          t += 1 / 60;
          (listeners['levels'] || []).forEach((cb) => cb(mockLevels(t)));
          (listeners['presetInfo'] || []).forEach((cb) => cb({ name: 'Deep Drone', dirty: false }));
        }, 16);
        return {
          addEventListener: (name, cb) => { (listeners[name] = listeners[name] || []).push(cb); },
          removeEventListener: (name, cb) => {
            const a = listeners[name] || [];
            const i = a.indexOf(cb);
            if (i >= 0) a.splice(i, 1);
          },
          emitEvent: () => {},
        };
      })(),
      getToggleState: (id) => toggles[id] || (toggles[id] = (() => {
        let v = false;
        const ev = mkEvent();
        return { getValue: () => v, setValue: (nv) => { v = !!nv; ev.fire(); }, valueChangedEvent: ev };
      })()),
      getComboBoxState: (id) => combos[id] || (combos[id] = (() => {
        let idx = id === 'velTarget' ? 1 : 0;
        const ev = mkEvent();
        return { getChoiceIndex: () => idx, setChoiceIndex: (ni) => { idx = ni; ev.fire(); }, valueChangedEvent: ev };
      })()),
      getNativeFunction: (name) => {
        if (name === 'listFactoryPresets')
          return () => Promise.resolve(['Deep Drone', 'Yidaki', 'Termite Bore', 'Circular Breath',
                                        'Rhythm Machine', 'Growl Beast', 'Wobble Bass', 'High Mago',
                                        'Wide Bell', 'Dry & Close']);
        if (name === 'listUserPresets') return () => Promise.resolve([]);
        return () => Promise.resolve();
      },
    };
    global.__DIDGE_MOCK__ = true;
  }

  /* ---- Event emit to native (no-op under the mock) ---- */
  function emitNative(name, payload) {
    try {
      if (global.__JUCE__ && global.__JUCE__.backend)
        global.__JUCE__.backend.emitEvent(name, payload || {});
    } catch (_) {}
  }

  /* ---- Hooks ---- */
  function useJuceSlider(id) {
    const relay = global.Juce.getSliderState(id);
    const [v, setV] = useState(relay.getNormalisedValue());
    useEffect(() => {
      const onChanged = () => setV(relay.getNormalisedValue());
      relay.valueChangedEvent.addListener(onChanged);
      return undefined;
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
      const onChanged = () => setV(!!relay.getValue());
      relay.valueChangedEvent.addListener(onChanged);
      return undefined;
    }, [id]);
    const set = useCallback((b) => { relay.setValue(!!b); setV(!!b); }, [id]);
    return [v, set];
  }

  function useJuceChoice(id, options) {
    const relay = global.Juce.getComboBoxState(id);
    const [idx, setIdx] = useState(relay.getChoiceIndex());
    useEffect(() => {
      const onChanged = () => setIdx(relay.getChoiceIndex());
      relay.valueChangedEvent.addListener(onChanged);
      return undefined;
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
                        emitNative, PARAMS, VOWELS, VEL_TARGETS,
                        BORE_PROFILES, MATERIALS, EXCITERS, M };
  global.PARAMS = PARAMS;
  global.VOWELS = VOWELS;
  global.VEL_TARGETS = VEL_TARGETS;
  global.BORE_PROFILES = BORE_PROFILES;
  global.MATERIALS = MATERIALS;
  global.EXCITERS = EXCITERS;
})(window);
