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
const H_X0 = 120, H_Y0 = 236;         // the bottom note's block
const H_DX = 226, H_DY = 196;         // total travel across the compass
const H_LEN0 = 470, H_LENK = 0.60;    // longest tine, and how much it shrinks
const H_BAR_DY = 30;                  // tone bar above its tine, at the front
const H_SWING = 13;                   // px at full deflection

/* The action, in front of the harp and in the same projection: one key per
   tine, in the same order, because that is the relationship the picture is
   for. A key can be held long after its tine has gone quiet, which the
   motion alone cannot show and which is exactly what the pedal changes. */
// A CONSTANT offset, which is not a detail. This is an oblique projection,
// and under one of those a rigid translation in three dimensions is a rigid
// translation in two -- so the keyboard is the harp's own layout moved
// bodily, and every key lands in front of its own tine by construction.
// Scaling the offset with depth, which is what this did, slides each key
// away from the tine it belongs to and the two rows stop corresponding.
const K_DX = -132, K_DY = 54;         // where a key sits relative to its block
const K_W = 148;                      // key length, the same for every key
const K_BLACK = [1, 3, 6, 8, 10];     // pitch classes that are black keys, from A0

/* Eighty-eight assemblies in a couple of hundred pixels is about two
   pixels each, so weight has to be spent carefully: the bars are drawn
   thin and dim, as the striped surface they read as from this angle, and
   the tines get what is left, because the tines are the part that
   moves. */

function harpGeometry(i) {
  const t = i / (HARP_N - 1);
  /* Nearer notes get more room, which is the entire point of drawing this
     in depth: the bass tines are the long, slow, visibly moving ones.
     This eased the other way and crushed them into a solid slab while
     spending the space on the top octave, which is a row of stubs however
     it is drawn. */
  const d = 1.28 * t - 0.28 * t * t;
  /* How much vertical room this note has to itself, which is the derivative
     of the same curve. Nothing may be drawn thicker than this or the
     eighty-eight lines fuse into a surface -- which is what they did. */
  const pitch = H_DY * (1.28 - 0.56 * t) / (HARP_N - 1);
  return {
    x: H_X0 + d * H_DX,
    y: H_Y0 - d * H_DY,
    len: H_LEN0 * (1 - H_LENK * d),
    barDy: H_BAR_DY * (1 - 0.35 * d),
    pitch,
    depth: d,
  };
}

/* One assembly. Everything that moves is mutated through a ref, so a
   playing instrument never touches React. */
function isBlackKey(i) { return K_BLACK.indexOf((i + 9) % 12) >= 0; }

/* One key. Black keys sit slightly back and are drawn shorter, which is all
   the cue needed at this size. */
function HarpKey({ i, keyRef }) {
  const g = harpGeometry(i);
  const near = 1 - g.depth;
  const black = isBlackKey(i);
  /* Black keys are shorter and sit further back, which at this angle is the
     whole of what makes a keyboard legible. */
  const w = black ? K_W * 0.58 : K_W;
  const h = Math.max(1.0, g.pitch * (black ? 0.58 : 0.86));
  const x = g.x + K_DX + (black ? K_W * 0.42 : 0);
  const y = g.y + K_DY - h * 0.5;
  return (
    <rect ref={keyRef} className={black ? 'hp-key black' : 'hp-key'}
          x={x.toFixed(1)} y={y.toFixed(1)}
          width={w.toFixed(1)} height={h.toFixed(1)}
          opacity={(0.55 + 0.40 * near).toFixed(3)} />
  );
}

function HarpTine({ i, tineRef, barRef }) {
  const g = harpGeometry(i);
  const near = 1 - g.depth;
  return (
    <g className="hp-unit" opacity={(0.22 + 0.62 * near).toFixed(3)}>
      <line className="hp-bar" ref={barRef}
            x1={g.x - 6} y1={g.y - g.barDy}
            x2={g.x - 6 + g.len * 0.58} y2={g.y - g.barDy}
            strokeWidth={(g.pitch * 0.42).toFixed(2)} />
      <line className="hp-block"
            x1={g.x - 3} y1={g.y - g.barDy}
            x2={g.x - 3} y2={g.y}
            strokeWidth={(g.pitch * 0.52).toFixed(2)} />
      <line className="hp-tine" ref={tineRef}
            x1={g.x} y1={g.y} x2={g.x + g.len} y2={g.y}
            strokeWidth={(g.pitch * 0.60).toFixed(2)} />
    </g>
  );
}

