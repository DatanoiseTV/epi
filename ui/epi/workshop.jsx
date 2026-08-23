/* ============================================================
   Epi · the workshops
   ============================================================
   Two benches behind the panel. The TINE workshop re-cuts the
   steel: length retunes as 1/L^2, gauge holds the nominal pitch
   (a regauged tine is re-cut for its note) and instead moves the
   modal mass and the shear that pulls the overtones flat. The
   PICKUP workshop mis-adjusts the transducers: each pickup's
   height and gap ride as offsets on the panel knobs, and the
   winding scales that pickup's contribution the way a coil with
   more or fewer turns does.

   The length lane zooms -- at +/-50 cents the full lane height
   is a third of a semitone, which is what painting a temperament
   actually needs -- and the templates row paints proven tunings,
   rotatable to any root. The pickup bench's templates paint
   manufacturing tolerance: deterministic per-pickup scatter at
   three severities, from a well-kept instrument to one that has
   been on the road for a decade. Everything a template paints
   lands in the lanes, visible and editable, never hidden state.

   Paint across a lane to draw, shift narrows the stroke for fine
   work, double-click a bar resets it. The engine applies edits
   through the same bounded priority rebuild the knobs use --
   editing while a chord rings is fine.
   ============================================================ */

const WS_N = 88, WS_LO = 21;
const WS_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

function wsNoteName(i) {
  const n = WS_LO + i;
  return WS_NAMES[n % 12] + (Math.floor(n / 12) - 1);
}

/* lenScale <-> cents. A cantilever's pitch follows 1/L^2, a string's 1/L,
   so the same trim is twice the cents on a tine that it is on a string. */
const wsScaleToCents = (s, strings) => (strings ? -1200 : -2400) * Math.log2(s);
const wsCentsToScale = (c, strings) => Math.pow(2, -c / (strings ? 1200 : 2400));

/* Deterministic per-note scatter in [-1, 1]; `ch` decorrelates channels. */
function wsHash(i, ch) {
  const h = (((i + 131 * ch) * 2654435761) >>> 0) & 65535;
  return h / 32767.5 - 1;
}

/* ---- proven tunings, cents offsets from equal temperament at root C ---- */
const WS_TUNINGS = {
  JUST:         [0, 11.7, 3.9, 15.6, -13.7, -2.0, -9.8, 2.0, 13.7, -15.6, -3.9, -11.7],
  PYTHAGOREAN:  [0, 13.7, 3.9, -5.9, 7.8, -2.0, 11.7, 2.0, 15.6, 5.9, -3.9, 9.8],
  MEANTONE:     [0, -24.0, -6.8, 10.3, -13.7, 3.4, -20.5, -3.4, -27.4, -10.3, 6.8, -17.1],
  WERCKMEISTER: [0, -9.8, -7.8, -5.9, -9.8, -2.0, -11.7, -3.9, -7.8, -11.7, -3.9, -7.8],
  QUARTERBLACK: [0, 50, 0, 50, 0, 0, 50, 0, 50, 0, 50, 0],
  SLENDRO:      [0, -100, 40, -60, 80, -20, -120, 20, -80, 60, -40, 100],
};

/* Pelog: keys snapped to the nearest degree of a measured seven-tone
   octave; neighbouring keys share a degree, as a keyboard mapped to a
   gamelan does. */
const WS_PELOG = [0, 137, 446, 575, 687, 820, 1098, 1200];
function wsPelogCents(pc) {
  const et = pc * 100;
  let best = 0, bd = 1e9;
  for (const d of WS_PELOG) {
    if (Math.abs(d - et) < bd) { bd = Math.abs(d - et); best = d - et; }
  }
  return best;
}

/* Per-NOTE length templates. */
function wsStretchCents(i) {
  // Railsback-like octave stretch: flat at middle C, bass eased down and
  // treble up, as tuners actually lay a piano.
  const d = (WS_LO + i - 60) / 48;
  return 32 * d * d * d;
}
const wsScatterCents = (i) => wsHash(i, 0) * 8;

/* Gauge templates. */
function wsGaugeGong(i) {
  const reg = i / 87;
  return 1 + 0.45 * Math.exp(-Math.pow((reg - 0.45) / 0.3, 2));
}
function wsGaugeThin(i) {
  const reg = i / 87;
  return 1 - 0.25 * Math.max(0, (reg - 0.3) / 0.7);
}

