/* ============================================================
   Epi · app shell
   ============================================================
   Header (wordmark, preset pill, output and meters), the harp
   visualizer, the five-panel rack, and the footer strip.
   ============================================================ */

function Header({ onOpenBrowser }) {
  const preset = JuceBridge.useJuceEvent('presetInfo', { name: '—', dirty: false });

  return (
    <div className="hdr">
      <div className="brand">
        <div className="name">epi</div>
        <div className="tag">Physical Modeling Piano</div>
      </div>
      <div className="mid">
        <div className="presetbar">
          <button className="pbtn" onClick={() => JuceBridge.emitNative('preset_prev')} title="Previous preset">‹</button>
          <div className="pname" onClick={onOpenBrowser} title="Browse presets">
            {preset.name}{preset.dirty && <span className="dirty"> *</span>}
          </div>
          <button className="pbtn" onClick={() => JuceBridge.emitNative('preset_next')} title="Next preset">›</button>
          <button className="psave" onClick={onOpenBrowser}>SAVE</button>
        </div>
      </div>
      <div className="right">
        <PKnob id="outGain" size="sm" label="OUTPUT" />
        <HeaderMeters />
      </div>
    </div>
  );
}

function App() {
  const { useJuceChoice, useJuceEvent } = JuceBridge;

  /* Which instrument this is decides which controls exist at all. */
  const [instIdx] = useJuceChoice('instrument', INSTRUMENTS);

  const preset = useJuceEvent('presetInfo', { name: '—', dirty: false });
  const [browser, setBrowser] = React.useState(false);

  return (
    <div id="stage">
      <div id="plugin">
        <Header onOpenBrowser={() => setBrowser(true)} />
        <VizCard />
        <div className="rack">
          <ActionPanel />
          <TinePanel inst={instIdx} />
          <PickupPanel inst={instIdx} />
          <AmpPanel inst={instIdx} />
          <FxPanel />
        </div>
        <div className="footer">
          <span>DRAG A KNOB · DOUBLE-CLICK RESETS · HOLD SHIFT FOR FINE</span>
          <span>EPI {window.EPI_VERSION_STR || 'DEV'} · 88-VOICE PHYSICAL MODEL</span>
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
  window.__epiReady = true;
} catch (err) {
  window.__epiMountError = String((err && err.stack) || err);
  throw err;
}
