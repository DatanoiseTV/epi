/* ============================================================
   Didge · InstrumentView — the live cutaway
   ============================================================
   A side-view section through the instrument: vocal tract on the
   left, lips, then the bore opening out to the bell on the right.

   What is drawn is the engine's own state, not an illustration of
   it. The bore outline comes from the waveguide's 16 segment
   radii; the standing wave comes from the pressure and volume
   flow measured at those same segment boundaries; the lips move
   on a captured trace of the actual lip opening. So the nodes sit
   where the model really puts them, and the air moves the way the
   model says it moves.

   Everything animates from a single rAF loop that mutates SVG
   attributes in place. The 60 Hz telemetry never touches React
   state — a re-render per event would repaint the whole panel
   tree and fight the animation.
   ============================================================ */

/* ---- drawing frame (SVG user units) ---- */
const VB_W = 1280, VB_H = 296, CY = 148;
const TRACT_X0 = 58, TRACT_X1 = 200;   // glottis -> mouth
const LIP_X = 214, LIP_W = 32;
const BORE_X0 = 252, BORE_X1 = 1128;
const BORE_SPAN = BORE_X1 - BORE_X0;
/* Bore radii arrive in metres. The scale is fixed rather than fitted to the
   current bore, so growing the bell actually grows the drawing, and a
   didgeridoo keeps roughly its real proportions (about eight times longer
   than the bell is wide). */
const M_PX = 1500;
const CM_PX = 26;                      // tract radii are centimetres
const WALL = 9;                        // drawn wall thickness
const MAX_HALF = VB_H / 2 - 22;

/* Boundaries of the two draggable regions, as a fraction of bore length. */
const FLARE_ZONE = [0.20, 0.68];
const BELL_ZONE = [0.68, 1.0];

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
function hzToNote(hz) {
  if (!(hz > 0)) return '—';
  const m = Math.round(69 + 12 * Math.log2(hz / 440));
  return NOTE_NAMES[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1);
}

/* Catmull-Rom through the segment samples, emitted as cubic beziers — the
   bore is only sampled 16 times and a polyline reads as a faceted cone. */
function smoothPath(pts, lead) {
  if (!pts.length) return '';
  let d = (lead === false ? ' L ' : 'M ') + pts[0][0].toFixed(1) + ' ' + pts[0][1].toFixed(1);
  for (let i = 0; i < pts.length - 1; i++) {
    const p0 = pts[i - 1] || pts[i], p1 = pts[i], p2 = pts[i + 1], p3 = pts[i + 2] || pts[i + 1];
    const c1x = p1[0] + (p2[0] - p0[0]) / 6, c1y = p1[1] + (p2[1] - p0[1]) / 6;
    const c2x = p2[0] - (p3[0] - p1[0]) / 6, c2y = p2[1] - (p3[1] - p1[1]) / 6;
    d += ` C ${c1x.toFixed(1)} ${c1y.toFixed(1)} ${c2x.toFixed(1)} ${c2y.toFixed(1)} ${p2[0].toFixed(1)} ${p2[1].toFixed(1)}`;
  }
  return d;
}

/* A closed tube outline: forward along the top edge, across the open end,
   back along the bottom edge. */
function tubePath(xs, hs, cy) {
  const top = xs.map((x, i) => [x, cy - hs[i]]);
  const bot = xs.map((x, i) => [x, cy + hs[i]]).reverse();
  return smoothPath(top) + smoothPath(bot, false) + ' Z';
}

const DEFAULT_BORE = [0.0145, 0.0146, 0.0151, 0.0158, 0.0168, 0.0182, 0.0198, 0.0218,
                      0.0242, 0.0268, 0.0298, 0.0331, 0.0367, 0.0407, 0.0450, 0.0500];
const DEFAULT_TRACT = [1.44, 2.92, 3.58, 2.56, 1.72, 2.08, 3.08, 3.80];
const ZERO16 = new Array(16).fill(0);
const ZERO96 = new Array(96).fill(0);
/* Must match Spectrum::kBins and its range in dsp/Spectrum.h. */
const SPEC_N = 256, SPEC_LO = 40, SPEC_HI = 16000, SPEC_DB = -104;
const SPEC_FLOOR = new Array(SPEC_N).fill(SPEC_DB);
const specX = (f) => SPEC_X0 + (Math.log(f / SPEC_LO) / Math.log(SPEC_HI / SPEC_LO)) * (SPEC_X1 - SPEC_X0);

const N_PARTICLES = 130;

