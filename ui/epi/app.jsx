/* ============================================================
   Epi · app shell
   ============================================================ */

function Header({ onOpenBrowser }) {
  const preset = JuceBridge.useJuceEvent('presetInfo', { name: '—', dirty: false });

  return (
    <div className="hdr">
      <div className="mark"><span className="tine" /><span className="pole" /></div>
      <div className="wordmark">
        <div className="name">EPI</div>
        <div className="tag">Physically Modeled Electric Pianos</div>
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
      <div className="version">{window.EPI_VERSION_STR || 'dev'}</div>
    </div>
  );
}

function App() {
  const { useJuceSlider, useJuceEvent, useJuceChoice } = JuceBridge;

  /* Which instrument this is decides which controls exist at all. */
  const [instIdx] = useJuceChoice('instrument', INSTRUMENTS);
  const cp70 = instIdx === 1;

  /* The hero needs the pickup geometry as drag anchors: dragging the tine in
     the field IS the pickup-height parameter. */
  const [pickupPos, setPickupPos] = useJuceSlider('pickupPos');
  const [pickupDist] = useJuceSlider('pickupDist');

  const preset = useJuceEvent('presetInfo', { name: '—', dirty: false });
  const [browser, setBrowser] = React.useState(false);

  return (
    <div id="stage">
      <div id="plugin">
        <Header onOpenBrowser={() => setBrowser(true)} />
        <div className="hero">
          <InstrumentView
            pickupPos={JuceBridge.PARAMS.pickupPos.map.to(pickupPos)}
            setPickupPos={setPickupPos}
            pickupDist={pickupDist}
          />
        </div>
        <div className="rack">
          <ActionPanel />
          <TinePanel cp70={cp70} />
          <PickupPanel cp70={cp70} />
          <AmpPanel />
          <OutputPanel />
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
