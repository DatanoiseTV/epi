/* ============================================================
   Didge · interactive atoms — Knob, PKnob, PHead, meters
   ============================================================ */

/* ---- geometry helpers ---- */
function polar(cx, cy, r, deg) {
  const rad = (deg) * Math.PI / 180;
  return [cx + r * Math.sin(rad), cy - r * Math.cos(rad)];
}
function arcPath(cx, cy, r, a0, a1) {
  const [x0, y0] = polar(cx, cy, r, a0);
  const [x1, y1] = polar(cx, cy, r, a1);
  const large = (a1 - a0) > 180 ? 1 : 0;
  return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${r} ${r} 0 ${large} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}

const A0 = -135, A1 = 135, SWEEP = A1 - A0;

/* Shared vertical-drag gesture: 240 px covers the full 0..1 range, shift
   quarters the rate for fine trims. Used by Knob and by the draggable
   zones on the instrument cutaway so both feel identical. */
function beginVerticalDrag(e, startValue, onChange, onEnd) {
  e.preventDefault();
  const pt = e.touches ? e.touches[0] : e;
  const y0 = pt.clientY;
  const move = (ev) => {
    const p = ev.touches ? ev.touches[0] : ev;
    const fine = ev.shiftKey ? 0.25 : 1;
    const dv = (y0 - p.clientY) / 240 * fine;
    onChange && onChange(Math.max(0, Math.min(1, startValue + dv)));
  };
  const up = () => {
    onEnd && onEnd();
    window.removeEventListener('pointermove', move);
    window.removeEventListener('pointerup', up);
  };
  window.addEventListener('pointermove', move);
  window.addEventListener('pointerup', up);
}

/* ---- Knob ----
   value is normalised 0..1; format converts normalised -> display string.
   bipolar draws the arc out from top-centre. */
function Knob({ value = 0.5, onChange, size = 'md', label, format, bipolar = false, alt = false, defaultValue = 0.5 }) {
  const [drag, setDrag] = useState(false);

  const D = size === 'lg' ? 82 : size === 'sm' ? 46 : 58;
  const sw = size === 'lg' ? 3.6 : size === 'sm' ? 2.6 : 3.0;
  const c = D / 2;
  const R = c - sw - 2;
  const ang = A0 + value * SWEEP;
  const bodyR = R - (size === 'lg' ? 9 : size === 'sm' ? 6 : 7.5);

  const [px, py] = polar(c, c, R, ang);
  const pointerInner = bodyR - (size === 'lg' ? 8 : 4);
  const [pix, piy] = polar(c, c, pointerInner, ang);

  const onDown = useCallback((e) => {
    setDrag(true);
    beginVerticalDrag(e, value, onChange, () => setDrag(false));
  }, [value, onChange]);

  const onDbl = () => onChange && onChange(defaultValue);

  const ticks = [];
  const tickN = size === 'sm' ? 0 : 7;
  for (let i = 0; i < tickN; i++) {
    const a = A0 + (i / (tickN - 1)) * SWEEP;
    const [x1, y1] = polar(c, c, R + 3.5, a);
    const [x2, y2] = polar(c, c, R + 6.0, a);
    ticks.push(<line key={i} className="k-tick" x1={x1} y1={y1} x2={x2} y2={y2} strokeWidth="1" />);
  }

  const arc = bipolar
    ? (value >= 0.5 ? arcPath(c, c, R, 0, ang) : arcPath(c, c, R, ang, 0))
    : arcPath(c, c, R, A0, ang);

  const valTxt = format ? format(value) : Math.round(value * 100) + '%';

  return (
    <div className={'knob' + (alt ? ' alt' : '')}>
      <div className={'dial' + (drag ? ' dragging' : '')}
           onPointerDown={onDown} onDoubleClick={onDbl}
           style={{ width: D, height: D }}>
        <div className="kval">{valTxt}</div>
        <svg width={D} height={D} viewBox={`0 0 ${D} ${D}`} style={{ overflow: 'visible' }}>
          {ticks}
          <path className="k-track" d={arcPath(c, c, R, A0, A1)} fill="none" strokeWidth={sw} strokeLinecap="round" />
          {Math.abs(value - (bipolar ? 0.5 : 0)) > 0.001 &&
            <path className="k-arc" d={arc} fill="none" strokeWidth={sw} strokeLinecap="round" />}
          <circle className="k-body-out" cx={c} cy={c} r={bodyR + 2} />
          <circle className="k-body-in" cx={c} cy={c} r={bodyR} />
          <line className="k-point" x1={pix} y1={piy} x2={px - (px - c) * 0.06} y2={py - (py - c) * 0.06}
                strokeWidth={size === 'lg' ? 2.2 : 1.8} strokeLinecap="round" />
          <circle className="k-hub" cx={c} cy={c} r={size === 'lg' ? 2.6 : 2} />
        </svg>
      </div>
      {label !== null && <div className="klabel">{label !== undefined ? label : ''}</div>}
    </div>
  );
}

/* ---- Parameter knob ----
   Everything (label, default, formatter, bipolarity) comes from the one
   PARAMS table, so a range change in C++ is mirrored in exactly one place. */