/* ---- one paintable lane of 88 bars; get/set speak in lane units [-1,1] ---- */
function WsLane({ title, meta, height, get, set, resetOne, format }) {
  const cvsRef = useRef(null);
  const W = 968, H = height || 96, PAD = 6;
  const bw = (W - 2 * PAD) / WS_N;

  const draw = () => {
    const cv = cvsRef.current;
    if (!cv) return;
    const ctx = cv.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    if (cv.width !== W * dpr || cv.height !== H * dpr) { cv.width = W * dpr; cv.height = H * dpr; }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);
    const mid = H / 2;
    ctx.fillStyle = '#17150f';
    ctx.fillRect(PAD, mid - 0.5, W - 2 * PAD, 1);
    for (let i = 0; i < WS_N; i++) {
      const n = WS_LO + i;
      const v = get(i);
      const x = PAD + i * bw;
      const black = [1, 3, 6, 8, 10].includes(n % 12);
      ctx.fillStyle = black ? '#121109' : '#1a1810';
      ctx.fillRect(x + 0.5, 4, bw - 1, H - 8);
      if (Math.abs(v) > 0.002) {
        // A brighter bar means the value sits beyond this zoom.
        const clipped = Math.abs(v) > 1;
        const h = Math.min(1, Math.abs(v)) * (mid - 6);
        ctx.fillStyle = clipped ? '#e6cf8e' : '#caa45e';
        ctx.fillRect(x + 1, v > 0 ? mid - h : mid, bw - 2, h);
      }
      if (n % 12 === 0) {
        ctx.fillStyle = 'rgba(93,87,71,.9)';
        ctx.font = '7px Space Grotesk';
        ctx.textAlign = 'center';
        ctx.fillText('C' + Math.floor(n / 12 - 1), x + bw / 2, H - 1);
      }
    }
  };
  useEffect(draw);

  const barAt = (e) => {
    const r = cvsRef.current.getBoundingClientRect();
    const x = (e.clientX - r.left) * (W / r.width);
    const y = (e.clientY - r.top) * (H / r.height);
    const i = Math.max(0, Math.min(WS_N - 1, Math.floor((x - PAD) / bw)));
    const v = Math.max(-1, Math.min(1, (H / 2 - y) / (H / 2 - 6)));
    return { i, v };
  };

  const painting = useRef(false);
  const onDown = (e) => {
    e.preventDefault();
    painting.current = true;
    const { i, v } = barAt(e);
    set(i, v, e.shiftKey);
    try { e.target.setPointerCapture(e.pointerId); } catch (_) {}
  };
  const onMove = (e) => {
    if (!painting.current) return;
    const { i, v } = barAt(e);
    set(i, v, e.shiftKey);
  };
  const onUp = () => { painting.current = false; };
  const onDbl = (e) => { const { i } = barAt(e); resetOne(i); };

  const hover = useRef(null);
  const onHover = (e) => {
    const { i } = barAt(e);
    if (hover.current) hover.current.textContent = wsNoteName(i) + ' · ' + format(i);
  };

  return (
    <div className="wslane">
      <div className="wshead">
        <span className="wstitle">{title}</span>
        <span className="wsmeta">{meta}</span>
        <span className="wsread" ref={hover} />
      </div>
      <canvas ref={cvsRef} style={{ width: W, height: H }}
              onPointerDown={onDown} onPointerMove={(e) => { onMove(e); onHover(e); }}
              onPointerUp={onUp} onDoubleClick={onDbl} />
    </div>
  );
}

/* ---- shared modal scaffolding ---- */
function WsModal({ title, onReset, onClose, children }) {
  return (
    <div className="modal-back" onClick={onClose}>
      <div className="modal wsmodal" onClick={(e) => e.stopPropagation()}>
        <div className="mhead">
          <h3>{title}</h3>
          <div className="wsactions">
            <button className="wsreset" onClick={onReset}>RESET ALL</button>
            <button onClick={onClose}>✕</button>
          </div>
        </div>
        <div className="wsbody">{children}</div>
      </div>
    </div>
  );
}

/* ============================================================
   The tine workshop
   ============================================================ */
