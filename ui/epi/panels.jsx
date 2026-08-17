/* ============================================================
   Epi · control panels
   Action · Tine/Strings · Pickup/Bridge · Amp · Effects
   ============================================================ */

const { useJuceSlider, useJuceChoice, useJuceEvent, emitNative } = JuceBridge;

/* ---- ACTION: the key, the hammer and the damper ---- */
function ActionPanel() {
  return (
    <div className="panel f-action">
      <PHead title="Action" />
      <div className="krow">
        <PKnob id="hammerHard" label="HARDNESS" />
        <PKnob id="hammerMass" label="MASS" />
        <PKnob id="velCurve" label="VEL CURVE" />
        <PKnob id="escapement" label="LET-OFF" />
        <PKnob id="damperGrip" label="DAMPER" />
        <PKnob id="strikeNoise" label="KEY NOISE" />
      </div>
      {/* Contact duration is the whole attack. A neoprene tip stays on a bass
          tine for about four milliseconds, which lowpasses the strike hard
          enough that the tine's own overtones never get excited. */}
      <div className="note">hammer tip · escapement · felt</div>
    </div>
  );
}

/* ---- TINE / STRINGS: the resonator, per instrument ----
   The knobs shown are the ones this instrument's physics can read. The
   CP-70 has no tone bar, no bloom and no magnetics -- showing those
   knobs would be showing dead controls. */
function TinePanel({ inst }) {
  const cp70 = inst === 1, wurli = inst === 2;
  const [tune] = useJuceSlider('tune');
  const [shop, setShop] = useState(false);
  const a4 = (440 * Math.pow(2, JuceBridge.PARAMS.tune.map.to(tune) / 1200)).toFixed(1) + ' Hz';
  if (wurli) return (
    <div className="panel f-tine">
      <PHead title="Reed" meta={a4} />
      <div className="krow">
        <PKnob id="tipMass" label="TONGUE" />
        <PKnob id="resDamp" label="CLAMP" />
        <PKnob id="tune" label="TUNE" />
      </div>
      {/* A solder-tuned steel tongue on a knife-edge clamp: the tongue knob
          moves its thickness with the mass re-solved, the clamp knob files
          the knife edge, and tuning is the tech's solder move in reverse. */}
      <div className="note">tongue thickness · knife-edge loss · solder-tuned</div>
    </div>
  );
  if (cp70) return (
    <div className="panel f-tine">
      <div className="phead">
        <h2>Strings</h2>
        <span className="hrule" />
        <button className="wsopen" onClick={() => setShop(true)} title="Per-course length and gauge">WORKSHOP</button>
        <span className="hmeta">{a4}</span>
      </div>
      <div className="krow">
        <PKnob id="tipMass" label="UNISON" />
        <PKnob id="resDamp" label="DECAY" />
        <PKnob id="tune" label="TUNE" />
      </div>
      {/* One or two strings per note on a rigid bridge; the unison pair is
          deliberately uncoupled, because the measurements forbid coupling. */}
      <div className="note">unison spread · decay trim · stretch-tuned</div>
      {shop && <TineWorkshop strings onClose={() => setShop(false)} />}
    </div>
  );
  return (
    <div className="panel f-tine">
      <div className="phead">
        <h2>Tine</h2>
        <span className="hrule" />
        <button className="wsopen" onClick={() => setShop(true)} title="Per-tine length and gauge">WORKSHOP</button>
        <span className="hmeta">{a4}</span>
      </div>
      <div className="krow">
        <PKnob id="tipMass" label="SPRING" />
        <PKnob id="resDamp" label="DAMPING" />
        <PKnob id="tune" label="TUNE" />
        <PKnob id="barCouple" label="TONE BAR" />
        <PKnob id="barTune" label="BAR TUNE" />
        <PKnob id="nonlinAmt" label="BLOOM" />
        <PKnob id="bodyMix" label="BODY" />
      </div>
      {/* Sliding the tuning spring does not only retune the tine: it sits at a
          different fraction of every mode's shape, so it re-voices the
          overtones on the way. */}
      <div className="note">spring position · tone bar · bloom</div>
      {shop && <TineWorkshop onClose={() => setShop(false)} />}
    </div>
  );
}

