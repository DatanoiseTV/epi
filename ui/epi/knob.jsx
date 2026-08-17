/* ============================================================
   Epi · interactive atoms — Knob, PKnob, faders, meters
   ============================================================
   The knob follows the panel design: a small dark body ring, a
   thin outer track arc, the value drawn as a gold arc over it,
   and a pale pointer. Label above the value, both under the
   dial. All geometry is scaled from the 52 px reference so the
   46 px header knob and any larger variant keep proportions.
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
   quarters the rate for fine trims. */
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
function Knob({ value = 0.5, onChange, size = 'md', label, format, bipolar = false, defaultValue = 0.5, showValue = true }) {
  const [drag, setDrag] = useState(false);

  const D = size === 'lg' ? 62 : size === 'sm' ? 46 : 52;
  const s = D / 52;                       // everything scales off the reference
  const c = D / 2;
  const rBody = 16.5 * s, rArc = 19 * s, sw = 2.6 * s;
  const ang = A0 + value * SWEEP;

  const onDown = useCallback((e) => {
    setDrag(true);
    beginVerticalDrag(e, value, onChange, () => setDrag(false));
  }, [value, onChange]);

  const onDbl = () => onChange && onChange(defaultValue);

  const arc = bipolar
    ? (value >= 0.5 ? arcPath(c, c, rArc, 0, ang) : arcPath(c, c, rArc, ang, 0))
    : arcPath(c, c, rArc, A0, ang);

  const valTxt = format ? format(value) : Math.round(value * 100) + '%';

  return (
    <div className="knob" style={{ width: size === 'lg' ? 72 : 62 }}>
      <div className={'dial' + (drag ? ' dragging' : '')}
           onPointerDown={onDown} onDoubleClick={onDbl}
           style={{ width: D, height: D }}>
        <svg width={D} height={D} viewBox={`0 0 ${D} ${D}`} style={{ overflow: 'visible', display: 'block' }}>
          <circle className="k-body" cx={c} cy={c} r={rBody} />
          <path className="k-track" d={arcPath(c, c, rArc, A0, A1)} fill="none" strokeWidth={sw} strokeLinecap="round" />
          {Math.abs(value - (bipolar ? 0.5 : 0)) > 0.004 &&
            <path className="k-arc" d={arc} fill="none" strokeWidth={sw} strokeLinecap="round" />}
          <line className="k-point"
                x1={c} y1={c - (5.5 * s)} x2={c} y2={c - (13.5 * s)}
                strokeWidth={2 * s} strokeLinecap="round"
                transform={`rotate(${ang} ${c} ${c})`} />
        </svg>
      </div>
      {label !== null && <div className="klabel">{label !== undefined ? label : ''}</div>}
      {showValue && <div className="kval">{valTxt}</div>}
    </div>
  );
}

/* ---- Parameter knob ----
   Everything (label, default, formatter, bipolarity) comes from the one
   PARAMS table, so a range change in C++ is mirrored in exactly one place. */
function PKnob({ id, size, label }) {
  const spec = PARAMS[id];
  const [v, set] = JuceBridge.useJuceSlider(id);
  return (
    <Knob value={v} onChange={set} size={size}
          label={label || spec.label} format={spec.format}
          bipolar={!!spec.bipolar} defaultValue={spec.def} />
  );
}

/* ---- Parameter fader (levels-style vertical bar) ---- */
function PFader({ id, label }) {
  const spec = PARAMS[id];
  const [v, set] = JuceBridge.useJuceSlider(id);
  const onDown = (e) => beginVerticalDrag(e, v, set);
  const onDbl = () => set(spec.def);
  return (
    <div className="fader" onPointerDown={onDown} onDoubleClick={onDbl}>
      <div className="ftrack"><div className="ffill" style={{ height: (v * 100).toFixed(1) + '%' }} /></div>
      <div className="flabel">{label || spec.label}</div>
      <div className="fval">{spec.format(v)}</div>
    </div>
  );
}

/* ---- Segmented control, bound to a choice parameter ---- */
function PSeg({ id, options, wide = false }) {
  const [idx, setIdx] = JuceBridge.useJuceChoice(id, options);
  return (
    <div className={'seg' + (wide ? ' wide' : '')}>
      {options.map((o, i) => (
        <button key={o} className={i === idx ? 'on' : ''} onClick={() => setIdx(i)}>{o}</button>
      ))}
    </div>
  );
}

/* ---- Cycling selector for choice parameters ---- */
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

/* ---- Header stereo meter pair ----
   Subscribes straight to the levels event and animates from a rAF tick.
   Routing 30 Hz telemetry through React state would repaint the whole
   panel tree at the event rate and fight the render loop. */
function HeaderMeters() {
  const lv = JuceBridge.useEventRef('levels', { out: [-90, -90] });
  const lRef = useRef(null), rRef = useRef(null);
  const sm = useRef([0, 0]);
  useEffect(() => {
    let raf = 0, last = performance.now();
    const tick = (now) => {
      const dt = Math.min(0.1, (now - last) / 1000); last = now;
      const out = lv.current.out || [-90, -90];
      [lRef, rRef].forEach((r, ch) => {
        const db = out[ch] !== undefined ? out[ch] : -90;
        const target = Math.max(0, Math.min(1, (db + 60) / 66));
        const k = target > sm.current[ch] ? 1 - Math.exp(-dt / 0.02) : 1 - Math.exp(-dt / 0.20);
        sm.current[ch] += (target - sm.current[ch]) * k;
        if (r.current) r.current.style.height = (sm.current[ch] * 100) + '%';
      });
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);
  return (
    <div className="meters">
      <div className="mtrack"><div className="mfill" ref={lRef} /></div>
      <div className="mtrack"><div className="mfill" ref={rRef} /></div>
    </div>
  );
}

/* ---- Horizontal live bar ----
   `field` names a scalar on the levels event. */
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

Object.assign(window, { Knob, PKnob, PFader, PSeg, PCycle, PHead, HeaderMeters, LiveBar,
                        polar, arcPath, beginVerticalDrag, A0, A1, SWEEP });
