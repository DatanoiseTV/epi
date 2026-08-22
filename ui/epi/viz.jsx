/* ============================================================
   Epi · the harp, drawn from telemetry
   ============================================================
   One canvas: the 88 keys along the bottom, each note's tine (or
   string, when the CP-70 is selected) fanning up and away, the
   hammers below them, and the pickup rail beyond the tips.

   Everything that moves here moves because the engine said so.
   The `levels` event carries every tine's peak displacement this
   frame, the held-key bitmask and the pedal state. The GEOMETRY is
   physical -- excursion in pixels follows the reported microns
   through each rod's drawn-to-real scale, and each core oscillates
   at its note's true fundamental -- while COLOUR and glow follow a
   decibel mapping, because a sympathetically shaken tine sits
   30-40 dB under a struck one and linear brightness would erase
   exactly the thing worth seeing.

   The keys are playable: pointer down maps to a note and velocity
   (deeper on the key is louder, like an accelerating press) and
   emits a ui_note event the processor injects into the engine.
   A computer keyboard works too, a-w-s-e-d... from middle C.
   ============================================================ */

const VZ_W = 1128, VZ_H = 300, VZ_PAD = 16, VZ_KH = 54;
const VZ_N = 88, VZ_LO = 21;
const VZ_BLACK_PC = [1, 3, 6, 8, 10];
const VZ_TILT = 0.55, VZ_GLOW = 0.6;
const VZ_RANGE = 62;                       // dB range for colour and glow only
// The physical magnifier is honest until the drawing collides: a bass
// fortissimo would swing a rod across its neighbours, so the excursion
// soft-limits at just under half the rod spacing. tanh keeps every ratio
// intact below the cap and only the last pixels compress.
const VZ_SWING_CAP = 5.4;

/* The drawing is a scaled instrument, so the swing can be physical: the
   engine reports every tip's peak displacement in microns, each note's rod
   is drawn at a known fraction of its real length, and the excursion on
   screen is displacement times that same fraction. One uniform x3
   magnifier on top -- a microscope, not a lie: it preserves every ratio
   between notes, and without it a treble tine's fifth of a millimetre is
   less than a pixel, which is also why you cannot see one move in real
   life. */
const VZ_MAG = 3;

/* Real resonator lengths in millimetres. Rhodes tines run 175 down to 22;
   the CP-70's strings follow the voice's own length law,
   L = 665 * 2^(-(note-60)/13.16). */
function vzLenMm(i, mode) {
  if (mode === 1) return 665.2 * Math.pow(2, -((VZ_LO + i) - 60) / 13.16);
  if (mode === 2) return 90 * Math.pow(14 / 90, i / 87);   // reed bar, roughly
  return 175 * Math.pow(22 / 175, i / 87);
}

/* The CP-70's factory stretch table, the same anchors the voice uses.
   Without it the drawn string and the heard string disagree by up to a
   third of a semitone at the ends of the compass. */
const VZ_STRETCH_M = [21, 28, 42, 60, 88, 100, 108];
const VZ_STRETCH_C = [-23.0, -13.7, -4.6, 0.0, 7.2, 20.0, 35.0];
function vzStretchCents(n) {
  if (n <= VZ_STRETCH_M[0]) return VZ_STRETCH_C[0];
  for (let i = 0; i < 6; i++)
    if (n <= VZ_STRETCH_M[i + 1]) {
      const t = (n - VZ_STRETCH_M[i]) / (VZ_STRETCH_M[i + 1] - VZ_STRETCH_M[i]);
      return VZ_STRETCH_C[i] + (VZ_STRETCH_C[i + 1] - VZ_STRETCH_C[i]) * t;
    }
  return VZ_STRETCH_C[6];
}

/* Key geometry: 52 equal whites spanning the width, blacks 60% as wide
   overlaid on the boundary. Computed once. */