function TineWorkshop({ onClose, strings, grand }) {
  const [mods, setMods] = useState(null);
  const [range, setRange] = useState(100);      // length lane zoom, in cents
  const [root, setRoot] = useState(0);          // template root, 0 = C
  const evName = grand ? 'grand_mod' : strings ? 'string_mod' : 'tine_mod';

  useEffect(() => {
    let alive = true;
    try {
      Juce.getNativeFunction(grand ? 'getGrandMods' : strings ? 'getStringMods' : 'getTineMods')().then((a) => {
        if (!alive) return;
        const flat = a ? Array.from(a) : [];
        const m = [];
        for (let i = 0; i < WS_N; i++)
          m.push({ len: Number(flat[2 * i]) || 1, dia: Number(flat[2 * i + 1]) || 1 });
        setMods(m);
      });
    } catch (_) {
      setMods(Array.from({ length: WS_N }, () => ({ len: 1, dia: 1 })));
    }
    return () => { alive = false; };
  }, []);

  if (!mods) return null;

  const push = (i, len, dia) => {
    JuceBridge.emitNative(evName, { index: i, len, dia });
    setMods((m) => { const c = m.slice(); c[i] = { len, dia }; return c; });
  };
  const pushAll = (fnLenCents, fnDia) => {
    setMods((m) => {
      const c = m.map((e, i) => ({
        len: fnLenCents ? wsCentsToScale(fnLenCents(i), strings) : e.len,
        dia: fnDia ? fnDia(i) : e.dia,
      }));
      c.forEach((e, i) => JuceBridge.emitNative(evName, { index: i, len: e.len, dia: e.dia }));
      return c;
    });
  };

  const fmtCents = (i) => {
    const c = wsScaleToCents(mods[i].len, strings);
    return (c >= 0 ? '+' : '') + (Math.abs(c) < 20 ? c.toFixed(1) : Math.round(c)) + ' cents';
  };
  const fmtDia = (i) => Math.round(mods[i].dia * 100) + '% gauge';

  /* Shift narrows the stroke to a fifth -- the knobs' fine gesture. */
  const setLen = (i, v, fine) => {
    const target = v * range;
    const cur = wsScaleToCents(mods[i].len, strings);
    push(i, wsCentsToScale(fine ? cur + (target - cur) * 0.2 : target, strings), mods[i].dia);
  };
  const setDia = (i, v, fine) => {
    const target = Math.pow(2, v);
    push(i, mods[i].len, fine ? mods[i].dia + (target - mods[i].dia) * 0.2 : target);
  };

  const pc = (i) => ((((WS_LO + i) % 12) - root) + 12) % 12;
  const lenTemplates = [
    ['EQUAL', () => pushAll(() => 0, null)],
    ['JUST', () => pushAll((i) => WS_TUNINGS.JUST[pc(i)], null)],
    ['PYTHAGOREAN', () => pushAll((i) => WS_TUNINGS.PYTHAGOREAN[pc(i)], null)],
    ['MEANTONE ¼', () => pushAll((i) => WS_TUNINGS.MEANTONE[pc(i)], null)],
    ['WERCKMEISTER', () => pushAll((i) => WS_TUNINGS.WERCKMEISTER[pc(i)], null)],
    ['¼-TONE BLACKS', () => pushAll((i) => WS_TUNINGS.QUARTERBLACK[pc(i)], null)],
    ['SLENDRO', () => pushAll((i) => WS_TUNINGS.SLENDRO[pc(i)], null)],
    ['PELOG', () => pushAll((i) => wsPelogCents(pc(i)), null)],
    ['STRETCH', () => pushAll(wsStretchCents, null)],
    ['SCATTER', () => pushAll(wsScatterCents, null)],
  ];
  const diaTemplates = [
    ['STOCK WIRE', () => pushAll(null, () => 1)],
    [strings ? 'BELL' : 'GONG', () => pushAll(null, wsGaugeGong)],
    [strings ? 'LIGHT WIRE' : 'THIN TOP', () => pushAll(null, wsGaugeThin)],
  ];

  return (
    <WsModal title={strings ? 'String Workshop' : 'Tine Workshop'} onClose={onClose}
             onReset={() => { JuceBridge.emitNative(evName + '_reset'); setMods(Array.from({ length: WS_N }, () => ({ len: 1, dia: 1 }))); }}>
      <div className="wstools">
        <span className="wstoollabel">TEMPLATES</span>
        {lenTemplates.map(([n, fn]) => (
          <button key={n} className="wschip" onClick={fn}>{n}</button>
        ))}
      </div>
      <div className="wstools">
        <span className="wstoollabel">ROOT</span>
        <div className="seg">
          {WS_NAMES.map((n, i) => (
            <button key={n} className={i === root ? 'on' : ''} onClick={() => setRoot(i)}>{n}</button>
          ))}
        </div>
        <span className="wstoollabel">RANGE</span>
        <div className="seg">
          {[50, 100, 400, 1200].map((r) => (
            <button key={r} className={r === range ? 'on' : ''} onClick={() => setRange(r)}>
              ±{r >= 1200 ? '8ve' : r + 'c'}
            </button>
          ))}
        </div>
        <span className="wstoollabel">GAUGE</span>
        {diaTemplates.map(([n, fn]) => (
          <button key={n} className="wschip" onClick={fn}>{n}</button>
        ))}
      </div>
      <WsLane title="LENGTH" height={172}
              meta={'re-cut the steel · pitch follows ' + (strings ? '1/L' : '1/L²') + ' · lane spans ±' + (range >= 1200 ? 'one octave' : range + ' cents') + ' · shift paints fine'}
              get={(i) => wsScaleToCents(mods[i].len, strings) / range}
              set={setLen}
              resetOne={(i) => push(i, 1, mods[i].dia)}
              format={fmtCents} />
      <WsLane title="GAUGE" height={88}
              meta={strings ? 're-tensioned to pitch · inharmonicity follows d² · fat wire rings bell-like'
                            : 'swap the wire · pitch stands, the overtones move'}
              get={(i) => Math.log2(mods[i].dia)}
              set={setDia}
              resetOne={(i) => push(i, mods[i].len, 1)}
              format={fmtDia} />
      <div className="wsnote">
        PAINT ACROSS A LANE · SHIFT FOR FINE · DOUBLE-CLICK RESETS A BAR · TEMPLATES ROTATE TO
        THE CHOSEN ROOT · A BRIGHT BAR TOP SITS BEYOND THIS ZOOM · SAVED WITH THE PROJECT
      </div>
    </WsModal>
  );
}

