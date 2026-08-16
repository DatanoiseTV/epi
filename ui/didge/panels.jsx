/* ============================================================
   Didge · control panels
   Breath · Embouchure · Voice · Instrument · Space
   ============================================================ */

const { useJuceSlider, useJuceChoice, useJuceEvent, emitNative, PARAMS, VOWELS } = JuceBridge;

/* ---- BREATH: how the player pushes air ---- */
function BreathPanel() {
  const [pressure] = useJuceSlider('pressure');
  return (
    <div className="panel">
      <PHead title="Breath" meta={PARAMS.pressure.format(pressure)} />
      <div className="krow">
        <PKnob id="pressure" />
        <PKnob id="attack" />
        <PKnob id="release" />
      </div>
      <div className="krow">
        <PKnob id="vibRate" alt />
        <PKnob id="vibDepth" alt />
        <PKnob id="breathNoise" alt />
      </div>
      {/* Decay is off by default: a didgeridoo sustains. Switched on, the
          breath runs out under a held note, which turns the model into a
          struck exciter rather than a drone. */}
      <div className="prow">
        <PToggle id="decayOn" label="DECAY" />
        <PKnob id="decay" size="sm" />
        <PKnob id="sustain" size="sm" />
      </div>
      <div className="prow vel">
        <PCycle id="velTarget" options={VEL_TARGETS} label="VELOCITY TO" />
        <PKnob id="velAmount" size="sm" />
      </div>
    </div>
  );
}

/* ---- EMBOUCHURE: whatever turns the breath into an oscillation ---- */
function EmbouchurePanel() {
  const [ex] = useJuceChoice('exciter');
  const reed = ex > 0 && ex < 4;
  return (
    <div className="panel">
      <PHead title={reed ? 'Reed' : ex === 4 ? 'Jet' : 'Embouchure'} />
      {/* Lips sound above a bore resonance and a cane reed below it, so this
          is not a colour control -- it changes which instrument this is. */}
      <div className="prow"><PCycle id="exciter" options={EXCITERS} label="EXCITER" /></div>
      <div className="krow">
        <PKnob id="tension" />
        <PKnob id="lipDamp" />
        <PKnob id="embouchure" />
      </div>
      <div className="krow">
        <PKnob id="bendRange" alt bipolar={false} />
        <PKnob id="humanize" alt />
        <div className="knob spacer-knob" />
      </div>
      <div className="bars">
        <LiveBar field="lipOpen" full={0.004} label="Aperture" unit=" mm" scale={1000} digits={2} />
        <LiveBar field="flow" full={0.001} label="Flow" unit=" L/s" scale={1000} digits={2} />
      </div>
    </div>
  );
}

/* ---- VOICE: vocal tract coupled into the bore ---- */
function VoicePanel() {
  const [vowel] = useJuceSlider('vowelX');
  const name = VOWELS[Math.max(0, Math.min(4, Math.round(vowel * 4)))];
  return (
    <div className="panel">
      <PHead title="Voice" meta={name.toUpperCase()} />
      <div className="krow">
        <PKnob id="tractMix" />
        <PKnob id="vowelX" />
        <PKnob id="vowelY" />
      </div>
      <div className="krow">
        <PKnob id="growl" alt />
        <PKnob id="growlPitch" alt />
        <div className="vowelstrip">
          {VOWELS.map((v, i) => (
            <span key={v} className={i === Math.round(vowel * 4) ? 'on' : ''}>{v}</span>
          ))}
        </div>
      </div>
    </div>
  );
}

/* ---- INSTRUMENT: the piece of wood ---- */
function InstrumentPanel() {
  return (
    <div className="panel">
      <PHead title="Instrument" />
      <div className="krow">
        <PKnob id="tune" />
        <PKnob id="bell" />
        <PKnob id="flare" />
      </div>
      <div className="krow">
        <PKnob id="texture" alt />
        <PKnob id="wallDamp" alt />
        <PKnob id="boreDia" alt />
      </div>
      {/* The profile decides the resonance series, so it changes the
          instrument far more than any knob here: a cylinder gives odd
          harmonics only, a cone the complete series. */}
      <div className="prow"><PCycle id="boreProfile" options={BORE_PROFILES} label="BORE" /></div>
      <div className="prow"><PCycle id="material" options={MATERIALS} label="MATERIAL" /></div>
    </div>
  );
}

/* ---- SPACE + output ---- */
function SpacePanel() {
  return (
    <div className="panel">
      <PHead title="Space" />
      <div className="krow">
        <PKnob id="spaceMix" />
        <PKnob id="spaceSize" />
      </div>
      <div className="outrow">
        <PKnob id="outGain" size="lg" />
        <div className="meters">
          <LiveMeter channel={0} label="L" />
          <LiveMeter channel={1} label="R" />
        </div>
      </div>
    </div>
  );
}

/* ---- Preset browser modal ---- */
function PresetBrowser({ onClose, currentName }) {
  const [factory, setFactory] = useState([]);
  const [user, setUser] = useState([]);
  const [saveName, setSaveName] = useState('');

  useEffect(() => {
    let alive = true;
    try {
      Juce.getNativeFunction('listFactoryPresets')().then((a) => { if (alive && a) setFactory(Array.from(a)); });
      Juce.getNativeFunction('listUserPresets')().then((a) => { if (alive && a) setUser(Array.from(a)); });
    } catch (_) {}
    return () => { alive = false; };
  }, []);

  const load = (name) => { emitNative('preset_load', { name }); onClose(); };
  const del = (e, name) => { e.stopPropagation(); emitNative('preset_delete', { name }); setUser((u) => u.filter((n) => n !== name)); };
  const save = () => {
    const n = saveName.trim();
    if (!n) return;
    emitNative('preset_save', { name: n });
    onClose();
  };

  return (
    <div className="modal-back" onClick={onClose}>
      <div className="modal" onClick={(e) => e.stopPropagation()}>
        <div className="mhead">
          <h3>Presets</h3>
          <button onClick={onClose}>✕</button>
        </div>
        <div className="mlist">
          <div className="msec">Factory</div>
          {factory.map((n) => (
            <div key={n} className={'mrow' + (n === currentName ? ' cur' : '')} onClick={() => load(n)}>
              <span>{n}</span><span className="cat">FACTORY</span>
            </div>
          ))}
          {user.length > 0 && <div className="msec">User</div>}
          {user.map((n) => (
            <div key={n} className={'mrow' + (n === currentName ? ' cur' : '')} onClick={() => load(n)}>
              <span>{n}</span>
              <span className="cat">
                USER <button className="mdel" onClick={(e) => del(e, n)} title="Delete preset">✕</button>
              </span>
            </div>
          ))}
        </div>
        <div className="msave">
          <input placeholder="Save as…" value={saveName}
                 onChange={(e) => setSaveName(e.target.value)}
                 onKeyDown={(e) => e.key === 'Enter' && save()} />
          <button onClick={save}>SAVE</button>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { BreathPanel, EmbouchurePanel, VoicePanel, InstrumentPanel, SpacePanel, PresetBrowser });
