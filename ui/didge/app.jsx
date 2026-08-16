/* ============================================================
   Didge · app shell
   ============================================================ */

function Header({ onOpenBrowser }) {
  const preset = JuceBridge.useJuceEvent('presetInfo', { name: '—', dirty: false });

  return (
    <div className="hdr">
      <div className="mark"><span className="r1" /><span className="r2" /><span className="core" /></div>
      <div className="wordmark">
        <div className="name">DIDGE</div>
        <div className="tag">Physical Modeling Didgeridoo</div>
      </div>
      <div className="spacer" />
      <div className="presetbar">
        <button className="pbtn" onClick={() => JuceBridge.emitNative('preset_prev')} title="Previous preset">‹</button>
        <div className="pname" onClick={onOpenBrowser} title="Browse presets">
          {preset.name}{preset.dirty && <span className="dirty">*</span>}
        </div>
        <button className="pbtn" onClick={() => JuceBridge.emitNative('preset_next')} title="Next preset">›</button>
        <button className="psave" onClick={onOpenBrowser}>SAVE</button>
      </div>
      <div className="spacer" />
      <div className="version">{window.DIDGE_VERSION_STR || 'dev'}</div>
    </div>
  );
}

function App() {
  const { useJuceSlider, useJuceEvent } = JuceBridge;

  /* The cutaway draws from the engine's bore array, but it needs the two
     shaping parameters as drag anchors, plus texture/voice/damping to skin
     the wall and the tract inset. */
  const [bell, setBell]     = useJuceSlider('bell');
  const [flare, setFlare]   = useJuceSlider('flare');
  const [texture]           = useJuceSlider('texture');
  const [tractMix]          = useJuceSlider('tractMix');
  const [wallDamp]          = useJuceSlider('wallDamp');

  const preset = useJuceEvent('presetInfo', { name: '—', dirty: false });
  const [browser, setBrowser] = React.useState(false);

  return (
    <div id="stage">
      <div id="plugin">
        <Header onOpenBrowser={() => setBrowser(true)} />
        <div className="hero">
          <InstrumentView
            bell={bell} setBell={setBell}
            flare={flare} setFlare={setFlare}
            texture={texture} tractMix={tractMix} wallDamp={wallDamp}
          />
        </div>
        <div className="rack">
          <BreathPanel />
          <EmbouchurePanel />
          <VoicePanel />
          <InstrumentPanel />
          <SpacePanel />
        </div>
        {browser && <PresetBrowser onClose={() => setBrowser(false)} currentName={preset.name} />}
      </div>
    </div>
  );
}

/* ---- mount ---- */
try {
  const root = ReactDOM.createRoot(document.getElementById('root'));
  root.render(<App />);
  window.__didgeReady = true;
} catch (err) {
  window.__didgeMountError = String((err && err.stack) || err);
  throw err;
}