/* ============================================================
   The pickup workshop
   ============================================================
   Height and gap ride as per-pickup offsets on the panel knobs;
   winding scales that pickup's flux contribution as turns do.
   Lane units: height +/-1 mm, gap +/-1 mm, winding 2^(+/-0.5)
   which is about 70 to 140 percent.
   ============================================================ */
const WSP_HMM = 1.0;                 // lane full-scale, mm of height offset
const WSP_GMM = 1.0;                 // mm of gap offset
const WSP_HNORM = WSP_HMM / 2.0;     // pickupPos: 1 unit = 2 mm
const WSP_GNORM = WSP_GMM / 4.4;     // pickupDist: 1 unit = 4.4 mm

/* Manufacturing tolerance: per-pickup scatter, three severities. The
   numbers are the kind of spread a stack of real pickups measures:
   winding count a few percent, the voicing screws a fraction of a
   millimetre -- and "rough" is an instrument nobody has voiced in years. */
const WSP_TOL = [
  ['WELL KEPT', 0.05, 0.05, 0.03],
  ['WORN',      0.15, 0.15, 0.07],
  ['NEGLECTED', 0.40, 0.35, 0.15],
];

function PickupWorkshop({ onClose }) {
  const [mods, setMods] = useState(null);

  useEffect(() => {
    let alive = true;
    try {
      Juce.getNativeFunction('getPickupMods')().then((a) => {
        if (!alive) return;
        const flat = a ? Array.from(a) : [];
        const m = [];
        for (let i = 0; i < WS_N; i++)
          m.push({ h: Number(flat[3 * i]) || 0, g: Number(flat[3 * i + 1]) || 0,
                   s: Number(flat[3 * i + 2]) || 1 });
        setMods(m);
      });
    } catch (_) {
      setMods(Array.from({ length: WS_N }, () => ({ h: 0, g: 0, s: 1 })));
    }
    return () => { alive = false; };
  }, []);

  if (!mods) return null;

  const push = (i, h, g, s) => {
    JuceBridge.emitNative('pickup_mod', { index: i, h, g, s });
    setMods((m) => { const c = m.slice(); c[i] = { h, g, s }; return c; });
  };
  const pushAll = (fn) => {
    setMods((m) => {
      const c = m.map((e, i) => fn(i, e));
      c.forEach((e, i) => JuceBridge.emitNative('pickup_mod', { index: i, h: e.h, g: e.g, s: e.s }));
      return c;
    });
  };

  const applyTol = ([, hMm, gMm, sPct]) => pushAll((i) => ({
    h: wsHash(i, 1) * hMm / 2.0,
    g: wsHash(i, 2) * gMm / 4.4,
    s: Math.pow(2, wsHash(i, 3) * Math.log2(1 + sPct)),
  }));

  const fine = (cur, target, isFine) => (isFine ? cur + (target - cur) * 0.2 : target);

  return (
    <WsModal title="Pickup Workshop" onClose={onClose}
             onReset={() => { JuceBridge.emitNative('pickup_mod_reset'); setMods(Array.from({ length: WS_N }, () => ({ h: 0, g: 0, s: 1 }))); }}>
      <div className="wstools">
        <span className="wstoollabel">TOLERANCE</span>
        {WSP_TOL.map((t) => (
          <button key={t[0]} className="wschip" onClick={() => applyTol(t)}>{t[0]}</button>
        ))}
        <button className="wschip" onClick={() => pushAll(() => ({ h: 0, g: 0, s: 1 }))}>FACTORY</button>
        <span className="wsmeta" style={{ flex: 1, textAlign: 'right' }}>
          deterministic per pickup · the same instrument every session
        </span>
      </div>
      <WsLane title="HEIGHT" height={104}
              meta="per-pickup voicing screw · offset on the panel knob · ±1 mm"
              get={(i) => mods[i].h / WSP_HNORM}
              set={(i, v, f) => push(i, fine(mods[i].h, v * WSP_HNORM, f), mods[i].g, mods[i].s)}
              resetOne={(i) => push(i, 0, mods[i].g, mods[i].s)}
              format={(i) => (mods[i].h >= 0 ? '+' : '') + (mods[i].h * 2).toFixed(2) + ' mm'} />
      <WsLane title="GAP" height={104}
              meta="per-pickup distance · offset on the panel knob · ±1 mm"
              get={(i) => mods[i].g / WSP_GNORM}
              set={(i, v, f) => push(i, mods[i].h, fine(mods[i].g, v * WSP_GNORM, f), mods[i].s)}
              resetOne={(i) => push(i, mods[i].h, 0, mods[i].s)}
              format={(i) => (mods[i].g >= 0 ? '+' : '') + (mods[i].g * 4.4).toFixed(2) + ' mm'} />
      <WsLane title="WINDING" height={104}
              meta="turns on the coil · scales this pickup's contribution · 70–140%"
              get={(i) => Math.log2(mods[i].s) / 0.5}
              set={(i, v, f) => push(i, mods[i].h, mods[i].g, fine(mods[i].s, Math.pow(2, v * 0.5), f))}
              resetOne={(i) => push(i, mods[i].h, mods[i].g, 1)}
              format={(i) => Math.round(mods[i].s * 100) + '%'} />
      <div className="wsnote">
        TOLERANCE PAINTS ALL THREE LANES WITH PER-PICKUP MANUFACTURING SCATTER · EVERYTHING
        STAYS VISIBLE AND EDITABLE · SAVED WITH THE PROJECT
      </div>
    </WsModal>
  );
}