function PKnob({ id, size, alt }) {
  const spec = PARAMS[id];
  const [v, set] = JuceBridge.useJuceSlider(id);
  return (
    <Knob value={v} onChange={set} size={size} alt={alt}
          label={spec.label} format={spec.format}
          bipolar={!!spec.bipolar} defaultValue={spec.def} />
  );
}

/* ---- Panel section header ---- */
/* ---- Toggle chip, bound straight to a bool parameter ---- */
function PToggle({ id, label }) {
  const [on, setOn] = JuceBridge.useJuceToggle(id);
  return (
    <button className="chip" data-on={on ? '1' : '0'} onClick={() => setOn(!on)}>
      <span className="led" />{label}
    </button>
  );
}

/* ---- Segmented control, bound to a choice parameter ---- */
function PSeg({ id, options, compact = false }) {
  const [idx, setIdx] = JuceBridge.useJuceChoice(id, options);
  return (
    <div className={'seg' + (compact ? ' compact' : '')}>
      {options.map((o, i) => (
        <button key={o} className={i === idx ? 'on' : ''} onClick={() => setIdx(i)}>{o}</button>
      ))}
    </div>
  );
}

/* ---- Cycling selector for choice parameters ----
   A segmented control needs one cell per option, which does not survive a
   narrow panel once there are more than three or four. This shows only the
   current value and steps through the list, so it stays the same width
   whatever the parameter offers. */
function PCycle({ id, options, label }) {
  const [idx, setIdx] = JuceBridge.useJuceChoice(id, options);
  const step = (d) => setIdx((idx + d + options.length) % options.length);
  return (
    <div className="cyc">
      {label && <span className="cyclabel">{label}</span>}
      <div className="cycbody">
        <button className="cycarrow" onClick={() => step(-1)} title="Previous">&#8249;</button>
        <span className="cycval">{options[idx]}</span>
        <button className="cycarrow" onClick={() => step(1)} title="Next">&#8250;</button>
      </div>
    </div>
  );
}

function PHead({ title, meta }) {
  return (
    <div className="phead">
      <h2>{title}</h2>
      <span className="hrule" />
      {meta && <span className="hmeta">{meta}</span>}
    </div>
  );
}

/* ---- Vertical output meter ----
   Subscribes straight to the levels event and animates from a rAF tick.
   Routing 30 Hz telemetry through React state would repaint the whole
   panel tree at the event rate and fight the render loop. */
function LiveMeter({ channel = 0, label }) {
  const lv = JuceBridge.useEventRef('levels', { out: [-90, -90] });
  const barRef = useRef(null);
  const smooth = useRef(0);
  useEffect(() => {
    let raf = 0, last = performance.now();
    const tick = (now) => {
      const dt = Math.min(0.1, (now - last) / 1000); last = now;
      const db = (lv.current.out && lv.current.out[channel] !== undefined) ? lv.current.out[channel] : -90;
      const target = Math.max(0, Math.min(1, (db + 60) / 66));
      const k = target > smooth.current ? 1 - Math.exp(-dt / 0.02) : 1 - Math.exp(-dt / 0.20);
      smooth.current += (target - smooth.current) * k;
      if (barRef.current) barRef.current.style.height = (smooth.current * 100) + '%';
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [channel]);
  return (
    <div className="meter">
      <div className="mtrack"><div className="mfill" ref={barRef} /></div>
      {label && <div className="mlabel">{label}</div>}
    </div>
  );
}

/* ---- Horizontal live bar ----
   `field` names a scalar on the levels event; `full` is the value that
   maps to a filled bar (metres for lipOpen, m^3/s for flow). */
function LiveBar({ field, full = 1, label, unit, digits = 2, scale = 1 }) {
  const lv = JuceBridge.useEventRef('levels', {});
  const barRef = useRef(null);
  const numRef = useRef(null);
  const smooth = useRef(0);
  useEffect(() => {
    let raf = 0, last = performance.now();
    const tick = (now) => {
      const dt = Math.min(0.1, (now - last) / 1000); last = now;
      const raw = Number(lv.current[field]) || 0;
      const target = Math.max(0, Math.min(1, raw / full));
      const k = target > smooth.current ? 1 - Math.exp(-dt / 0.03) : 1 - Math.exp(-dt / 0.12);
      smooth.current += (target - smooth.current) * k;
      if (barRef.current) barRef.current.style.width = (smooth.current * 100) + '%';
      if (numRef.current) numRef.current.textContent = (raw * scale).toFixed(digits) + (unit || '');
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [field, full]);
  return (
    <div className="hbar">
      <div className="hbrow">
        <span className="hblabel">{label}</span>
        <span className="hbval" ref={numRef}>—</span>
      </div>
      <div className="hbtrack"><div className="hbfill" ref={barRef} /></div>
    </div>
  );
}

Object.assign(window, { Knob, PKnob, PToggle, PSeg, PCycle, PHead, LiveMeter, LiveBar,
                        polar, arcPath, beginVerticalDrag, A0, A1, SWEEP });
