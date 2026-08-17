/* ============================================================
   Epi · control panels
   Action · Tine · Pickup · Amp · Output
   ============================================================ */

const { useJuceSlider, useJuceChoice, useJuceEvent, emitNative, PARAMS } = JuceBridge;

/* ---- ACTION: the key, the hammer and the damper ---- */
function ActionPanel() {
  return (
    <div className="panel">
      <PHead title="Action" />
      <div className="prow"><PCycle id="instrument" options={INSTRUMENTS} label="INSTRUMENT" /></div>
      <div className="krow">
        <PKnob id="hammerHard" />
        <PKnob id="hammerMass" />
        <PKnob id="velCurve" />
      </div>
      <div className="krow">
        <PKnob id="escapement" alt />
        <PKnob id="damperGrip" alt />
        <PKnob id="strikeNoise" alt />
      </div>
      {/* Contact duration is the whole attack. A neoprene tip stays on a bass
          tine for about four milliseconds, which lowpasses the strike hard
          enough that the tine's own overtones never get excited. */}
      <div className="note">hammer tip · escapement · felt</div>
    </div>
  );
}

/* ---- TINE: the metal, and what it is bolted to ---- */
function TinePanel() {
  return (
    <div className="panel">
      <PHead title="Tine" />
      <div className="krow">
        <PKnob id="tipMass" />
        <PKnob id="resDamp" />
        <PKnob id="tune" />
      </div>
      <div className="krow">
        <PKnob id="barCouple" alt />
        <PKnob id="barTune" alt />
        <PKnob id="nonlinAmt" alt />
      </div>
      {/* Sliding the tuning spring does not only retune the tine: it sits at a
          different fraction of every mode's shape, so it re-voices the
          overtones on the way. */}
      <div className="note">spring position · tone bar · bloom</div>
    </div>
  );
}

/* ---- PICKUP: where the sound is actually made ---- */
function PickupPanel() {
  const [pos] = useJuceSlider('pickupPos');
  const mm = (PARAMS.pickupPos.map.to(pos) * 2).toFixed(2);
  return (
    <div className="panel">
      <PHead title="Pickup" meta={mm + ' mm'} />
      {/* Height is the voicing screw. On the pole centreline the field is
          symmetric, the tine crosses its peak twice a cycle, and the note comes
          out an octave up with almost no fundamental. Off-centre the
          fundamental returns. It is geometry, not a filter. */}
      <div className="krow">
        <PKnob id="pickupPos" size="lg" />
        <PKnob id="pickupDist" />
      </div>
      <div className="krow">
        <PKnob id="coilFreq" alt />
        <PKnob id="coilQ" alt />
        <PKnob id="coilSat" alt />
      </div>
      <div className="note">drag the tine in the field above</div>
    </div>
  );
}

/* ---- AMP: preamp, tone stack, and the panner ---- */
function AmpPanel() {
  const [depth] = useJuceSlider('tremDepth');
  return (
    <div className="panel">
      <PHead title="Amp" meta={depth > 0.01 ? 'VIBRATO' : ''} />
      <div className="krow">
        <PKnob id="preampDrive" />
        <PKnob id="bass" />
        <PKnob id="treble" />
      </div>
      {/* Called vibrato on the instrument, but nothing modulates pitch: it
          pans, by shining one oscillator through two photocells wired in
          opposition. The cells decay far slower than they attack, so the depth
          falls away as the rate rises -- which is why a fast Suitcase vibrato
          is shallow. */}
      <div className="krow">
        <PKnob id="tremRate" alt />
        <PKnob id="tremDepth" alt />
        <PKnob id="tremStereo" alt />
      </div>
      {/* Whether the two photocells are wired in opposition or together is the
          whole difference between the panner a Rhodes calls vibrato and the
          amplitude tremolo everyone else means by the word. */}
      <div className="krow">
        <PKnob id="cabMix" alt />
      </div>
      <div className="bars">
        <LiveBar field="vibL" full={1} label="Left" digits={2} />
        <LiveBar field="vibR" full={1} label="Right" digits={2} />
      </div>
    </div>
  );
}

/* ---- OUTPUT ---- */
function OutputPanel() {
  const lv = useJuceEvent('levels', { voices: 0 });
  return (
    <div className="panel">
      <PHead title="Output" meta={(lv.voices || 0) + ' voices'} />
      {/* Neither of these is in the instrument. A Rhodes through a phaser is
          one of the sounds it is known for, so it is here -- after the speaker,
          as an effect, not pretending to be part of the physics. */}
      <div className="krow">
        <PKnob id="phaserMix" alt />
        <PKnob id="phaserRate" alt />
        <PKnob id="phaserDepth" alt />
        <PKnob id="phaserFb" alt />
      </div>
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

Object.assign(window, { ActionPanel, TinePanel, PickupPanel, AmpPanel, OutputPanel, PresetBrowser });
