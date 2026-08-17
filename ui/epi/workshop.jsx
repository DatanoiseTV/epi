/* ============================================================
   Epi · the tine workshop
   ============================================================
   Eighty-eight tines, two numbers each: how long the steel is
   cut, and what gauge of wire it is. Length retunes as 1/L^2 --
   the beam equation's own law, so the lane is drawn in the cents
   that result. Gauge holds the nominal pitch (a regauged tine is
   re-cut for its note) and instead moves everything downstream
   of the geometry: modal mass, the shear that pulls the high
   overtones flat, how hard the hammer can drive the steel.
   Length is the microtonality lane; gauge is the weird-harmonies
   lane.

   Paint across a lane to draw a curve, double-click a bar to
   reset it, and the engine re-cuts each edited tine through the
   same bounded priority rebuild the knobs use -- editing while a
   chord rings is fine.
   ============================================================ */

const WS_N = 88, WS_LO = 21;
const WS_CENTS_MAX = 1200;                  // length lane: +/- one octave
const WS_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

function wsNoteName(i) {
  const n = WS_LO + i;
  return WS_NAMES[n % 12] + (Math.floor(n / 12) - 1);
}

/* lenScale <-> cents through f = 1/L^2. */
const wsScaleToCents = (s) => -2400 * Math.log2(s);
const wsCentsToScale = (c) => Math.pow(2, -c / 2400);

/* One paintable lane of 88 bars. `value(i)` in [-1, 1] bipolar around 0. */
function WsLane({ title, meta, get, set, resetOne, format }) {
  const cvsRef = useRef(null);
  const W = 968, H = 96, PAD = 6;
  const bw = (W - 2 * PAD) / WS_N;

  const draw = () => {
    const cv = cvsRef.current;
    if (!cv) return;
    const ctx = cv.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    if (cv.width !== W * dpr) { cv.width = W * dpr; cv.height = H * dpr; }
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
      if (Math.abs(v) > 0.004) {
        const h = Math.min(1, Math.abs(v)) * (mid - 6);
        ctx.fillStyle = '#caa45e';
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
    set(i, v);
    try { e.target.setPointerCapture(e.pointerId); } catch (_) {}
  };
  const onMove = (e) => {
    if (!painting.current) return;
    const { i, v } = barAt(e);
    set(i, v);
  };
  const onUp = () => { painting.current = false; };
  const onDbl = (e) => { const { i } = barAt(e); resetOne(i); };

  const hover = useRef(null);
  const onHover = (e) => {
    const { i } = barAt(e);
    if (hover.current) hover.current.textContent = wsNoteName(i) + ' · ' + format(get(i));
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

function TineWorkshop({ onClose }) {
  /* Local mirrors of the 88 x 2 trims, loaded from the plugin once. */
  const [mods, setMods] = useState(null);

  useEffect(() => {
    let alive = true;
    try {
      Juce.getNativeFunction('getTineMods')().then((a) => {
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
    JuceBridge.emitNative('tine_mod', { index: i, len, dia });
    setMods((m) => {
      const c = m.slice();
      c[i] = { len, dia };
      return c;
    });
  };

  const fmtCents = (v) => {
    const c = Math.round(v * WS_CENTS_MAX);
    return (c > 0 ? '+' : '') + c + ' cents';
  };
  const fmtDia = (v) => {
    const g = Math.pow(2, v);
    return Math.round(g * 100) + '% gauge';
  };

  return (
    <div className="modal-back" onClick={onClose}>
      <div className="modal wsmodal" onClick={(e) => e.stopPropagation()}>
        <div className="mhead">
          <h3>Tine Workshop</h3>
          <div className="wsactions">
            <button className="wsreset"
                    onClick={() => { JuceBridge.emitNative('tine_mod_reset'); setMods(Array.from({ length: WS_N }, () => ({ len: 1, dia: 1 }))); }}>
              RESET ALL
            </button>
            <button onClick={onClose}>✕</button>
          </div>
        </div>
        <div className="wsbody">
          <WsLane title="LENGTH" meta="re-cut the steel · pitch follows 1/L² · ± one octave"
                  get={(i) => wsScaleToCents(mods[i].len) / WS_CENTS_MAX}
                  set={(i, v) => push(i, wsCentsToScale(v * WS_CENTS_MAX), mods[i].dia)}
                  resetOne={(i) => push(i, 1, mods[i].dia)}
                  format={fmtCents} />
          <WsLane title="GAUGE" meta="swap the wire · pitch stands, the overtones move"
                  get={(i) => Math.log2(mods[i].dia)}
                  set={(i, v) => push(i, mods[i].len, Math.pow(2, Math.max(-1, Math.min(1, v))))}
                  resetOne={(i) => push(i, mods[i].len, 1)}
                  format={fmtDia} />
          <div className="wsnote">
            PAINT ACROSS A LANE · DOUBLE-CLICK A BAR RESETS IT · SAVED WITH THE PROJECT ·
            WORKSHOP PRESETS PAINT THESE LANES, OTHER PRESETS LEAVE THEM ALONE
          </div>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { TineWorkshop });