/* ---- PICKUP: where the sound is actually made ---- */
function PickupPanel({ inst }) {
  const cp70 = inst === 1, wurli = inst === 2;
  const [pos] = useJuceSlider('pickupPos');
  const [sat] = useJuceSlider('coilSat');
  const [shop, setShop] = useState(false);
  const mm = (JuceBridge.PARAMS.pickupPos.map.to(pos) * 2).toFixed(2) + ' mm';
  if (wurli) return (
    <div className="panel f-pickup">
      <PHead title="Pickup" meta={Math.round(100 + 100 * sat) + ' V'} />
      {/* Electrostatic: the reed is one plate of a capacitor polarised at
          +150 volts, and the 1/(gap - y) asymmetry IS the bark. Centring is
          the manual's own voicing move; the supply rail is a physical drive
          control -- a sagging unit sits near 130, the hotter Series 200
          rail near 170. */}
      <div className="krow">
        <PKnob id="pickupPos" label="CENTRING" />
        <PKnob id="pickupDist" label="GAP" />
        <PKnob id="coilSat" label="SUPPLY" />
      </div>
      <div className="note">electrostatic · the asymmetry is the bark</div>
    </div>
  );
  if (cp70) return (
    <div className="panel f-pickup">
      <PHead title="Bridge" meta="PIEZO" />
      {/* The CP-70's pickup is one piezo element under each bridge, reading
          the string's termination FORCE -- a +6 dB per octave tilt that is a
          law of the transducer, not a tone control. There is nothing to
          voice: no magnet, no gap, no coil, no resonance. What shapes the
          sound instead is the hammer and the mid-scooped preamp. */}
      <div className="krow">
        <PKnob id="strikeNoise" label="STRIKE" />
        <PKnob id="damperGrip" label="DAMPER" />
      </div>
      <div className="note">force sensing · fixed by construction</div>
    </div>
  );
  return (
    <div className="panel f-pickup">
      <div className="phead">
        <h2>Pickup</h2>
        <span className="hrule" />
        <button className="wsopen" onClick={() => setShop(true)} title="Per-pickup voicing and tolerance">WORKSHOP</button>
        <span className="hmeta">{mm}</span>
      </div>
      {/* Height is the voicing screw. On the pole centreline the field is
          symmetric, the tine crosses its peak twice a cycle, and the note
          comes out an octave up with almost no fundamental. Off-centre the
          fundamental returns. It is geometry, not a filter. */}
      <div className="krow">
        <PKnob id="pickupPos" label="HEIGHT" />
        <PKnob id="pickupDist" label="GAP" />
        <PKnob id="coilFreq" label="COIL PEAK" />
        <PKnob id="coilQ" label="COIL Q" />
        <PKnob id="coilSat" label="CORE SAT" />
      </div>
      <div className="note">the voicing screw, in millimetres</div>
      {shop && <PickupWorkshop onClose={() => setShop(false)} />}
    </div>
  );
}

/* ---- AMP: preamp, tone stack, and the panner ---- */
function AmpPanel() {
  const [depth] = useJuceSlider('tremDepth');
  const [shop, setShop] = useState(false);
  return (
    <div className="panel f-amp">
      <div className="phead">
        <h2>Amp</h2>
        <span className="hrule" />
        <button className="wsopen" onClick={() => setShop(true)} title="Cabinet dimensions and microphone">WORKSHOP</button>
        <span className="hmeta">{depth > 0.01 ? 'TREMOLO' : ''}</span>
      </div>
      <div className="krow">
        <PKnob id="preampDrive" label="DRIVE" />
        <PKnob id="bass" label="BASS" />
        <PKnob id="treble" label="TREBLE" />
        <PKnob id="clarity" label="CLARITY" />
        <PKnob id="cabMix" label="CABINET" />
        {/* Called vibrato on the instrument, but nothing modulates pitch: it
            pans, by shining one oscillator through two photocells wired in
            opposition. */}
        <PKnob id="tremRate" label="RATE" />
        <PKnob id="tremDepth" label="TREMOLO" />
        <PKnob id="tremStereo" label="WIDTH" />
      </div>
      <div className="bars">
        <LiveBar field="vibL" full={1} label="Left" digits={2} />
        <LiveBar field="vibR" full={1} label="Right" digits={2} />
      </div>
      {shop && <CabinetWorkshop onClose={() => setShop(false)} />}
    </div>
  );
}

/* ---- EFFECTS: phaser and the room ---- */
function FxPanel() {
  const lv = useJuceEvent('levels', { voices: 0 });
  return (
    <div className="panel f-fx">
      <PHead title="Effects" meta={(lv.voices || 0) + ' voices'} />
      {/* Neither of these is in the instrument. A Rhodes through a phaser is
          one of the sounds it is known for, so it is here -- after the
          speaker, as an effect, not pretending to be part of the physics. */}
      <div className="krow">
        <PKnob id="phaserMix" label="PHASER" />
        <PKnob id="phaserRate" label="PH RATE" />
        <PKnob id="phaserDepth" label="PH DEPTH" />
        <PKnob id="phaserFb" label="PH RES" />
        <PKnob id="spaceMix" label="ROOM" />
        <PKnob id="spaceSize" label="SIZE" />
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

Object.assign(window, { ActionPanel, TinePanel, PickupPanel, AmpPanel, FxPanel, PresetBrowser });