/* ============================================================
   The cabinet workshop
   ============================================================
   One box, five dimensions -- so a bench of knobs, not lanes.
   Each control is a physical thing and shows its physical value:
   the enclosure sets the resonance alignment, the cone size sets
   where it stops being a piston, the microphone's distance and
   angle are the proximity lift and the beaming loss, and the
   suspension is how far the cone travels before the limit.
   ============================================================ */
const WSC_TEMPLATES = [
  ['SUITCASE',  [0.74, 0.59, 0.5, 0.25, 0.5]],
  ['TWIN 2×12', [0.45, 0.55, 0.4, 0.15, 0.7]],
  ['BASS 1×15', [0.9, 0.05, 0.35, 0.3, 0.6]],
  ['PRACTICE',  [0.05, 0.95, 0.2, 0.1, 0.15]],
  ['FLAT PA',   [1.0, 1.0, 0.8, 0.0, 1.0]],
];

const WSC_KNOBS = [
  ['box',   'BOX',        (v) => 'fc ' + Math.round(140 * Math.pow(60 / 140, v)) + ' Hz'],
  ['cone',  'CONE',       (v) => (2.5 * Math.pow(6000 / 2500, v)).toFixed(1) + ' kHz'],
  ['dist',  'MIC DIST',   (v) => Math.round(2 + 58 * v) + ' cm'],
  ['angle', 'MIC ANGLE',  (v) => Math.round(60 * v) + '\u00b0'],
  ['susp',  'SUSPENSION', (v) => Math.round(v * 100) + '%'],
];

