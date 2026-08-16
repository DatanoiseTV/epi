/* ============================================================
   Epi · the harp
   ============================================================
   Eighty-eight tine-and-tonebar assemblies, drawn in oblique
   projection, each one moving because the engine says it is.

   The single-tine cutaway shows what a note IS. This shows what the
   INSTRUMENT is, and it shows one thing that cannot be seen any other
   way: every tine is bolted to the same frame, so striking one shakes
   the rest. With the pedal down the whole harp answers, and that is
   most of what a pedalled chord actually is.

   Oblique rather than perspective on purpose. A vanishing point would
   foreshorten the bass tines, which are the long ones and the ones
   whose length is the point -- pitch here is length, and a projection
   that distorts length would be drawing the wrong thing.
   ============================================================ */

const HARP_N = 88;

/* Where the assemblies sit. Successive notes step up and to the right,
   which is the oblique projection, and the whole run occupies rather
   less height than it does width because the viewBox is a wide strip. */
const H_X0 = 96, H_Y0 = 268;          // the bottom note's block
const H_DX = 176, H_DY = 150;         // total travel across the compass
const H_LEN0 = 430, H_LENK = 0.62;    // longest tine, and how much it shrinks
const H_BAR_DY = 26;                  // tone bar above its tine, at the front
const H_SWING = 15;                   // px at full deflection

function harpGeometry(i) {
  const t = i / (HARP_N - 1);
  /* Depth eased so the front of the instrument -- where the long, slow,
     visibly moving tines are -- gets more of the picture than the top
     octave, which is a row of stubs however it is drawn. */
  const d = t * t * 0.55 + t * 0.45;
  return {
    x: H_X0 + d * H_DX,
    y: H_Y0 - d * H_DY,
    len: H_LEN0 * (1 - H_LENK * d),
    barDy: H_BAR_DY * (1 - 0.45 * d),
    depth: d,
  };
}

/* One assembly. Everything that moves is mutated through a ref, so a
   playing instrument never touches React. */
function HarpTine({ i, tineRef, barRef }) {
  const g = harpGeometry(i);
  const near = 1 - g.depth;
  return (
    <g className="hp-unit" opacity={(0.30 + 0.55 * near).toFixed(3)}>
      <line className="hp-bar" ref={barRef}
            x1={g.x - 6} y1={g.y - g.barDy}
            x2={g.x - 6 + g.len * 0.74} y2={g.y - g.barDy}
            strokeWidth={(1.1 + 1.7 * near).toFixed(2)} />
      <line className="hp-block"
            x1={g.x - 3} y1={g.y - g.barDy}
            x2={g.x - 3} y2={g.y}
            strokeWidth={(1.6 + 2.2 * near).toFixed(2)} />
      <line className="hp-tine" ref={tineRef}
            x1={g.x} y1={g.y} x2={g.x + g.len} y2={g.y}
            strokeWidth={(1.0 + 1.6 * near).toFixed(2)} />
    </g>
  );
}