function HarpView() {
  const lv = JuceBridge.useEventRef('levels',
    { harp: [], lastNote: 60, loNote: 21, voices: 0 });

  const tineRefs = useRef([]);
  const barRefs = useRef([]);
  const keyRefs = useRef([]);
  const hammerRefs = useRef([]);
  const capRef = useRef(null);
  if (tineRefs.current.length !== HARP_N) {
    tineRefs.current = Array.from({ length: HARP_N }, () => React.createRef());
    barRefs.current = Array.from({ length: HARP_N }, () => React.createRef());
    keyRefs.current = Array.from({ length: HARP_N }, () => React.createRef());
    hammerRefs.current = Array.from({ length: HARP_N }, () => React.createRef());
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
    const prevEnv = new Float32Array(HARP_N);
    /* One hammer per note, not one hammer. A chord is several notes struck
       together, and showing the last of them was showing a chord as a single
       note -- which is what it looked like.

       Each is fired by that tine's own level jumping, rather than by a note
       event, so nothing extra has to be sent from the engine and a note
       struck while already ringing still fires one. */
    const strikeT = new Float32Array(HARP_N).fill(99);
    let scale = 1e-3;

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

      /* Mapped in decibels, over a range wide enough to contain the thing
         worth seeing.

         Linearly, a tine shaken by the frame sits 30 to 40 dB under the one
         that was struck -- which is a hundredth of the height, indexes below
         any sensible visibility threshold, and disappears. So the drawing
         threw away the one behaviour it exists to show. Fifty-four decibels
         puts a note 30 dB down at a bit under half height, where it reads. */
      const RANGE = 54;
      const level = (v) => {
        if (!(v > 0)) return 0;
        const db = 20 * Math.log10(v * inv);
        return db <= -RANGE ? 0 : (db + RANGE) / RANGE;
      };

      /* Which keys are down, unpacked from the bitfield the engine packs. */
      const K = L.keys || [];
      const isDown = (i) => ((K[i >> 5] || 0) & (1 << (i & 31))) !== 0;

      for (let i = 0; i < HARP_N; i++) {
        const g = harpGeometry(i);
        const a = level(env[i]);
        const t = tineRefs.current[i].current;
        if (t) {
          t.setAttribute('y2', (g.y + a * H_SWING).toFixed(1));
          /* Struck and sympathetic are drawn differently on purpose. A tine
             that is moving with nobody holding its key is moving because the
             frame is shaking it, and that is the one thing this drawing
             exists to show -- with the pedal down it should be most of the
             instrument. */
          const cls = a <= 0.02 ? 'hp-tine'
                    : isDown(i) ? 'hp-tine struck'
                                : 'hp-tine symp';
          if (t.getAttribute('class') !== cls) t.setAttribute('class', cls);
          if (a > 0.02) t.setAttribute('style', `opacity:${(0.30 + 0.70 * a).toFixed(3)}`);
          else t.removeAttribute('style');
        }
        /* The bar is the fork's other prong: same frequency, opposite
           phase, and far less of it. */
        const b = barRefs.current[i].current;
        if (b) b.setAttribute('transform', `translate(0 ${(-a * H_SWING * 0.22).toFixed(2)})`);
      }

      for (let i = 0; i < HARP_N; i++) {
        const el = keyRefs.current[i].current;
        if (!el) continue;
        const down = isDown(i);
        const cls = (isBlackKey(i) ? 'hp-key black' : 'hp-key') + (down ? ' down' : '');
        if (el.getAttribute('class') !== cls) el.setAttribute('class', cls);
        /* A key that is down moves, along the same axis everything else does. */
        el.setAttribute('transform', down ? 'translate(4 2)' : 'translate(0 0)');
      }

      for (let i = 0; i < HARP_N; i++) {
        /* A jump of a few times, from something already audible: that is a
           hammer landing and not a note decaying. */
        if (env[i] > prevEnv[i] * 2.2 && env[i] * inv > 0.02) strikeT[i] = 0;
        prevEnv[i] = env[i];
        strikeT[i] += dt;

        const el = hammerRefs.current[i].current;
        if (!el) continue;
        const st = strikeT[i];
        if (st > 0.45) {
          if (el.getAttribute('opacity') !== '0') el.setAttribute('opacity', '0');
          continue;
        }
        const g = harpGeometry(i);
        let rise;
        if (st < 0.05) rise = st / 0.05;
        else if (st < 0.10) rise = 1;
        else rise = Math.max(0, 1 - (st - 0.10) / 0.35);
        const rest = 16;
        el.setAttribute('opacity', (0.25 + 0.75 * rise).toFixed(3));
        el.setAttribute('transform',
          `translate(${(g.x + g.len * 0.2).toFixed(1)} ${(g.y + rest - rest * rise).toFixed(1)})`);
      }

      if (capRef.current) {
        let held = 0;
        for (let i = 0; i < HARP_N; i++) if (isDown(i)) held++;
        const n = L.voices || 0;
        const txt = held > 0 && n > held
          ? held + ' struck · ' + (n - held) + ' answering'
          : n + (n === 1 ? ' tine' : ' tines') + ' moving';
        if (capRef.current.textContent !== txt) capRef.current.textContent = txt;
      }

      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(raf);
  }, []);

  const keys = [];
  for (let i = HARP_N - 1; i >= 0; i--)
    keys.push(<HarpKey key={'k' + i} i={i} keyRef={keyRefs.current[i]} />);

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
      {keys}
      {Array.from({ length: HARP_N }, (_, i) => {
        const g = harpGeometry(i);
        const h = Math.max(1.4, g.pitch * 1.5);
        return (
          <g key={'h' + i} ref={hammerRefs.current[i]} opacity="0">
            <rect className="hp-hammer" x={-g.len * 0.13} y="0"
                  width={(g.len * 0.13).toFixed(1)} height={h.toFixed(1)}
                  rx={(h * 0.4).toFixed(1)} />
            <rect className="hp-tip" x="-2" y={(-h * 0.5).toFixed(1)}
                  width={(h * 1.1).toFixed(1)} height={(h * 2).toFixed(1)}
                  rx={(h * 0.5).toFixed(1)} />
          </g>
        );
      })}
      <text className="iv-cap" x={10} y={H_Y0 + K_DY + 24}>HARP · 88 TINES AND THEIR KEYS</text>
      <text className="iv-cap hp-count" ref={capRef}
            x={H_X0 + H_DX + 96} y={H_Y0 - H_DY - 14}>0 tines moving</text>
    </g>
  );
}

Object.assign(window, { HarpView, HARP_N, harpGeometry });