function CabinetWorkshop({ onClose }) {
  const [v, setV] = useState(null);

  useEffect(() => {
    let alive = true;
    try {
      Juce.getNativeFunction('getCabMods')().then((a) => {
        if (!alive) return;
        const flat = a ? Array.from(a).map(Number) : [];
        setV(flat.length === 5 ? flat : [0.74, 0.59, 0.5, 0.25, 0.5]);
      });
    } catch (_) { setV([0.74, 0.59, 0.5, 0.25, 0.5]); }
    return () => { alive = false; };
  }, []);

  if (!v) return null;

  const push = (arr) => {
    setV(arr);
    JuceBridge.emitNative('cab_mod', { box: arr[0], cone: arr[1], dist: arr[2], angle: arr[3], susp: arr[4] });
  };

  return (
    <WsModal title="Cabinet Workshop" onClose={onClose}
             onReset={() => { JuceBridge.emitNative('cab_mod_reset'); setV([0.74, 0.59, 0.5, 0.25, 0.5]); }}>
      <div className="wstools">
        <span className="wstoollabel">CABINETS</span>
        {WSC_TEMPLATES.map(([n, t]) => (
          <button key={n} className="wschip" onClick={() => push(t.slice())}>{n}</button>
        ))}
      </div>
      <div className="wscabknobs">
        {WSC_KNOBS.map(([, label, fmt], k) => (
          <Knob key={label} value={v[k]} size="lg" label={label} format={fmt}
                defaultValue={[0.74, 0.59, 0.5, 0.25, 0.5][k]}
                onChange={(nv) => { const c = v.slice(); c[k] = nv; push(c); }} />
        ))}
      </div>
      <div className="wsnote">
        THE BOX SETS THE RESONANCE, THE CONE SETS THE BREAKUP, THE MICROPHONE SETS PROXIMITY
        AND BEAMING · APPLIES TO THE CABINET BLEND ON THE AMP PANEL · SAVED WITH THE PROJECT
      </div>
    </WsModal>
  );
}


/* ============================================================
   Mic Studio -- the grand's bench.
   Spread scales the measured bass-left ILD line and lowers the
   onset of the interchannel phase, balance walks the whole
   image, distance is the lid's high-band shadow as the pair
   backs off the rim, and the two trims are the mixer's own.
   Defaults are exactly the calibrated pair the suite measured.
   ============================================================ */
const WSM_DEFAULTS = [1.0, 0.0, 0.0, 1.0, 1.0];
const WSM_KNOBS = [
  ['spread', 'SPREAD',   (x) => (0.25 + 1.75 * x).toFixed(2) + 'x'],
  ['bias',   'BALANCE',  (x) => ((x - 0.5) * 10).toFixed(1) + ' dB'],
  ['dist',   'DISTANCE', (x) => (0.3 + 2.2 * x).toFixed(1) + ' m'],
  ['lvlL',   'MIC L',    (x) => (40 * Math.log10(0.25 + 1.75 * x) / 2).toFixed(1) + ' dB'],
  ['lvlR',   'MIC R',    (x) => (40 * Math.log10(0.25 + 1.75 * x) / 2).toFixed(1) + ' dB'],
];
/* Engine units per knob: spread 0.25..2 (norm x), bias -1..1, dist 0..1,
   levels 0.25..2. The knob stores normalised 0..1 and maps on push. */
const wsmToEngine = (v) => ({
  spread: 0.25 + 1.75 * v[0],
  bias:   (v[1] - 0.5) * 2,
  dist:   v[2],
  lvlL:   0.25 + 1.75 * v[3],
  lvlR:   0.25 + 1.75 * v[4],
});
const wsmFromEngine = (e) => [
  (e[0] - 0.25) / 1.75,
  e[1] / 2 + 0.5,
  e[2],
  (e[3] - 0.25) / 1.75,
  (e[4] - 0.25) / 1.75,
];
const WSM_TEMPLATES = [
  ['CALIBRATED', [ (1 - 0.25) / 1.75, 0.5, 0.0, (1 - 0.25) / 1.75, (1 - 0.25) / 1.75 ]],
  ['WIDE PAIR',  [ (1.7 - 0.25) / 1.75, 0.5, 0.05, (1 - 0.25) / 1.75, (1 - 0.25) / 1.75 ]],
  ['CLOSE LID',  [ (0.8 - 0.25) / 1.75, 0.5, 0.0, (1.1 - 0.25) / 1.75, (1.1 - 0.25) / 1.75 ]],
  ['PAST THE RIM', [ (1.2 - 0.25) / 1.75, 0.5, 0.75, (0.9 - 0.25) / 1.75, (0.9 - 0.25) / 1.75 ]],
];

