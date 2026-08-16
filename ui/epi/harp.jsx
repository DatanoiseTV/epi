/* ============================================================
   Epi · the harp, in three dimensions
   ============================================================
   Eighty-eight assemblies -- key, hammer, tine, tone bar, pickup --
   built as real geometry in millimetres and put through a pinhole
   camera, rather than sheared about until they looked like depth.

   The layout is the decision that matters, and two attempts got it
   backwards before this one. Sending the NOTE axis into the distance
   gives every note about two pixels of the picture, and eighty-eight
   things two pixels apart are a striped sheet whatever else is done to
   them. On the instrument the note axis runs across the player and the
   mechanism goes away from them -- which gives each note a dozen pixels
   of width, puts the depth on the axis that has any, and is also simply
   what one looks like.

   Everything is in millimetres, close enough to the real instrument that
   the proportions come out on their own: a harp about 1.2 m wide, tines
   from 175 mm at the bottom to 22 at the top, keys 140 mm long. That
   eight-to-one range in tine length is not a graphic effect. It is why
   the bass growls and the top octave pings, and it is the one thing a
   picture of this instrument ought to show.
   ============================================================ */

const HARP_N = 88;

/* ---- the instrument, in millimetres ----
   Everything here is prefixed. These files are plain scripts sharing one
   global lexical scope, so a top-level `const` that clashes with another
   file's throws a redeclaration error and that file simply never defines
   anything -- which is exactly what happened: this named its tone bar
   BAR_Y, instrument.jsx already had one, and the whole interface went
   blank with an error pointing somewhere else entirely. */
const HP_WKW = 23;                        // white key width
const HP_BLACK_PC = [1, 3, 6, 8, 10];     // pitch classes that are black

const HP_TINE_LEN0 = 175, HP_TINE_LEN1 = 22; // bass and treble tine
const HP_BAR_Y = 34, HP_BAR_X0 = -16;        // tone bar: above, and back to the block
const HP_BAR_LEN0 = 150, HP_BAR_LEN1 = 66;
const HP_KEY_Y = -54, HP_KEY_X0 = -205, HP_KEY_X1 = -62;
const HP_KEY_DIP = 7;                     // how far a key falls when played
const HP_HAM_AT = 0.20;                   // where the hammer meets the tine
const HP_SWING = 11;                      // mm of tip travel at full deflection

const HP_TINE_HW = 1.3, HP_BAR_HW = 3.4, HP_POLE_HW = 3.0;   // half-widths across z

/* Note positions: the keyboard's own layout, with each tine sitting at
   its key's centre. A real harp spaces its tines evenly and lets them
   fall where they may against the keys. Putting each one behind its own
   key costs a little truth and buys what the picture is for, which is
   being able to see which note is which. */
const HP_KEY_Z = new Float64Array(HARP_N);
const HP_IS_BLACK = new Uint8Array(HARP_N);
(function () {
  let w = 0;
  for (let i = 0; i < HARP_N; i++) {
    const pc = (21 + i) % 12;
    const black = HP_BLACK_PC.indexOf(pc) >= 0;
    HP_IS_BLACK[i] = black ? 1 : 0;
    if (black) HP_KEY_Z[i] = w * HP_WKW;                  // on the boundary between whites
    else { HP_KEY_Z[i] = w * HP_WKW + HP_WKW * 0.5; w++; }
  }
})();
const HP_HARP_W = HP_KEY_Z[HARP_N - 1] + HP_WKW * 0.5;

/* Length against pitch. A beam's frequency goes as 1/L^2, so halving the
   length is two octaves -- which is where the eight to one comes from,
   not from picking numbers that looked good. */
function hpTineLen(i) {
  const t = i / (HARP_N - 1);
  return HP_TINE_LEN0 * Math.pow(HP_TINE_LEN1 / HP_TINE_LEN0, t);
}
function hpBarLen(i) {
  const t = i / (HARP_N - 1);
  return HP_BAR_LEN0 * Math.pow(HP_BAR_LEN1 / HP_BAR_LEN0, t);
}

/* ---- the camera ----
   In front of the keyboard, above it, looking down and back along the
   mechanism. A pinhole: no shear and no fudge, so straight things are
   straight and parallel things converge because they are meant to. */
const HP_CAM = { x: -430, y: 560, z: HP_HARP_W * 0.5, atx: -20, aty: 0, F: 372 };
const HP_VIEW_CX = 384, HP_VIEW_CY = 196;

const _P = Math.atan2(HP_CAM.y - HP_CAM.aty, HP_CAM.atx - HP_CAM.x);
const _cp = Math.cos(_P), _sp = Math.sin(_P);