/* A strip along the bottom, clear of the bore's lower wall, wide enough that
   256 display points each get their own few pixels. Drawn full-panel behind
   the instrument it read as an opaque silhouette competing with the cutaway
   rather than as a readout. */
const SPEC_X0 = 250, SPEC_X1 = 1244, SPEC_Y0 = 232, SPEC_Y1 = 290;
const ZERO32 = new Array(32).fill(0);

/* The wave field arrives as one complex amplitude per segment boundary, real
   and imaginary interleaved. Sampling it between boundaries has to interpolate
   the real and imaginary parts separately: interpolating magnitude alone would
   throw away the phase, which is exactly the part that makes a wave travel.
   Returns the value of that field at time `phase`. */
function fieldAt(arr, u, cosP, sinP) {
  const n = arr.length >> 1;
  if (n < 2) return 0;
  const t = Math.max(0, Math.min(1, u)) * (n - 1);
  const i = Math.min(n - 2, Math.floor(t));
  const f = t - i;
  const re = arr[2 * i] * (1 - f) + arr[2 * i + 2] * f;
  const im = arr[2 * i + 1] * (1 - f) + arr[2 * i + 3] * f;
  return re * cosP - im * sinP;
}

function fieldPeak(arr) {
  let m = 1e-12;
  for (let i = 0; i < arr.length; i += 2) {
    const a = Math.hypot(arr[i], arr[i + 1]);
    if (a > m) m = a;
  }
  return m;
}