const VZ_KEYS = (() => {
  const whites = [];
  for (let i = 0; i < VZ_N; i++)
    if (!VZ_BLACK_PC.includes((VZ_LO + i) % 12)) whites.push(VZ_LO + i);
  const ww = (VZ_W - 2 * VZ_PAD) / whites.length, bw = ww * 0.6;
  const map = {};
  whites.forEach((n, wi) => {
    map[n] = { n, black: false, x: VZ_PAD + wi * ww, w: ww, cx: VZ_PAD + wi * ww + ww / 2 };
  });
  for (let i = 0; i < VZ_N; i++) {
    const n = VZ_LO + i;
    if (VZ_BLACK_PC.includes(n % 12)) {
      const left = map[n - 1];
      const bx = left.x + left.w - bw / 2;
      map[n] = { n, black: true, x: bx, w: bw, cx: bx + bw / 2 };
    }
  }
  return map;
})();

function vzNoteAt(e, cv) {
  const r = cv.getBoundingClientRect();
  const x = (e.clientX - r.left) * (VZ_W / r.width);
  const y = (e.clientY - r.top) * (VZ_H / r.height);
  if (y < VZ_H - VZ_KH - 8) return null;
  const vel = Math.max(0.3, Math.min(1, 0.4 + 0.6 * (y - (VZ_H - VZ_KH)) / VZ_KH));
  if (y < VZ_H - VZ_KH + VZ_KH * 0.6)
    for (let n = VZ_LO; n < VZ_LO + VZ_N; n++) {
      const k = VZ_KEYS[n];
      if (k.black && x >= k.x && x <= k.x + k.w) return { n, vel };
    }
  for (let n = VZ_LO; n < VZ_LO + VZ_N; n++) {
    const k = VZ_KEYS[n];
    if (!k.black && x >= k.x && x <= k.x + k.w) return { n, vel };
  }
  return null;
}

