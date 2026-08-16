/* ============================================================
   Epi · the tone generator, drawn from the model

   The hero is the thing the instrument actually is: a hammer, a tine, a tone
   bar, and a magnet whose field the tine is moving through. The field curve is
   not an illustration -- it is the array the engine computes and sends up, the
   same numbers the audio is being made from. Dragging the tine in it moves the
   voicing screw, which is the single control that most decides how a Rhodes
   sounds.

   Everything animates from one rAF loop that mutates SVG attributes in place.
   The 60 Hz telemetry never touches React state.
   ============================================================ */

const VB_W = 1280, VB_H = 300;

/* Layout, in SVG user units. */
const BAR_X0 = 120, BAR_X1 = 540, BAR_Y = 74;      // tone bar
const TINE_X0 = 150, TINE_X1 = 700, TINE_Y = 176;  // tine at rest
const BLOCK_X = 132;                                // the aluminium block
const HAMMER_X = 268;
const POLE_X = 742;                                 // magnet face
const FIELD_X0 = 762, FIELD_X1 = 1244;              // field plot
const FIELD_CY = 176, FIELD_H = 108;
const TINE_SWING = 46;                              // px for a full swing

function InstrumentView({ pickupPos, setPickupPos, pickupDist }) {
  const lv = JuceBridge.useEventRef('levels', { out: [-90, -90], field: [], tip: 0, offset: 0, voices: 0 });
  const P = useRef({ pickupPos, pickupDist });
  P.current = { pickupPos, pickupDist };

  const tineRef = useRef(null), tipRef = useRef(null), orbitRef = useRef(null);
  const hammerRef = useRef(null), barRef = useRef(null);
  const fieldRef = useRef(null), markerRef = useRef(null), traceRef = useRef(null);
  const gapRef = useRef(null), noteRef = useRef(null), voiceRef = useRef(null);

  useEffect(() => {
    let raf = 0, last = performance.now();
    let level = 0, phase = 0, glow = 0;
    const trail = new Array(72).fill(0);
    let trailAt = 0;

    const tick = (now) => {
      const dt = Math.min(0.1, (now - last) / 1000); last = now;
      const L = lv.current || {};
      const db = (L.out && L.out[0]) || -90;
      const target = Math.max(0, Math.min(1, (db + 60) / 60));
      level += (target - level) * (1 - Math.exp(-dt / 0.09));
      glow  += (target - glow)  * (1 - Math.exp(-dt / 0.14));

      /* The real note sits far above the frame rate, so the drawing is geared
         down by a fixed ratio: relative pitch still reads without strobing. */
      phase += 2 * Math.PI * 5.5 * dt;

      const swing = Math.sin(phase) * level;
      const y = TINE_Y + swing * TINE_SWING;

      /* The tine bends; it does not pivot. A clamped-free beam's first mode is
         flat at the clamp and steepest at the tip, so the curve is drawn from
         the mode shape rather than as a straight line. */
      if (tineRef.current) {
        let d = `M ${TINE_X0} ${TINE_Y}`;
        const N = 26;
        for (let i = 1; i <= N; i++) {
          const u = i / N;
          const shape = (1 - Math.cos(1.8751 * u * Math.PI / 1.8751)) * 0.5;
          const w = Math.pow(u, 2) * (3 - 2 * u);
          const x = TINE_X0 + (TINE_X1 - TINE_X0) * u;
          d += ` L ${x.toFixed(1)} ${(TINE_Y + swing * TINE_SWING * w).toFixed(1)}`;
        }
        tineRef.current.setAttribute('d', d);
      }
      if (tipRef.current) { tipRef.current.setAttribute('cy', y.toFixed(1)); }

      /* The tip traces an ellipse, not a line: the two polarisations run a few
         cents apart, so the orbit slowly precesses. */
      if (orbitRef.current) {
        const rx = 3 + 7 * level, ry = TINE_SWING * level;
        orbitRef.current.setAttribute('rx', rx.toFixed(1));
        orbitRef.current.setAttribute('ry', Math.max(0.5, ry).toFixed(1));
        orbitRef.current.setAttribute('opacity', (0.30 * level).toFixed(3));
      }

      /* Hammer: rides in during the attack and drops away. */
      if (hammerRef.current) {
        const k = Math.max(0, level - 0.55) / 0.45;
        hammerRef.current.setAttribute('transform',
          `translate(0 ${(26 - 22 * Math.min(1, k)).toFixed(1)})`);
      }

      /* The tone bar is enslaved to the tine: same frequency, opposite phase,
         and far more heavily damped. */
      if (barRef.current)
        barRef.current.setAttribute('transform',
          `translate(0 ${(-swing * 7).toFixed(2)})`);

      /* ---- the field, straight from the engine ---- */
      const F = (L.field && L.field.length > 8) ? L.field : null;
      if (F && fieldRef.current) {
        let d = '';
        for (let i = 0; i < F.length; i++) {
          const x = FIELD_X0 + (FIELD_X1 - FIELD_X0) * (i / (F.length - 1));
          const yy = FIELD_CY + FIELD_H * 0.5 - Math.min(1.6, F[i]) * FIELD_H * 0.5;
          d += (i ? ' L ' : 'M ') + x.toFixed(1) + ' ' + yy.toFixed(1);
        }
        fieldRef.current.setAttribute('d', d);
      }

      /* Where the tine sits on that curve, right now. This is the whole
         instrument in one dot: the shape of the field under the swing is the
         waveform that comes out. */
      const off = P.current.pickupPos;          // -1..1 in pole half-widths
      const span = 4;                            // table spans +/-4 half-widths
      const u = (off + swing * 0.9 + span) / (2 * span);
      if (markerRef.current && F) {
        const x = FIELD_X0 + (FIELD_X1 - FIELD_X0) * Math.max(0, Math.min(1, u));
        const idx = Math.max(0, Math.min(F.length - 1, Math.round(u * (F.length - 1))));
        const yy = FIELD_CY + FIELD_H * 0.5 - Math.min(1.6, F[idx]) * FIELD_H * 0.5;
        markerRef.current.setAttribute('cx', x.toFixed(1));
        markerRef.current.setAttribute('cy', yy.toFixed(1));
        markerRef.current.setAttribute('opacity', (0.25 + 0.75 * level).toFixed(3));

        trail[trailAt = (trailAt + 1) % trail.length] = F[idx];
        if (traceRef.current) {
          let td = '';
          for (let i = 0; i < trail.length; i++) {
            const j = (trailAt + 1 + i) % trail.length;
            const tx = FIELD_X0 + (FIELD_X1 - FIELD_X0) * (i / (trail.length - 1));
            const ty = FIELD_CY + FIELD_H + 34 - Math.min(1.6, trail[j]) * 26;
            td += (i ? ' L ' : 'M ') + tx.toFixed(1) + ' ' + ty.toFixed(1);
          }
          traceRef.current.setAttribute('d', td);
          traceRef.current.setAttribute('opacity', (0.30 + 0.60 * level).toFixed(3));
        }
      }

      if (gapRef.current) {
        const g = 0.6 + 4.4 * P.current.pickupDist;
        gapRef.current.setAttribute('width', Math.max(2, g * 5).toFixed(1));
      }
      if (voiceRef.current) voiceRef.current.textContent = (L.voices || 0) + ' voices';

      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  /* Dragging the tine in the field is the voicing screw. Same gesture as the
     knobs, so it feels the same. */
  const dragHeight = useCallback((e) => {
    const cur = (P.current.pickupPos + 1) * 0.5;
    beginVerticalDrag(e, cur, (n) => setPickupPos(n));
  }, [setPickupPos]);

  return (
    <div className="ivwrap">
      <div className="ivchip left"><span className="lbl">TONE GENERATOR</span></div>
      <div className="ivchip right"><span className="lbl" ref={voiceRef}>0 voices</span></div>

      <svg className="ivsvg" viewBox={`0 0 ${VB_W} ${VB_H}`} preserveAspectRatio="xMidYMid meet">
        <defs>
          <linearGradient id="steel" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="#cfd8e4" /><stop offset="0.45" stopColor="#8b98a9" />
            <stop offset="1" stopColor="#4a5563" />
          </linearGradient>
          <linearGradient id="fieldg" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="#5ad2ff" stopOpacity="0.55" />
            <stop offset="1" stopColor="#5ad2ff" stopOpacity="0.02" />
          </linearGradient>
          <radialGradient id="poleglow">
            <stop offset="0" stopColor="#7fe3ff" stopOpacity="0.55" />
            <stop offset="1" stopColor="#7fe3ff" stopOpacity="0" />
          </radialGradient>
        </defs>

        {/* ---- tone bar ---- */}
        <g ref={barRef}>
          <rect className="iv-bar" x={BAR_X0} y={BAR_Y} width={BAR_X1 - BAR_X0} height="15" rx="4" />
        </g>
        <text className="iv-cap" x={BAR_X0} y={BAR_Y - 10}>TONE BAR</text>

        {/* ---- block ---- */}
        <rect className="iv-block" x={BLOCK_X} y={BAR_Y + 12} width="26" height={TINE_Y - BAR_Y - 4} rx="3" />

        {/* ---- hammer ---- */}
        <g ref={hammerRef}>
          <rect className="iv-hammer" x={HAMMER_X} y={TINE_Y + 30} width="46" height="15" rx="7" />
          <rect className="iv-hammertip" x={HAMMER_X + 40} y={TINE_Y + 26} width="13" height="23" rx="6" />
        </g>
        <text className="iv-cap" x={HAMMER_X} y={TINE_Y + 68}>HAMMER</text>

        {/* ---- tine ---- */}
        <ellipse ref={orbitRef} className="iv-orbit" cx={TINE_X1} cy={TINE_Y} rx="6" ry="1" />
        <path ref={tineRef} className="iv-tine" d="" />
        <circle ref={tipRef} className="iv-tip" cx={TINE_X1} cy={TINE_Y} r="6" />
        <text className="iv-cap" x={TINE_X0} y={TINE_Y + 40}>TINE</text>

        {/* ---- magnet ---- */}
        <circle className="iv-poleglow" cx={POLE_X + 8} cy={TINE_Y} r="54" fill="url(#poleglow)" />
        <path className="iv-pole" d={`M ${POLE_X + 40} ${TINE_Y - 46} L ${POLE_X + 6} ${TINE_Y - 9}
                                      L ${POLE_X + 6} ${TINE_Y + 9} L ${POLE_X + 40} ${TINE_Y + 46} Z`} />
        <rect ref={gapRef} className="iv-gap" x={TINE_X1 + 4} y={TINE_Y - 3} width="10" height="6" />
        <text className="iv-cap" x={POLE_X + 4} y={TINE_Y + 74}>PICKUP</text>

        {/* ---- the field the tine is moving through ---- */}
        <path ref={fieldRef} className="iv-field" d="" />
        <circle ref={markerRef} className="iv-marker" cx={FIELD_X0} cy={FIELD_CY} r="5.5" />
        <path ref={traceRef} className="iv-trace" d="" />
        <text className="iv-cap" x={FIELD_X0} y={FIELD_CY - FIELD_H * 0.5 - 12}>
          MAGNETIC FIELD ACROSS THE POLE
        </text>
        <text className="iv-cap dim" x={FIELD_X0} y={FIELD_CY + FIELD_H + 56}>
          OUTPUT WAVEFORM = THE FIELD UNDER THE SWING
        </text>

        {/* ---- drag zone: the voicing screw ---- */}
        <g className="iv-zone">
          <rect x={TINE_X1 - 90} y={TINE_Y - 70} width="180" height="140"
                onPointerDown={dragHeight} />
          <text className="iv-dragcap" x={TINE_X1 - 78} y={TINE_Y - 76}>HEIGHT — DRAG</text>
        </g>
      </svg>

      <div className="ivhint">
        drag the tine to set its height in the field · double-click a knob to reset · hold shift for fine
      </div>
    </div>
  );
}

window.InstrumentView = InstrumentView;