function InstrumentView({ bell, setBell, flare, setFlare, texture = 0.3, tractMix = 0.5, wallDamp = 0.3 }) {
  const lv = JuceBridge.useEventRef('levels', {
    out: [-90, -90], pressure: 0, lipOpen: 0, flow: 0,
    f0: 73.42, toot: 199, tootActive: false, playing: false,
    bore: DEFAULT_BORE, tract: DEFAULT_TRACT,
    press: ZERO16, flowSeg: ZERO16, lipWave: ZERO96, meanFlow: 0, turb: 0,
    waveP: ZERO32, waveD: ZERO32,
    spec: SPEC_FLOOR, specPk: SPEC_FLOOR, peaks: [],
  });

  /* Params the loop reads without re-subscribing. */
  const P = useRef({});
  P.current = { texture, tractMix, wallDamp, bell, flare };

  const woodRef = useRef(null);
  const cavityRef = useRef(null);
  const innerEdgeRef = useRef(null);
  const grainRef = useRef(null);
  const specRef = useRef(null);
  const specLineRef = useRef(null);
  const peakRefs = [useRef(null), useRef(null), useRef(null),
                    useRef(null), useRef(null), useRef(null)];
  const waveRef = useRef(null);
  const waveLineRef = useRef(null);
  const nodeRef = useRef(null);
  const airRef = useRef(null);
  const tractRef = useRef(null);
  const tractEdgeRef = useRef(null);
  const lipUpRef = useRef(null);
  const lipDnRef = useRef(null);
  const lipTraceRef = useRef(null);
  const turbRef = useRef(null);
  const bellGlowRef = useRef(null);
  const radRefs = [useRef(null), useRef(null), useRef(null)];
  const noteRef = useRef(null);
  const hzRef = useRef(null);
  const tootRef = useRef(null);
  const tootChipRef = useRef(null);
  const droneChipRef = useRef(null);
  const svgRef = useRef(null);

  /* ---- drag zones: the bore is honest about which parameter owns which
     part of its profile, so dragging there edits that parameter ---- */
  const dragBell = useCallback((e) => beginVerticalDrag(e, P.current.bell, setBell), [setBell]);
  const dragFlare = useCallback((e) => beginVerticalDrag(e, P.current.flare, setFlare), [setFlare]);

  useEffect(() => {
    let raf = 0, last = performance.now();
    let phase = 0, level = 0, lipSm = 0, glowSm = 0, turbSm = 0;
    const specSm = new Array(SPEC_N).fill(0);

    const xs = [];
    for (let i = 0; i < 16; i++) xs.push(BORE_X0 + (i / 15) * BORE_SPAN);

    // Fixed pseudo-random offsets so the wall grain does not crawl frame to frame.
    const grainSeed = [];
    for (let i = 0; i < 34; i++) {
      const s = Math.sin(i * 12.9898) * 43758.5453;
      grainSeed.push([(s - Math.floor(s)), ((s * 3.7) - Math.floor(s * 3.7))]);
    }

    /* Air parcels. Each keeps a rest position along the bore and a lateral
       offset; the animation moves it about that rest point. */
    const air = [];
    for (let i = 0; i < N_PARTICLES; i++) {
      const s = Math.sin(i * 78.233) * 43758.5453;
      const s2 = Math.sin(i * 27.611) * 12345.6789;
      air.push({
        u: (i + 0.5) / N_PARTICLES,
        lat: (s - Math.floor(s)) * 2 - 1,
        jitter: (s2 - Math.floor(s2)),
      });
    }

    const NW = 84;   // wave samples

    const tick = (now) => {
      const dt = Math.min(0.1, (now - last) / 1000); last = now;
      const L = lv.current || {};
      const pr = P.current;

      /* ---- bore geometry ---- */
      const bore = (L.bore && L.bore.length === 16) ? L.bore : DEFAULT_BORE;
      const hs = [], ho = [];
      for (let i = 0; i < 16; i++) {
        const h = Math.max(8, Math.min(MAX_HALF - WALL, bore[i] * M_PX));
        hs.push(h);
        ho.push(h + WALL);
      }
      const at = (arr, u) => {
        const f = Math.max(0, Math.min(1, u)) * 15;
        const i = Math.min(14, Math.floor(f));
        return arr[i] + (arr[i + 1] - arr[i]) * (f - i);
      };
      const hAt = (u) => at(hs, u);

      if (woodRef.current) woodRef.current.setAttribute('d', tubePath(xs, ho, CY));
      const cav = tubePath(xs, hs, CY);
      if (cavityRef.current) cavityRef.current.setAttribute('d', cav);
      if (innerEdgeRef.current) innerEdgeRef.current.setAttribute('d', cav);

      /* Wall grain: short strokes lying inside the wall thickness. Density is
         fixed, contrast tracks the Texture parameter (rough vs polished bore). */
      let g = '';
      for (let i = 0; i < grainSeed.length; i++) {
        const u = grainSeed[i][0];
        const side = i % 2 ? 1 : -1;
        const frac = 0.18 + 0.64 * grainSeed[i][1];
        const len = 0.02 + 0.05 * grainSeed[i][1];
        const u2 = Math.min(1, u + len);
        const x1 = BORE_X0 + u * BORE_SPAN, x2 = BORE_X0 + u2 * BORE_SPAN;
        const y1 = CY + side * (hAt(u) + WALL * frac);
        const y2 = CY + side * (hAt(u2) + WALL * frac);
        g += `M ${x1.toFixed(1)} ${y1.toFixed(1)} L ${x2.toFixed(1)} ${y2.toFixed(1)} `;
      }
      if (grainRef.current) {
        grainRef.current.setAttribute('d', g);
        grainRef.current.setAttribute('opacity', (0.10 + 0.55 * pr.texture).toFixed(3));
      }

      /* ---- vocal tract inset ---- */
      const tr = (L.tract && L.tract.length === 8) ? L.tract : DEFAULT_TRACT;
      const txs = [], ths = [];
      for (let i = 0; i < 8; i++) {
        txs.push(TRACT_X0 + (i / 7) * (TRACT_X1 - TRACT_X0));
        ths.push(Math.sqrt(Math.max(0.05, tr[i]) / Math.PI) * CM_PX);
      }
      const td = tubePath(txs, ths, CY);
      if (tractRef.current) {
        tractRef.current.setAttribute('d', td);
        tractRef.current.setAttribute('opacity', (0.20 + 0.75 * pr.tractMix).toFixed(3));
      }
      if (tractEdgeRef.current) tractEdgeRef.current.setAttribute('d', td);

      /* ---- envelope + phase ---- */
      const target = Math.max(0, Math.min(1, Number(L.pressure) || 0));
      level += (target - level) * (1 - Math.exp(-dt / 0.09));

      const f0 = Number(L.f0) > 0 ? L.f0 : 73.42;
      /* The real drone sits far above the frame rate, so the animation is
         geared down by a fixed ratio: relative pitch still reads (a higher
         f0 pulses faster, and the overblown register visibly doubles up)
         without strobing against the display refresh. */
      const wVis = 2 * Math.PI * (f0 / 22);
      phase += dt * wVis;
      if (phase > 1e6) phase -= 1e6;
      const cosP = Math.cos(phase), sinP = Math.sin(phase);

      /* ---- travelling wave, reconstructed ----
         waveP is the acoustic pressure at each segment boundary as a complex
         amplitude at the sounding frequency: magnitude and phase, measured by
         a quadrature detector running in the engine on the real waveguide.

         Evaluating it at a common time gives the pressure profile along the
         bore at that instant, and because each position carries its own phase,
         the profile MOVES. Where the bore reflects strongly the phases line up
         and it stands still with fixed nodes; where the bell radiates well the
         phase advances smoothly and the crest runs toward the mouth of it.
         That difference is the physics, and it appears here for free -- there
         is no travelling-versus-standing switch in this code, only the
         measured field.

         The previous version drew the amplitude envelope and pulsed all of it
         in step, which cannot show a wave going anywhere. */
      const waveP = (L.waveP && L.waveP.length === 32) ? L.waveP : ZERO32;
      const waveD = (L.waveD && L.waveD.length === 32) ? L.waveD : ZERO32;
      const pMax = fieldPeak(waveP);
      const dMax = fieldPeak(waveD);

      const press = (L.press && L.press.length === 16) ? L.press : ZERO16;
      let pEnvMax = 1e-9;
      for (let i = 0; i < 16; i++) if (press[i] > pEnvMax) pEnvMax = press[i];

      const damp = 1 - 0.45 * pr.wallDamp;

      /* Two things are drawn, because one alone would lie.

         The filled column is the amplitude, |P(x)| -- how hard the air is
         working at each place, which is what fixes the nodes. It is steady.

         The line inside it is the pressure at this instant, signed, crossing
         the axis where the air is momentarily at rest. That is the part that
         moves. Drawing only the instant would be truthful and unreadable: in
         a standing wave the pressure passes through zero everywhere at the
         same moment, so the whole picture would blink out twice a cycle
         against the frame rate. Drawing only the amplitude is what this used
         to do, and an amplitude cannot go anywhere. */
      const top = [], bot = [], line = [];
      for (let i = 0; i <= NW; i++) {
        const u = i / NW;
        const x = BORE_X0 + u * BORE_SPAN;
        const n = (waveP.length >> 1) - 1;
        const t = Math.max(0, Math.min(1, u)) * n;
        const j = Math.min(n - 1, Math.floor(t)), f = t - j;
        const mag = (Math.hypot(waveP[2 * j], waveP[2 * j + 1]) * (1 - f)
                   + Math.hypot(waveP[2 * j + 2], waveP[2 * j + 3]) * f) / pMax;
        const amp = Math.min(hAt(u) - 2, 96 * mag * damp * level) + level;
        top.push([x, CY - amp]);
        bot.push([x, CY + amp]);
        const pNow = fieldAt(waveP, u, cosP, sinP) / pMax;
        line.push([x, CY - Math.max(-1, Math.min(1, pNow)) * amp]);
      }
      const wd = smoothPath(top) + smoothPath(bot.slice().reverse(), false) + ' Z';
      if (waveRef.current) {
        waveRef.current.setAttribute('d', wd);
        waveRef.current.setAttribute('opacity', (0.55 * level).toFixed(3));
      }
      if (waveLineRef.current) {
        waveLineRef.current.setAttribute('d', smoothPath(line));
        waveLineRef.current.setAttribute('opacity', (0.95 * level).toFixed(3));
      }

      /* Pressure nodes: where the measured envelope dips to a local minimum.
         These are the points the tube is not "pushing" on — worth marking,
         since they move when the register changes. */
      let nd = '';
      if (level > 0.05) {
        for (let i = 1; i < 15; i++) {
          const a = press[i - 1] / pEnvMax, b = press[i] / pEnvMax, c = press[i + 1] / pEnvMax;
          if (b < a && b <= c && b < 0.35) {
            const x = xs[i], h = hs[i];
            nd += `M ${x.toFixed(1)} ${(CY - h).toFixed(1)} L ${x.toFixed(1)} ${(CY + h).toFixed(1)} `;
          }
        }
      }
      if (nodeRef.current) nodeRef.current.setAttribute('d', nd);

      /* ---- air parcels ----
         Air does not travel with a sound wave. Each parcel oscillates about a
         fixed place, and it is the disturbance that moves through them -- the
         single most misrepresented thing in any picture of sound. So each
         parcel here sits at a rest position and is displaced by the measured
         particle displacement at that position, waveD, which the engine
         derives from the same wave components as the pressure.

         Two things then follow on their own, without being drawn in. Parcels
         swing widest where the flow is greatest and stand still at the flow
         nodes. And because neighbouring parcels are displaced by slightly
         different amounts, they crowd together and spread apart: those are
         compressions and rarefactions, they sit a quarter cycle from the
         swing, and they move along the tube at the speed of the wave.

         On top of that, and much smaller, is the steady drift of the breath
         actually leaving the bell. Real, and worth showing, but it is the
         small term -- the reverse of how it is usually drawn. */
      const drift = Math.max(0, Number(L.meanFlow) || 0) * level;
      /* Displacement scaled to the drawing, and to the visible span rather
         than to metres: the real excursion in a didgeridoo is a few
         millimetres in a tube more than a metre long, which at this size would
         be invisible. The gearing is honest about being a gearing -- what is
         faithful is the shape, the phase and where the nodes fall. */
      const dScale = 0.075 * level;
      let ad = '';
      for (let i = 0; i < N_PARTICLES; i++) {
        const p = air[i];
        p.u += dt * 9000.0 * drift * (0.6 + 0.8 * p.jitter);
        if (p.u > 1) p.u -= 1;

        const disp = fieldAt(waveD, p.u, cosP, sinP) / dMax;
        const ux = Math.max(0, Math.min(1, p.u + dScale * disp));
        const x = BORE_X0 + ux * BORE_SPAN;
        const h = hAt(ux) - 3;
        const y = CY + p.lat * h * 0.86;
        /* A parcel of air is not compressible on its own -- it is the SPACING
           between parcels that shows compression, and the displacement above
           already produces it. So the marker keeps its size, growing only
           where the swing is large enough to be worth watching. Sizing it by
           the instantaneous pressure instead, as an earlier version did, made
           every parcel shrink to nothing twice a cycle together. */
        const nn = (waveD.length >> 1) - 1;
        const tt = Math.max(0, Math.min(1, p.u)) * nn;
        const jj = Math.min(nn - 1, Math.floor(tt)), ff = tt - jj;
        const swingMag = (Math.hypot(waveD[2 * jj], waveD[2 * jj + 1]) * (1 - ff)
                        + Math.hypot(waveD[2 * jj + 2], waveD[2 * jj + 3]) * ff) / dMax;
        const r = (level * (0.85 + 1.5 * swingMag)).toFixed(2);
        ad += `M ${(x - Number(r)).toFixed(1)} ${y.toFixed(1)} a ${r} ${r} 0 1 0 ${(2 * Number(r)).toFixed(2)} 0 a ${r} ${r} 0 1 0 ${(-2 * Number(r)).toFixed(2)} 0 `;
      }
      if (airRef.current) {
        airRef.current.setAttribute('d', ad);
        airRef.current.setAttribute('opacity', (0.85 * level).toFixed(3));
      }

      /* ---- lips ----
         Driven by the captured lip trace, so the drawn gap follows the real
         waveform — including the flat stretch where the lips are shut, which
         is where the buzz comes from. */
      const lw = (L.lipWave && L.lipWave.length) ? L.lipWave : ZERO96;
      let lwMax = 1e-9;
      for (let i = 0; i < lw.length; i++) if (lw[i] > lwMax) lwMax = lw[i];
      const idx = Math.floor(((phase / (2 * Math.PI)) * 3) % 1 * lw.length + lw.length) % lw.length;
      const lipNow = lw[idx] / lwMax;

      lipSm += (lipNow - lipSm) * (1 - Math.exp(-dt / 0.012));
      const gap = 1.5 + lipSm * 22 * level;
      const LIP_H = 40;
      if (lipUpRef.current) {
        lipUpRef.current.setAttribute('y', (CY - LIP_H).toFixed(1));
        lipUpRef.current.setAttribute('height', Math.max(3, LIP_H - gap / 2).toFixed(1));
      }
      if (lipDnRef.current) {
        lipDnRef.current.setAttribute('y', (CY + gap / 2).toFixed(1));
        lipDnRef.current.setAttribute('height', Math.max(3, LIP_H - gap / 2).toFixed(1));
      }

      /* Lip waveform inset, so the closed phase is visible as a flat floor. */
      let lt = '';
      const LTW = 118, LTH = 30, LTX = 96, LTY = CY + 96;
      for (let i = 0; i < lw.length; i++) {
        const x = LTX + (i / (lw.length - 1)) * LTW;
        const y = LTY - (lw[i] / lwMax) * LTH * Math.max(0.08, level);
        lt += (i ? ' L ' : 'M ') + x.toFixed(1) + ' ' + y.toFixed(1);
      }
      if (lipTraceRef.current) lipTraceRef.current.setAttribute('d', lt);

      /* ---- turbulence at the lips ----
         Jet noise is made where the air squeezes through the slit, and it
         stops when the lips shut, so the wisps scale with both. */
      const tTarget = Math.max(0, Math.min(1, Number(L.turb) || 0)) * lipSm;
      turbSm += (tTarget - turbSm) * (1 - Math.exp(-dt / 0.05));
      let tw = '';
      for (let i = 0; i < 10; i++) {
        const s = (i * 0.137 + phase * 0.09) % 1;
        const x = LIP_X + LIP_W + 4 + s * 74;
        const spread = 3 + s * 20 * turbSm;
        const yy = CY + Math.sin(i * 2.4 + phase * 0.7) * spread;
        const len = 5 + 13 * turbSm;
        tw += `M ${x.toFixed(1)} ${yy.toFixed(1)} l ${len.toFixed(1)} ${(Math.sin(i * 1.7) * 2).toFixed(1)} `;
      }
      if (turbRef.current) {
        turbRef.current.setAttribute('d', tw);
        turbRef.current.setAttribute('opacity', (0.5 * turbSm).toFixed(3));
      }

      /* ---- bell radiation ----
         Wavefronts leaving the open end, one per period, expanding at the
         speed of sound. */
      const db = (L.out && L.out.length) ? L.out[0] : -90;
      const gt = Math.max(0, Math.min(1, (db + 54) / 54));
      glowSm += (gt - glowSm) * (1 - Math.exp(-dt / 0.12));
      if (bellGlowRef.current) {
        bellGlowRef.current.setAttribute('rx', (22 + 56 * glowSm).toFixed(1));
        bellGlowRef.current.setAttribute('ry', (hs[15] + 24 + 34 * glowSm).toFixed(1));
        bellGlowRef.current.setAttribute('opacity', (0.10 + 0.5 * glowSm).toFixed(3));
      }
      for (let i = 0; i < radRefs.length; i++) {
        const el = radRefs[i].current;
        if (!el) continue;
        const s = ((phase / (2 * Math.PI)) + i / radRefs.length) % 1;
        el.setAttribute('rx', (6 + s * 108).toFixed(1));
        el.setAttribute('ry', (hs[15] * (0.9 + s * 1.5)).toFixed(1));
        el.setAttribute('opacity', ((1 - s) * 0.5 * glowSm).toFixed(3));
      }

      /* ---- output spectrum ----
         256 log-spaced points, so the drone's partials stand as separate
         lines instead of merging into an envelope. Live level as a filled
         curve, slow-falling peak hold as a line above it, and the tracked
         partials marked with their measured frequency. */
      const sp = (L.spec && L.spec.length === SPEC_N) ? L.spec : SPEC_FLOOR;
      const spk = (L.specPk && L.specPk.length === SPEC_N) ? L.specPk : SPEC_FLOOR;
      const sh = SPEC_Y1 - SPEC_Y0;
      const norm = (db) => Math.max(0, Math.min(1, (db - SPEC_DB) / (-6 - SPEC_DB)));

      let sd = `M ${SPEC_X0} ${SPEC_Y1}`;
      let sc = '';
      for (let i = 0; i < SPEC_N; i++) {
        const x = SPEC_X0 + (i / (SPEC_N - 1)) * (SPEC_X1 - SPEC_X0);
        const target = norm(sp[i]);
        // Rise immediately, fall gently: a spectrum that decays too fast
        // strobes, one that decays too slowly smears the partials together.
        specSm[i] += (target - specSm[i]) * (1 - Math.exp(-dt / (target > specSm[i] ? 0.008 : 0.10)));
        sd += ` L ${x.toFixed(1)} ${(SPEC_Y1 - specSm[i] * sh).toFixed(1)}`;
        sc += (i ? ' L ' : 'M ') + x.toFixed(1) + ' ' + (SPEC_Y1 - norm(spk[i]) * sh).toFixed(1);
      }
      sd += ` L ${SPEC_X1} ${SPEC_Y1} Z`;
      if (specRef.current) specRef.current.setAttribute('d', sd);
      if (specLineRef.current) specLineRef.current.setAttribute('d', sc);

      /* Tracked partials: a tick at each, labelled with the frequency the
         model actually measured rather than the nearest display point. */
      const pks = Array.isArray(L.peaks) ? L.peaks : [];
      for (let i = 0; i < peakRefs.length; i++) {
        const g = peakRefs[i].current;
        if (!g) continue;
        const pk = pks[i];
        if (!pk || !(pk.f > SPEC_LO) || pk.f > SPEC_HI || pk.db < SPEC_DB + 12) {
          g.setAttribute('opacity', '0');
          continue;
        }
        const x = specX(pk.f);
        const y = SPEC_Y1 - norm(pk.db) * sh;
        g.setAttribute('opacity', (i === 0 ? 0.95 : 0.6).toFixed(2));
        g.setAttribute('transform', `translate(${x.toFixed(1)} ${y.toFixed(1)})`);
        const t = g.querySelector('text');
        if (t) t.textContent = pk.f >= 1000 ? (pk.f / 1000).toFixed(2) + 'k' : pk.f.toFixed(0);
      }

      /* ---- readouts ---- */
      const toot = !!L.tootActive;
      if (noteRef.current) noteRef.current.textContent = hzToNote(f0);
      if (hzRef.current) hzRef.current.textContent = f0.toFixed(1) + ' Hz';
      const tf = Number(L.toot) > 0 ? L.toot : f0 * 2.7;
      if (tootRef.current) tootRef.current.textContent = hzToNote(tf) + ' · ' + tf.toFixed(0) + ' Hz';
      if (tootChipRef.current) tootChipRef.current.classList.toggle('live', toot);
      if (droneChipRef.current) droneChipRef.current.classList.toggle('live', !!L.playing);

      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  const zx = (f) => BORE_X0 + f * BORE_SPAN;

  return (
    <div className="ivwrap">
      <div className="ivchip drone" ref={droneChipRef}>
        <span className="dot" />
        <span className="lab">Drone</span>
        <span className="val" ref={noteRef}>D2</span>
        <span className="hz" ref={hzRef}>73.4 Hz</span>
      </div>
      <div className="ivchip toot" ref={tootChipRef}>
        <span className="lab">Toot</span>
        <span className="val" ref={tootRef}>—</span>
      </div>

      <svg ref={svgRef} className="ivsvg" viewBox={`0 0 ${VB_W} ${VB_H}`} preserveAspectRatio="xMidYMid meet">
        <defs>
          <linearGradient id="wood" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%"   stopColor="#3a2214" />
            <stop offset="22%"  stopColor="#8a5228" />
            <stop offset="46%"  stopColor="#a9682f" />
            <stop offset="70%"  stopColor="#6a3d1c" />
            <stop offset="100%" stopColor="#2b170c" />
          </linearGradient>
          <linearGradient id="cav" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%"   stopColor="#150c06" />
            <stop offset="50%"  stopColor="#0a0503" />
            <stop offset="100%" stopColor="#160d06" />
          </linearGradient>
          <linearGradient id="wave" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%"   stopColor="#ffd08a" />
            <stop offset="45%"  stopColor="#ff9d3c" />
            <stop offset="100%" stopColor="#e2562a" />
          </linearGradient>
          <radialGradient id="bellglow" cx="0.5" cy="0.5" r="0.5">
            <stop offset="0%"   stopColor="#ffb35a" stopOpacity="0.85" />
            <stop offset="100%" stopColor="#ff8a2a" stopOpacity="0" />
          </radialGradient>
          <linearGradient id="specg" x1="0" y1="1" x2="0" y2="0">
            <stop offset="0%"   stopColor="#c9702a" stopOpacity="0.55" />
            <stop offset="60%"  stopColor="#ffa447" stopOpacity="0.8" />
            <stop offset="100%" stopColor="#ffd9a6" stopOpacity="0.95" />
          </linearGradient>
          <linearGradient id="tractg" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%"   stopColor="#7a4a86" />
            <stop offset="100%" stopColor="#c4657a" />
          </linearGradient>
          <filter id="soft" x="-40%" y="-60%" width="180%" height="220%">
            <feGaussianBlur stdDeviation="7" />
          </filter>
        </defs>

        {/* centre axis */}
        <line className="iv-axis" x1={TRACT_X0} y1={CY} x2={BORE_X1 + 22} y2={CY} />

        {/* vocal tract inset */}
        <g className="iv-tract">
          <path ref={tractRef} d="" fill="url(#tractg)" />
          <path ref={tractEdgeRef} d="" className="iv-tract-edge" fill="none" />
          <line className="iv-glottis" x1={TRACT_X0 - 2} y1={CY - 26} x2={TRACT_X0 - 2} y2={CY + 26} />
          <text className="iv-cap" x={TRACT_X0 - 4} y={CY + 62}>VOCAL TRACT</text>
          <text className="iv-cap dim" x={TRACT_X0 - 4} y={CY - 62}>GLOTTIS</text>
        </g>

        {/* throat -> lips connector */}
        <path className="iv-neck" d={`M ${TRACT_X1} ${CY - 15} L ${LIP_X} ${CY - 22} L ${LIP_X} ${CY + 22} L ${TRACT_X1} ${CY + 15} Z`} />

        {/* bell radiation */}
        <ellipse ref={bellGlowRef} className="iv-bellglow" cx={BORE_X1 + 24} cy={CY} rx="30" ry="90"
                 fill="url(#bellglow)" filter="url(#soft)" />
        <g className="iv-radiation">
          {radRefs.map((r, i) => (
            <ellipse key={i} ref={r} cx={BORE_X1 + 4} cy={CY} rx="10" ry="40" fill="none" />
          ))}
        </g>

        {/* instrument body */}
        <path ref={woodRef} d="" fill="url(#wood)" className="iv-wood" />
        <path ref={grainRef} d="" className="iv-grain" fill="none" />
        <path ref={cavityRef} d="" fill="url(#cav)" />
        <path ref={waveRef} d="" fill="url(#wave)" className="iv-wave" />
        <path ref={waveLineRef} d="" className="iv-waveline" fill="none" />
        <path ref={airRef} d="" className="iv-air" />
        <path ref={nodeRef} d="" className="iv-node" fill="none" />
        <path ref={innerEdgeRef} d="" className="iv-inner" fill="none" />

        {/* turbulence at the slit */}
        <path ref={turbRef} d="" className="iv-turb" fill="none" />

        {/* lips */}
        <g className="iv-lips">
          <rect ref={lipUpRef} x={LIP_X} y={CY - 40} width={LIP_W} height="28" rx="9" />
          <rect ref={lipDnRef} x={LIP_X} y={CY + 12} width={LIP_W} height="28" rx="9" />
          <text className="iv-cap" x={LIP_X - 2} y={CY + 62}>LIPS</text>
        </g>

        {/* output spectrum */}
        <g className="iv-spec">
          <text className="iv-cap dim" x={SPEC_X0} y={SPEC_Y0 - 5}>OUTPUT SPECTRUM</text>
          <line className="iv-specbase" x1={SPEC_X0} y1={SPEC_Y1} x2={SPEC_X1} y2={SPEC_Y1} />
          {[50, 100, 200, 500, 1000, 2000, 5000, 10000].map((f) => (
            <g key={f}>
              <line className="iv-specgrid" x1={specX(f)} y1={SPEC_Y0} x2={specX(f)} y2={SPEC_Y1} />
              <text className="iv-specmark" x={specX(f) + 2} y={SPEC_Y1 - 3}>
                {f >= 1000 ? (f / 1000) + 'k' : f}
              </text>
            </g>
          ))}
          <path ref={specRef} className="iv-specfill" d="" />
          <path ref={specLineRef} className="iv-specpeak" d="" fill="none" />
          {peakRefs.map((r, i) => (
            <g key={i} ref={r} className="iv-peak" opacity="0">
              <line x1="0" y1="0" x2="0" y2="7" />
              <circle cx="0" cy="0" r="1.7" />
              <text x="0" y="-4">0</text>
            </g>
          ))}
        </g>

        {/* lip motion trace */}
        <g className="iv-trace">
          <text className="iv-cap dim" x={96} y={CY + 78}>LIP OPENING</text>
          <path ref={lipTraceRef} d="" fill="none" />
        </g>

        {/* drag affordances — labels appear on hover */}
        <g className="iv-zone" onPointerDown={dragFlare}>
          <rect x={zx(FLARE_ZONE[0])} y={CY - 120} width={zx(FLARE_ZONE[1]) - zx(FLARE_ZONE[0])} height="240" />
          <text x={(zx(FLARE_ZONE[0]) + zx(FLARE_ZONE[1])) / 2} y={CY - 96}>FLARE — DRAG</text>
        </g>
        <g className="iv-zone" onPointerDown={dragBell}>
          <rect x={zx(BELL_ZONE[0])} y={CY - 130} width={BORE_X1 + 30 - zx(BELL_ZONE[0])} height="260" />
          <text x={(zx(BELL_ZONE[0]) + BORE_X1) / 2 + 10} y={CY - 110}>BELL — DRAG</text>
        </g>
      </svg>

      <div className="ivhint">drag the bore to shape it · double-click a knob to reset · hold shift for fine</div>
    </div>
  );
}

Object.assign(window, { InstrumentView, hzToNote });