/* a-row plays from middle C, like every soft synth. */
const VZ_KEYMAP = ['a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'y', 'h', 'u', 'j', 'k', 'o', 'l', 'p', ';'];

function VizCard() {
  const { useJuceChoice, useEventRef, emitNative } = JuceBridge;
  const [instIdx, setInstIdx] = useJuceChoice('instrument', INSTRUMENTS);
  const strings = instIdx === 1;
  const lv = useEventRef('levels', { harp: [], keys: [], voices: 0 });
  const cvsRef = useRef(null);
  const modeRef = useRef(instIdx);
  modeRef.current = instIdx;

  useEffect(() => {
    const cv = cvsRef.current;
    if (!cv) return;
    const dpr = window.devicePixelRatio || 1;
    cv.width = VZ_W * dpr; cv.height = VZ_H * dpr;
    const ctx = cv.getContext('2d');
    ctx.scale(dpr, dpr);

    const env = new Float32Array(VZ_N);
    const prevEnv = new Float32Array(VZ_N);
    const strikeT = new Float32Array(VZ_N).fill(99);
    let scale = 1e-3;
    let raf = 0, prev = performance.now();
    const tuneRelay = window.Juce && Juce.getSliderState ? Juce.getSliderState('tune') : null;

    const frame = (now) => {
      const dt = Math.min(0.1, (now - prev) / 1000); prev = now;
      const L = lv.current || {};
      const H = L.harp || [], K = L.keys || [];
      const isDown = (i) => ((K[i >> 5] || 0) & (1 << (i & 31))) !== 0;
      const raw = modeRef.current;           // 0 tines, 1 strings, 2 reeds, 3 grand
      const mode = raw === 3 ? 1 : raw;      // the grand draws as strings
      const tineMode = mode !== 1;

      let peak = 1e-6;
      for (let i = 0; i < VZ_N; i++) {
        const v = i < H.length ? Math.abs(H[i]) : 0;
        /* Fast up, slow down: the engine sends a peak per block, and
           without the release it strobes at the block rate. */
        env[i] = v > env[i] ? v : env[i] + (v - env[i]) * Math.min(1, dt * 6);
        if (env[i] > peak) peak = env[i];
        /* A level jump is a strike; that is what fires the hammer. */
        if (env[i] > prevEnv[i] * 2.2 && env[i] > peak * 0.02) strikeT[i] = 0;
        prevEnv[i] = env[i];
        strikeT[i] += dt;
      }
      /* The reference must hold still while a note rings: if it decays
         with the struck string, every sympathetic level is re-normalised
         upward as the note fades and the wash appears to swell. Slow
         release keeps ratios honest within a note's lifetime. */
      scale = peak > scale ? peak : scale + (peak - scale) * Math.min(1, dt * 0.12);
      const inv = 1 / Math.max(scale, 1e-6);

      /* Master tune, in cents, read straight off the relay -- the drawn
         frequency must follow the heard one through the knob. */
      const tuneCents = tuneRelay
        ? -100 + 200 * tuneRelay.getNormalisedValue() : 0;
      const tSec = now / 1000;

      ctx.clearRect(0, 0, VZ_W, VZ_H);

      const sx = (VZ_TILT - 0.2) * 0.5, sy = 1 - sx * 0.25;
      const baseY = VZ_H - VZ_KH - 24, hyRest = VZ_H - VZ_KH - 12;

      /* -- keys -- */
      for (let i = 0; i < VZ_N; i++) {
        const k = VZ_KEYS[VZ_LO + i];
        if (k.black) continue;
        ctx.fillStyle = '#d9d2bf';
        ctx.fillRect(k.x + 0.5, VZ_H - VZ_KH, k.w - 1, VZ_KH);
        ctx.fillStyle = 'rgba(0,0,0,.18)';
        ctx.fillRect(k.x + 0.5, VZ_H - 3, k.w - 1, 3);
        if (isDown(i)) {
          ctx.fillStyle = 'rgba(202,164,94,.85)';
          ctx.fillRect(k.x + 0.5, VZ_H - VZ_KH, k.w - 1, VZ_KH);
        }
      }
      for (let i = 0; i < VZ_N; i++) {
        const k = VZ_KEYS[VZ_LO + i];
        if (!k.black) continue;
        ctx.fillStyle = '#15130f';
        ctx.fillRect(k.x, VZ_H - VZ_KH, k.w, VZ_KH * 0.6);
        ctx.fillStyle = 'rgba(255,255,255,.06)';
        ctx.fillRect(k.x, VZ_H - VZ_KH, k.w, 2);
        if (isDown(i)) {
          ctx.fillStyle = 'rgba(202,164,94,.9)';
          ctx.fillRect(k.x, VZ_H - VZ_KH, k.w, VZ_KH * 0.6);
        }
      }
      ctx.fillStyle = 'rgba(60,55,44,.95)';
      ctx.font = '8px Space Grotesk';
      ctx.textAlign = 'center';
      for (let i = 0; i < VZ_N; i++) {
        const n = VZ_LO + i;
        if (n % 12 === 0) ctx.fillText('C' + Math.floor(n / 12 - 1), VZ_KEYS[n].cx, VZ_H - 6);
      }

      /* -- tines / strings, hammers, rail -- */
      const railPts = [];
      const gap = 8;
      const segs = 8, m1 = (u) => Math.pow(u, 1.5), m2 = (u) => Math.sin(3.9 * u) * Math.sqrt(u);

      for (let i = 0; i < VZ_N; i++) {
        const x = VZ_KEYS[VZ_LO + i].cx;
        /* Tine length halves per two octaves; strings run longer and
           flatter -- the two real geometries, scaled to the frame. */
        const Lm = mode === 1 ? 40 + 152 * Math.pow(1 - i / 87, 1.1)
                 : mode === 2 ? 22 + 54 * Math.pow(1 - i / 87, 1.1)
                              : 30 + 96 * Math.pow(1 - i / 87, 1.15);
        const e = env[i] * inv;
        const a = e > 0 ? Math.min(1, Math.max(0, (20 * Math.log10(e) + VZ_RANGE) / VZ_RANGE)) : 0;
        const down = isDown(i);
        const sym = a > 0.02 && !down && strikeT[i] > 0.5;
        /* Sympathetic brightness gets its own mapping: a 45 dB window with
           a gamma of 1.6, so the octave partner (-19 dB) reads clearly,
           the twelfth (-26 dB) noticeably dimmer, and the -38 dB wash as
           the faint shimmer it actually is -- the flat 62 dB window put
           near-inaudible strings at 40% brightness. */
        const ws = sym ? Math.pow(Math.max(0, (20 * Math.log10(Math.max(e, 1e-9)) + 45) / 45), 1.6) : 0;
        const active = a > 0.02 ? Math.min(1, a * 1.4) : 0;

        /* Physical swing: microns to millimetres, millimetres to pixels
           through this rod's own drawn-to-real scale, times the uniform
           magnifier. */
        const rawA = (env[i] * 1e-3) * (Lm / vzLenMm(i, mode)) * VZ_MAG;
        const A = VZ_SWING_CAP * Math.tanh(rawA / VZ_SWING_CAP);

        /* Physical frequency: this note's fundamental, with the master
           tune and (for the strings) the factory stretch. Sampled once
           per frame, so anything above ~30 Hz aliases -- which is what a
           tine looks like to the eye: a blur envelope with a slow beat
           crawling through it. The envelope band carries the true
           excursion; the sampled core carries the motion. */
        const n = VZ_LO + i;
        const cents = tuneCents + (tineMode ? 0 : vzStretchCents(n));
        const f = 440 * Math.pow(2, (n - 69) / 12) * Math.pow(2, cents / 1200);
        const ph = 6.283 * f * tSec;
        /* The second mode: a cantilever's sits at 6.267 f, a string's at
           2 f. It decays several times faster than the fundamental, so it
           is shown while the strike is fresh. */
        const ph2 = ph * (tineMode ? 6.267 : 2.0);
        const A2 = A * 0.45 * Math.exp(-strikeT[i] * 6);

        const tipX = x + Lm * sx, tipY = baseY - Lm * sy;

        /* hammer: a dot that jumps at the strike */
        const esc = 5;
        const hj = strikeT[i] < 0.4 ? Math.exp(-strikeT[i] * 16) * (esc + 4) : 0;
        ctx.fillStyle = active > 0 && !sym ? '#caa45e' : '#5d5749';
        ctx.beginPath();
        ctx.arc(x, hyRest - esc * 0.6 - hj, 2, 0, 6.283);
        ctx.fill();

        /* vibration envelope band: what the eye actually sees of a fast tine */
        // A sympathetically shaken rod moves microns -- physically invisible.
        // The BAND is the one deliberately non-physical stroke: it floors at
        // a sliver whenever the colour mapping says the rod is alive, so the
        // pedal wash reads on screen the way it reads in the room.
        const bandA = Math.max(A, down ? a * 1.6 : sym ? ws * 1.6 : 0);
        if (bandA > 0.15) {
          ctx.beginPath();
          for (let s = 0; s <= segs; s++) {
            const u = s / segs, ee = bandA * m1(u) + A2 * Math.abs(m2(u));
            const px = x + (tipX - x) * u + ee, py = baseY + (tipY - baseY) * u;
            s === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
          }
          for (let s = segs; s >= 0; s--) {
            const u = s / segs, ee = bandA * m1(u) + A2 * Math.abs(m2(u));
            ctx.lineTo(x + (tipX - x) * u - ee, baseY + (tipY - baseY) * u);
          }
          ctx.closePath();
          /* Sympathetic band opacity tracks the measured level, so two
             strings ringing 12 dB apart read 12 dB apart on screen. */
          ctx.fillStyle = sym
            ? 'rgba(216,205,176,' + Math.min(0.3, 0.02 + ws * 0.24).toFixed(3) + ')'
            : 'rgba(202,164,94,' + Math.min(0.3, 0.06 + bandA * 0.045).toFixed(3) + ')';
          ctx.fill();
        }

        /* the core at its instantaneous position */
        ctx.beginPath();
        for (let s = 0; s <= segs; s++) {
          const u = s / segs;
          const px = x + (tipX - x) * u + A * Math.sin(ph) * m1(u) + A2 * Math.sin(ph2) * m2(u);
          const py = baseY + (tipY - baseY) * u;
          s === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
        }
        ctx.lineWidth = tineMode ? 2.9 - 1.5 * i / 87 : 2.3 - 1.2 * i / 87;
        if (!sym && active > 0) {
          /* steel to gold, fading with the vibration envelope */
          const w = active;
          ctx.strokeStyle = 'rgb(' + Math.round(143 + w * 87) + ',' + Math.round(136 + w * 61) + ',' + Math.round(120 + w * 12) + ')';
          ctx.shadowColor = '#caa45e';
          ctx.shadowBlur = VZ_GLOW * 22 * w * Math.min(1, A / 3);
        } else if (sym) {
          /* Not a flag: the colour rides the same dB mapping as the struck
             gold, but toward a cool silver, so struck and sympathetic are
             different temperatures and the wash shows its actual ratios --
             the octave partner bright, the twelfth dimmer, the background
             a shade over rest. */
          const w = ws;
          /* Lerp FROM the resting stroke, not from a darker steel: a faint
             sympathetic string must sit just above rest, never below it. */
          ctx.strokeStyle = 'rgb(' + Math.round(143 + w * 73) + ',' + Math.round(136 + w * 69) + ',' + Math.round(120 + w * 56) + ')';
          ctx.shadowColor = '#d8cba8';
          ctx.shadowBlur = VZ_GLOW * 14 * w;
        } else {
          ctx.strokeStyle = '#8f8878';
          ctx.shadowBlur = 0;
        }
        ctx.stroke();
        ctx.shadowBlur = 0;

        if (tineMode) {
          /* tuning-spring collar riding the rod, tonebar mount at the base */
          const u = 0.72;
          const cx2 = x + (tipX - x) * u + A * Math.sin(ph) * m1(u) + A2 * Math.sin(ph2) * m2(u);
          const cy2 = baseY + (tipY - baseY) * u;
          ctx.fillStyle = active > 0 && !sym ? '#c9b98e' : '#6e675a';
          ctx.fillRect(cx2 - 1.8, cy2 - 3.5, 3.6, 7);
          ctx.fillStyle = '#3b3629';
          ctx.fillRect(x - 2.2, baseY - 1, 4.4, 4);
        }

        const mag = Math.hypot(sx, sy);
        railPts.push({ x: tipX + (sx / mag) * gap, y: tipY - (sy / mag) * gap, w: sym ? 0 : active, sw: ws });
      }

      /* the pickup rail (bridge, for the CP-70) beyond the tips */
      ctx.beginPath();
      railPts.forEach((r, j) => j === 0 ? ctx.moveTo(r.x, r.y) : ctx.lineTo(r.x, r.y));
      ctx.strokeStyle = '#3b3425';
      ctx.lineWidth = 2.5;
      ctx.stroke();
      railPts.forEach((r) => {
        ctx.beginPath();
        ctx.arc(r.x, r.y, 1.6, 0, 6.283);
        if (r.w > 0) {
          const w = r.w;
          ctx.fillStyle = 'rgb(' + Math.round(122 + w * 108) + ',' + Math.round(106 + w * 101) + ',' + Math.round(68 + w * 74) + ')';
          ctx.shadowColor = '#caa45e';
          ctx.shadowBlur = 10 * w;
        } else if (r.sw > 0) {
          const w = r.sw;
          ctx.fillStyle = 'rgb(' + Math.round(122 + w * 94) + ',' + Math.round(106 + w * 99) + ',' + Math.round(68 + w * 108) + ')';
          ctx.shadowColor = '#d8cba8';
          ctx.shadowBlur = 7 * w;
        } else {
          ctx.fillStyle = '#7a6a44';
          ctx.shadowBlur = 0;
        }
        ctx.fill();
        ctx.shadowBlur = 0;
      });

      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(raf);
  }, []);

  /* ---- playing: pointer on the keys, or the computer keyboard ---- */
  const cvN = useRef(null);
  const noteOn = (n, vel) => emitNative('ui_note', { note: n, velocity: vel, on: true });
  const noteOff = (n) => emitNative('ui_note', { note: n, velocity: 0, on: false });

  const onDown = (e) => {
    e.preventDefault();
    const k = vzNoteAt(e, cvsRef.current);
    if (k) {
      cvN.current = k.n;
      noteOn(k.n, k.vel);
      try { e.target.setPointerCapture(e.pointerId); } catch (_) {}
    }
  };
  const onMove = (e) => {
    if (cvN.current == null) return;
    const k = vzNoteAt(e, cvsRef.current);
    if (k && k.n !== cvN.current) { noteOff(cvN.current); cvN.current = k.n; noteOn(k.n, k.vel); }
  };
  const onUp = () => { if (cvN.current != null) { noteOff(cvN.current); cvN.current = null; } };

  useEffect(() => {
    const held = {};
    const down = (e) => {
      if (e.repeat || e.metaKey || e.ctrlKey || e.altKey) return;
      const t = e.target;
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
      const i = VZ_KEYMAP.indexOf(e.key);
      if (i >= 0) { const n = 60 + i; noteOn(n, 0.75); held[e.key] = n; }
    };
    const up = (e) => {
      const n = held[e.key];
      if (n != null) { noteOff(n); delete held[e.key]; }
    };
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
    };
  }, []);

  const modes = ['TINES', 'STRINGS', 'REEDS', 'GRAND'];
  return (
    <div className="vizcard">
      <canvas ref={cvsRef} style={{ width: VZ_W, height: VZ_H }}
              onPointerDown={onDown} onPointerMove={onMove}
              onPointerUp={onUp} onPointerLeave={onUp} />
      <div className="viz-top">
        <div className="seg">
          {modes.map((m, i) => (
            <button key={m} className={i === instIdx ? 'on' : ''}
                    onClick={() => setInstIdx(i)}>{m}</button>
          ))}
        </div>
        <span className="viz-note">{instIdx === 1 ? 'E-GRAND · PIEZO BRIDGE' : instIdx === 2 ? 'REED · ELECTROSTATIC BAR' : instIdx === 3 ? 'GRAND · SOUNDBOARD · MIC PAIR' : 'TINE · MAGNETIC PICKUPS'} · HAMMER ACTION</span>
      </div>
      <div className="viz-hint">CLICK KEYS OR PLAY A – ; ON YOUR KEYBOARD</div>
      <PedalLamp />
    </div>
  );
}

/* The sustain-pedal lamp. A pedal held by CC64 is invisible on the panel,
   and an inverted or stuck pedal is the classic "why does everything
   sustain" -- the lamp makes the state diagnosable at a glance. Its own
   tiny component so the 30 Hz telemetry never re-renders the canvas card:
   state changes only when the pedal actually moves. */
function PedalLamp() {
  const lv = JuceBridge.useEventRef('levels', { pedal: false });
  const [down, setDown] = React.useState(false);
  useEffect(() => {
    let raf = 0;
    const tick = () => {
      const d = !!(lv.current && lv.current.pedal);
      setDown((prev) => (prev === d ? prev : d));
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);
  return (
    <div className={'pedal-lamp' + (down ? ' on' : '')}>
      <span className="led" />SUSTAIN
    </div>
  );
}

Object.assign(window, { VizCard, PedalLamp });