function hpProject(x, y, z) {
  const rx = x - HP_CAM.x, ry = y - HP_CAM.y, rz = z - HP_CAM.z;
  const depth = rx * _cp - ry * _sp;                 // along the view direction
  const up = rx * _sp + ry * _cp;
  const s = HP_CAM.F / Math.max(80, depth);
  return [HP_VIEW_CX + rz * s, HP_VIEW_CY - up * s];
}

/* Every part of this instrument is a flat hpRibbon lying in the x-z plane
   at some height, so one helper draws all of them. */
function hpRibbon(x0, x1, y0, y1, z, hw) {
  const a = hpProject(x0, y0, z - hw), b = hpProject(x1, y1, z - hw);
  const c = hpProject(x1, y1, z + hw), d = hpProject(x0, y0, z + hw);
  return a[0].toFixed(1) + ',' + a[1].toFixed(1) + ' ' +
         b[0].toFixed(1) + ',' + b[1].toFixed(1) + ' ' +
         c[0].toFixed(1) + ',' + c[1].toFixed(1) + ' ' +
         d[0].toFixed(1) + ',' + d[1].toFixed(1);
}

function HarpView() {
  const lv = JuceBridge.useEventRef('levels',
    { harp: [], keys: [], loNote: 21, voices: 0 });

  const tineRefs = useRef([]);
  const keyRefs = useRef([]);
  const hamRefs = useRef([]);
  const capRef = useRef(null);
  if (tineRefs.current.length !== HARP_N) {
    tineRefs.current = Array.from({ length: HARP_N }, () => React.createRef());
    keyRefs.current = Array.from({ length: HARP_N }, () => React.createRef());
    hamRefs.current = Array.from({ length: HARP_N }, () => React.createRef());
  }

  useEffect(() => {
    let raf = 0, prev = performance.now();
    const env = new Float32Array(HARP_N);
    const prevEnv = new Float32Array(HARP_N);
    const strikeT = new Float32Array(HARP_N).fill(99);
    let scale = 1e-3;

    const frame = (now) => {
      const dt = Math.min(0.1, (now - prev) / 1000); prev = now;
      const L = lv.current || {};
      const H = L.harp || [], K = L.keys || [];
      const isDown = (i) => ((K[i >> 5] || 0) & (1 << (i & 31))) !== 0;

      let peak = 1e-6;
      for (let i = 0; i < HARP_N; i++) {
        const v = i < H.length ? Math.abs(H[i]) : 0;
        /* Fast up, slow down: the engine sends a peak per block, and
           without the release it strobes at the block rate. */
        env[i] = v > env[i] ? v : env[i] + (v - env[i]) * Math.min(1, dt * 6);
        if (env[i] > peak) peak = env[i];
      }
      scale = peak > scale ? peak : scale + (peak - scale) * Math.min(1, dt * 0.6);
      const inv = 1 / Math.max(scale, 1e-6);

      /* In decibels. A tine shaken by the frame sits 30 to 40 dB under the
         one that was struck, which linearly is a hundredth of the height
         and disappears -- so a linear mapping throws away the one thing
         this drawing is for. */
      const RANGE = 52;
      let held = 0, ringing = 0;

      for (let i = 0; i < HARP_N; i++) {
        const e = env[i] * inv;
        const a = e > 0 ? Math.min(1, Math.max(0, (20 * Math.log10(e) + RANGE) / RANGE)) : 0;
        const z = HP_KEY_Z[i], len = hpTineLen(i), down = isDown(i);
        if (down) held++;
        if (a > 0.02) ringing++;

        const t = tineRefs.current[i].current;
        if (t) {
          t.setAttribute('points', hpRibbon(0, len, 0, a * HP_SWING, z, HP_TINE_HW));
          const cls = a <= 0.02 ? 'hp-tine' : down ? 'hp-tine struck' : 'hp-tine symp';
          if (t.getAttribute('class') !== cls) t.setAttribute('class', cls);
          t.setAttribute('opacity', (0.40 + 0.60 * a).toFixed(3));
        }

        const k = keyRefs.current[i].current;
        if (k) {
          const cls = (HP_IS_BLACK[i] ? 'hp-key black' : 'hp-key') + (down ? ' down' : '');
          if (k.getAttribute('class') !== cls) k.setAttribute('class', cls);
          /* A key pivots on its balance rail, so the front falls and the
             back does not. */
          k.setAttribute('points',
            hpRibbon(HP_KEY_X0, HP_KEY_X1, HP_KEY_Y - (down ? HP_KEY_DIP : 0), HP_KEY_Y, z,
                   HP_IS_BLACK[i] ? HP_WKW * 0.28 : HP_WKW * 0.46));
        }

        /* One hammer per note, fired by that tine's own level jumping, so
           a chord shows as a chord and a note struck while already
           ringing still shows. */
        if (env[i] > prevEnv[i] * 2.2 && e > 0.02) strikeT[i] = 0;
        prevEnv[i] = env[i];
        strikeT[i] += dt;

        const h = hamRefs.current[i].current;
        if (h) {
          const st = strikeT[i];
          if (st > 0.42) { if (h.getAttribute('opacity') !== '0') h.setAttribute('opacity', '0'); }
          else {
            const rise = st < 0.05 ? st / 0.05
                       : st < 0.10 ? 1
                       : Math.max(0, 1 - (st - 0.10) / 0.32);
            const hx = len * HP_HAM_AT;
            const y = -34 + 32 * rise;               // up to the tine and away
            h.setAttribute('opacity', (0.30 + 0.70 * rise).toFixed(3));
            h.setAttribute('points', hpRibbon(hx - 34, hx, y, y, z, HP_WKW * 0.22));
          }
        }
      }

      if (capRef.current) {
        const txt = held > 0 && ringing > held
          ? held + ' struck · ' + (ringing - held) + ' answering'
          : ringing + (ringing === 1 ? ' tine' : ' tines') + ' ringing';
        if (capRef.current.textContent !== txt) capRef.current.textContent = txt;
      }

      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(raf);
  }, []);

  /* Static parts, projected once. Painted back to front: the bar rail is
     furthest, then bars, blocks, pickups, tines, hammers, and the keys
     nearest the viewer. Every note is the same distance from the camera
     -- it looks along the note axis, not across it -- so there is nothing
     to sort between them. */
  const bars = [], blocks = [], poles = [], tines = [], hammers = [], keys = [];
  for (let i = 0; i < HARP_N; i++) {
    const z = HP_KEY_Z[i], len = hpTineLen(i), bl = hpBarLen(i);
    bars.push(<polygon key={'b' + i} className="hp-bar"
                       points={hpRibbon(HP_BAR_X0, HP_BAR_X0 + bl, HP_BAR_Y, HP_BAR_Y, z, HP_BAR_HW)} />);
    blocks.push(<polygon key={'c' + i} className="hp-block"
                         points={hpRibbon(HP_BAR_X0, 6, HP_BAR_Y, 0, z, HP_BAR_HW * 0.85)} />);
    poles.push(<polygon key={'p' + i} className="hp-pole"
                        points={hpRibbon(len + 4, len + 16, -2, -2, z, HP_POLE_HW)} />);
    tines.push(<polygon key={'t' + i} ref={tineRefs.current[i]} className="hp-tine"
                        points={hpRibbon(0, len, 0, 0, z, HP_TINE_HW)} />);
    hammers.push(<polygon key={'h' + i} ref={hamRefs.current[i]} className="hp-hammer"
                          opacity="0" points="" />);
    keys.push(<polygon key={'y' + i} ref={keyRefs.current[i]}
                       className={HP_IS_BLACK[i] ? 'hp-key black' : 'hp-key'}
                       points={hpRibbon(HP_KEY_X0, HP_KEY_X1, HP_KEY_Y, HP_KEY_Y, z,
                                      HP_IS_BLACK[i] ? HP_WKW * 0.28 : HP_WKW * 0.46)} />);
  }

  const rail = hpRibbon(HP_BAR_X0 - 12, HP_BAR_X0 - 2, HP_BAR_Y + 4, HP_BAR_Y + 4,
                      HP_HARP_W * 0.5, HP_HARP_W * 0.5 + HP_WKW);
  const cheek = hpRibbon(HP_KEY_X0 - 6, HP_BAR_X0 - 12, HP_KEY_Y - 10, HP_BAR_Y + 4,
                       HP_HARP_W * 0.5, HP_HARP_W * 0.5 + HP_WKW);

  return (
    <g className="hp">
      <polygon className="hp-case" points={cheek} />
      <polygon className="hp-rail" points={rail} />
      {bars}
      {blocks}
      {poles}
      {tines}
      {hammers}
      {keys}
      <text className="iv-cap" x="18" y="332">HARP · 88 TINES AND THEIR KEYS</text>
      <text className="iv-cap hp-count" ref={capRef} x="596" y="332">0 tines ringing</text>
    </g>
  );
}

Object.assign(window, { HarpView, HARP_N });