function HarpView() {
  const lv = JuceBridge.useEventRef('levels',
    { harp: [], lastNote: 60, loNote: 21, voices: 0 });

  const tineRefs = useRef([]);
  const barRefs = useRef([]);
  const hammerRef = useRef(null);
  const capRef = useRef(null);
  if (tineRefs.current.length !== HARP_N) {
    tineRefs.current = Array.from({ length: HARP_N }, () => React.createRef());
    barRefs.current = Array.from({ length: HARP_N }, () => React.createRef());
  }

  useEffect(() => {
    let raf = 0, prev = performance.now();
    /* Per-tine envelopes, and one shared scale.

       Scaling each tine against its own peak would make the top octave
       swing as far as the bottom, which is exactly the thing that is not
       true: a bass tine moves millimetres and a treble tine a fraction
       of one. One decaying maximum across the instrument keeps that
       relationship visible while still adapting to how hard it is being
       played. */
    const env = new Float32Array(HARP_N);
    let scale = 1e-3;
    let strikeT = 99, strikeIdx = -1;

    const frame = (now) => {
      const dt = Math.min(0.1, (now - prev) / 1000); prev = now;
      const L = lv.current || {};
      const H = L.harp || [];
      const lo = L.loNote || 21;

      let peak = 1e-6;
      for (let i = 0; i < HARP_N; i++) {
        const v = i < H.length ? Math.abs(H[i]) : 0;
        /* Fast up, slow down: the engine hands over a peak per block, and
           without the release the display strobes at the block rate. */
        env[i] = v > env[i] ? v : env[i] + (v - env[i]) * Math.min(1, dt * 6);
        if (env[i] > peak) peak = env[i];
      }
      scale = peak > scale ? peak : scale + (peak - scale) * Math.min(1, dt * 0.6);
      const inv = 1 / Math.max(scale, 1e-6);

      for (let i = 0; i < HARP_N; i++) {
        const g = harpGeometry(i);
        const a = Math.min(1, env[i] * inv);
        const t = tineRefs.current[i].current;
        if (t) {
          t.setAttribute('y2', (g.y + a * H_SWING).toFixed(1));
          t.setAttribute('class', a > 0.02 ? 'hp-tine live' : 'hp-tine');
          if (a > 0.02) t.setAttribute('style', `opacity:${(0.35 + 0.65 * a).toFixed(3)}`);
          else t.removeAttribute('style');
        }
        /* The bar is the fork's other prong: same frequency, opposite
           phase, and far less of it. */
        const b = barRefs.current[i].current;
        if (b) b.setAttribute('transform', `translate(0 ${(-a * H_SWING * 0.22).toFixed(2)})`);
      }

      /* A hammer on the note that was last struck, so the mechanism is
         visible without drawing eighty-eight of them. */
      const note = (L.lastNote || 60) - lo;
      if (note !== strikeIdx && (L.strikes !== undefined)) { /* index only */ }
      if (L.lastNote !== undefined && note !== strikeIdx) { strikeIdx = note; strikeT = 0; }
      strikeT += dt;
      if (hammerRef.current && strikeIdx >= 0 && strikeIdx < HARP_N) {
        const g = harpGeometry(strikeIdx);
        let rise;
        if (strikeT < 0.05) rise = strikeT / 0.05;
        else if (strikeT < 0.10) rise = 1;
        else if (strikeT < 0.45) rise = Math.max(0, 1 - (strikeT - 0.10) / 0.35);
        else rise = 0;
        const rest = 22 * (1 - 0.4 * g.depth);
        hammerRef.current.setAttribute('opacity', (0.15 + 0.75 * rise).toFixed(3));
        hammerRef.current.setAttribute('transform',
          `translate(${(g.x + g.len * 0.2).toFixed(1)} ${(g.y + rest - rest * rise).toFixed(1)})`);
      }

      if (capRef.current) {
        const n = L.voices || 0;
        const txt = n + (n === 1 ? ' tine' : ' tines') + ' moving';
        if (capRef.current.textContent !== txt) capRef.current.textContent = txt;
      }

      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(raf);
  }, []);

  const units = [];
  /* Drawn from the top of the compass down, so the near, long, bass
     assemblies are painted last and overlap the ones behind them. */
  for (let i = HARP_N - 1; i >= 0; i--)
    units.push(<HarpTine key={i} i={i}
                         tineRef={tineRefs.current[i]}
                         barRef={barRefs.current[i]} />);

  return (
    <g className="hp">
      {units}
      <g ref={hammerRef} opacity="0">
        <rect className="hp-hammer" x="-16" y="0" width="22" height="6" rx="3" />
        <rect className="hp-tip" x="2" y="-3" width="7" height="11" rx="3" />
      </g>
      <text className="iv-cap" x={H_X0 - 4} y={H_Y0 + 22}>HARP · 88 TINES</text>
      <text className="iv-cap hp-count" ref={capRef}
            x={H_X0 + H_DX + 96} y={H_Y0 - H_DY - 14}>0 tines moving</text>
    </g>
  );
}

Object.assign(window, { HarpView, HARP_N, harpGeometry });