function MicStudio({ onClose }) {
  const [v, setV] = useState(null);

  useEffect(() => {
    let alive = true;
    try {
      Juce.getNativeFunction('getMicMods')().then((a) => {
        if (!alive) return;
        const flat = a ? Array.from(a).map(Number) : [];
        setV(flat.length === 5 ? wsmFromEngine(flat) : WSM_DEFAULTS.slice());
      });
    } catch (_) { setV(WSM_DEFAULTS.slice()); }
    return () => { alive = false; };
  }, []);

  if (!v) return null;

  const push = (arr) => {
    setV(arr);
    JuceBridge.emitNative('mic_mod', wsmToEngine(arr));
  };

  return (
    <WsModal title="Mic Studio" onClose={onClose}
             onReset={() => { JuceBridge.emitNative('mic_mod_reset'); setV(wsmFromEngine([1, 0, 0, 1, 1])); }}>
      <div className="wstools">
        <span className="wstoollabel">PLACEMENTS</span>
        {WSM_TEMPLATES.map(([n, t]) => (
          <button key={n} className="wschip" onClick={() => push(t.slice())}>{n}</button>
        ))}
      </div>
      <div className="wscabknobs">
        {WSM_KNOBS.map(([, label, fmt], k) => (
          <Knob key={label} value={v[k]} size="lg" label={label} format={fmt}
                defaultValue={wsmFromEngine([1, 0, 0, 1, 1])[k]}
                onChange={(nv) => { const c = v.slice(); c[k] = nv; push(c); }} />
        ))}
      </div>
      <div className="wsnote">
        SPREAD WIDENS THE PAIR AND DEEPENS THE BASS-LEFT IMAGE · DISTANCE IS THE
        LID SHADOW, NOT A TONE CONTROL · DEFAULTS ARE THE CALIBRATED PAIR ·
        SAVED WITH THE PROJECT AND YOUR PRESETS
      </div>
    </WsModal>
  );
}


/* ============================================================
   Velocity curve editor.
   REAL mode is a true bypass: at the identity map the engine
   short-circuits and the raw velocity reaches the physics'
   own launch law bit-exact -- no interpolation in the path.
   Bending any point engages the monotone cubic; RESET returns
   to real. Presets are named hand positions, pending the
   measured-curve research for the CONCERT map.
   ============================================================ */
const WSV_IDENT = [0, 0.25, 0.5, 0.75, 1];
const WSV_PRESETS = [
  ['REAL', WSV_IDENT],
  ['LIGHT', [0, 0.38, 0.62, 0.83, 1]],
  ['HEAVY', [0, 0.14, 0.36, 0.66, 1]],
  ['STAGE', [0.06, 0.32, 0.55, 0.78, 1]],
];
function VelocityWorkshop({ onClose }) {
  const [y, setY] = useState(null);
  const cvs = useRef(null);
  const dragIdx = useRef(-1);

  useEffect(() => {
    let alive = true;
    try {
      Juce.getNativeFunction('getVelMap')().then((a) => {
        if (!alive) return;
        const flat = a ? Array.from(a).map(Number) : [];
        setY(flat.length === 5 ? flat : WSV_IDENT.slice());
      });
    } catch (_) { setY(WSV_IDENT.slice()); }
    return () => { alive = false; };
  }, []);

  const isReal = y && y.every((v, i) => Math.abs(v - WSV_IDENT[i]) < 1e-6);

  const push = (arr) => {
    const c = arr.slice();
    for (let i = 1; i < 5; i++) c[i] = Math.max(c[i], c[i - 1]);
    setY(c);
    JuceBridge.emitNative('vel_map', { y0: c[0], y1: c[1], y2: c[2], y3: c[3], y4: c[4] });
  };

  useEffect(() => {
    if (!y || !cvs.current) return;
    const el = cvs.current, ctx = el.getContext('2d');
    const W = el.width, H = el.height, pad = 14;
    ctx.clearRect(0, 0, W, H);
    ctx.strokeStyle = '#2c271d';
    for (let i = 0; i <= 4; i++) {
      const gx = pad + (W - 2 * pad) * i / 4;
      ctx.beginPath(); ctx.moveTo(gx, pad); ctx.lineTo(gx, H - pad); ctx.stroke();
      const gy = pad + (H - 2 * pad) * i / 4;
      ctx.beginPath(); ctx.moveTo(pad, gy); ctx.lineTo(W - pad, gy); ctx.stroke();
    }
    /* identity reference */
    ctx.strokeStyle = '#3a3426'; ctx.setLineDash([3, 4]);
    ctx.beginPath(); ctx.moveTo(pad, H - pad); ctx.lineTo(W - pad, pad); ctx.stroke();
    ctx.setLineDash([]);
    /* the curve: same monotone cubic the engine runs */
    const slope = (i) => {
      const sec = (a) => (y[a + 1] - y[a]) * 4;
      if (i === 0) return sec(0);
      if (i === 4) return sec(3);
      const s0 = sec(i - 1), s1 = sec(i);
      if (s0 <= 0 || s1 <= 0) return 0;
      return 2 * s0 * s1 / (s0 + s1);
    };
    const evalMap = (v) => {
      const x = Math.min(Math.max(v, 0), 1) * 4;
      const seg = Math.min(3, Math.floor(x)), t = x - seg;
      const y0 = y[seg], y1 = y[seg + 1];
      const m0 = slope(seg) / 4, m1 = slope(seg + 1) / 4;
      const t2 = t * t, t3 = t2 * t;
      return (2*t3 - 3*t2 + 1) * y0 + (t3 - 2*t2 + t) * m0 + (-2*t3 + 3*t2) * y1 + (t3 - t2) * m1;
    };
    ctx.strokeStyle = isReal ? '#96907d' : '#caa45e'; ctx.lineWidth = 2;
    ctx.beginPath();
    for (let px = 0; px <= 100; px++) {
      const v = px / 100, u = evalMap(v);
      const cx = pad + (W - 2 * pad) * v, cy = H - pad - (H - 2 * pad) * u;
      px === 0 ? ctx.moveTo(cx, cy) : ctx.lineTo(cx, cy);
    }
    ctx.stroke();
    for (let i = 0; i < 5; i++) {
      const cx = pad + (W - 2 * pad) * i / 4, cy = H - pad - (H - 2 * pad) * y[i];
      ctx.beginPath(); ctx.arc(cx, cy, 5, 0, 6.283);
      ctx.fillStyle = '#caa45e'; ctx.fill();
      ctx.strokeStyle = '#171310'; ctx.stroke();
    }
  }, [y, isReal]);

  if (!y) return null;

  const hitPoint = (e) => {
    const r = cvs.current.getBoundingClientRect();
    const W = cvs.current.width, pad = 14;
    const px = (e.clientX - r.left) * (W / r.width);
    let best = -1, bd = 24;
    for (let i = 0; i < 5; i++) {
      const cx = pad + (W - 2 * pad) * i / 4;
      const d = Math.abs(px - cx);
      if (d < bd) { bd = d; best = i; }
    }
    return best;
  };
  const yFromEvent = (e) => {
    const r = cvs.current.getBoundingClientRect();
    const H = cvs.current.height, pad = 14;
    const py = (e.clientY - r.top) * (H / r.height);
    return Math.min(1, Math.max(0, (H - pad - py) / (H - 2 * pad)));
  };
  const onDown = (e) => {
    dragIdx.current = hitPoint(e);
    if (dragIdx.current >= 0) {
      const c = y.slice(); c[dragIdx.current] = yFromEvent(e); push(c);
      try { e.target.setPointerCapture(e.pointerId); } catch (_) {}
    }
  };
  const onMove = (e) => {
    if (dragIdx.current < 0) return;
    const c = y.slice(); c[dragIdx.current] = yFromEvent(e); push(c);
  };
  const onUp = () => { dragIdx.current = -1; };

  return (
    <WsModal title="Velocity Curve" onClose={onClose}
             onReset={() => { JuceBridge.emitNative('vel_map_reset'); setY(WSV_IDENT.slice()); }}>
      <div className="wstools">
        <span className="wstoollabel">MAPS</span>
        {WSV_PRESETS.map(([n, t]) => (
          <button key={n} className={'wschip' + (n === 'REAL' && isReal ? ' on' : '')}
                  onClick={() => push(t.slice())}>{n}</button>
        ))}
        <span className="wsvmode">{isReal ? 'REAL · CURVE BYPASSED, RAW VELOCITY TO THE PHYSICS' : 'CURVED · MONOTONE MAP ENGAGED'}</span>
      </div>
      <canvas ref={cvs} width={560} height={240} className="wsvcanvas"
              onPointerDown={onDown} onPointerMove={onMove} onPointerUp={onUp} />
      <div className="wsnote">
        THE DASHED LINE IS REAL · DRAG A POINT TO BEND · THE MAP FEEDS THE
        INSTRUMENT'S OWN LAUNCH LAW AND CANNOT INVERT DYNAMICS · SAVED WITH
        THE PROJECT AND YOUR PRESETS
      </div>
    </WsModal>
  );
}

Object.assign(window, { TineWorkshop, PickupWorkshop, CabinetWorkshop, MicStudio, VelocityWorkshop });
