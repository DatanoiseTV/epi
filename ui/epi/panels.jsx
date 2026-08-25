/* ============================================================
   Epi · control panels
   Action · Tine/Strings · Pickup/Bridge · Amp · Effects
   ============================================================ */

const { useJuceSlider, useJuceChoice, useJuceEvent, emitNative } = JuceBridge;

/* ---- ACTION: the key, the hammer and the damper ---- */
function ActionPanel({ inst }) {
  const [velShop, setVelShop] = useState(false);
  return (
    <div className="panel f-action">
      <div className="phead">
        <h2>Action</h2>
        <span className="hrule" />
        <button className="wsopen" onClick={() => setVelShop(true)} title="Velocity response curve">CURVE</button>
      </div>
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
      {inst !== 4 && (
        <div className="matrow bodyrow stack">
          <PCycle id="damperFelt" options={FELTS} label="FELT" />
          {inst <= 3 && <PCycle id="keyBed" options={KEYBEDS} label="KEYBED" />}
        </div>
      )}
      {inst !== 4 && (
        <div className="matrow bodyrow stack">
          <PCycle id="hammerMat" options={HAMMERS} label="HAMMER" />
          {inst === 3 && <PCycle id="softMode" options={['SHIFT', 'RAIL']} label="SOFT PED" />}
        </div>
      )}
      <div className="note">hammer tip · escapement · felt</div>
      {velShop && <VelocityWorkshop onClose={() => setVelShop(false)} />}
    </div>
  );
}


/* ---- Rocker: an on/off switch bound to a stepped float param ---- */
function PRocker({ id }) {
  const spec = PARAMS[id];
  const [v, set] = JuceBridge.useJuceSlider(id);
  const on = v >= 0.5;
  return (
    <button className={'seg wsrocker' + (on ? ' on' : '')}
            onClick={() => set(on ? 0 : 1)}>{spec.label.toUpperCase()}</button>
  );
}

/* ---- BODY: what the frame, bar or board is made of, and how big ----
   Index 0 is the calibrated stock body; size 0.5 is the stock scale. A
   uniform size scale moves every frame mode by 1/s and the material by
   sqrt(E/rho); the modal mass follows rho s^3. Live retunes keep the
   frame's ring. */
function BodyRow() {
  return (
    <div className="matrow bodyrow">
      <PCycle id="bodyMat" options={BODY_MATERIALS} label="BODY" />
      <Knob2Inline id="bodySize" />
    </div>
  );
}
function Knob2Inline({ id }) {
  return <div className="bodysize"><PKnob id={id} size="sm" label="SIZE" /></div>;
}

/* ---- MATERIAL: what the resonator is made of ----
   Index 0 is the calibrated stock metal. The row also states the one hard
   transducer fact: a non-magnetic metal is invisible to a magnetic pickup,
   and an insulator cannot be an electrostatic plate. The resonator keeps
   vibrating either way -- the silence is physics, so the panel says so
   instead of leaving it to a bug report. */
function MaterialRow({ inst }) {
  const [mi] = useJuceChoice('material', MATERIALS);
  const [tr] = useJuceChoice('pickupSel', PICKUP_SEL);
  const ferro = MAT_FERRO[mi], cond = MAT_COND[mi];
  /* Which selected transducer is deaf to this material, per instrument. */
  let deaf = null;
  const magnetic = inst === 3 ? false : (inst === 0 || inst === 4) ? tr <= 1 : tr === 0;
  const electro  = inst === 3 ? false : inst === 2 ? (tr === 1 || tr === 2) : tr === 2;
  if (magnetic && !ferro) deaf = cond
    ? 'a ' + MATERIALS[mi].toLowerCase() + ' resonator reaches a magnetic pickup only as a faint eddy signal'
    : 'an insulator is silent through a magnetic pickup';
  else if (electro && !cond) deaf = MATERIALS[mi].toLowerCase() + ' cannot be an electrostatic plate';
  return (
    <div className="matrow">
      <PCycle id="material" options={MATERIALS} label="MATERIAL" />
      {deaf && <div className="note warn">{deaf} -- switch the transducer to hear it</div>}
    </div>
  );
}

/* ---- TINE / STRINGS: the resonator, per instrument ----
   The knobs shown are the ones this instrument's physics can read. The
   CP-70 has no tone bar, no bloom and no magnetics -- showing those
   knobs would be showing dead controls. */
function TinePanel({ inst }) {
  const cp70 = inst === 1, wurli = inst === 2, gpiano = inst === 3, clav = inst === 4;
  const [tune] = useJuceSlider('tune');
  const [shop, setShop] = useState(false);
  const a4 = (440 * Math.pow(2, JuceBridge.PARAMS.tune.map.to(tune) / 1200)).toFixed(1) + ' Hz';
  if (clav) return (
    <div className="panel f-tine">
      <PHead title="Strings" meta={a4} />
      <div className="krow">
        <PKnob id="resDamp" label="DECAY" />
        <PKnob id="tune" label="TUNE" />
      </div>
      {/* Sixty strings under tangents: decay trims the intrinsic loss (the
          yarn does the rest at release), and the tangent's rest distance
          lives on the ACTION panel as let-off, which is what it is. */}
      <MaterialRow inst={4} />
      <BodyRow />
      <div className="krow">
        <PKnob id="bodyMix" label="CASE" />
        <PKnob id="wearAmount" label="WEAR" />
      </div>
      {/* The case reaches the pickup by structure-borne sound -- the rail
          rides the resonating box, and a pickup senses relative motion.
          Wear notches the tangent rubbers: catches, clicks, per-key. */}
      <div className="note">tangent-held strings · the case in the pickup · wear notches per key</div>
    </div>
  );
  if (gpiano) return (
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
        <PKnob id="bodyMix" label="BODY" />
      </div>
      {/* Three strings per note on a fitted soundboard. Unison is the
          tuner's hairsbreadth between them; body trims the string-to-board
          coupling about the measured mobility; the pedal opens every
          undamped string to the board. */}
      <MaterialRow inst={3} />
      <BodyRow />
      <div className="note">unison hairsbreadth · board coupling · pedal-open board wash</div>
      {shop && <TineWorkshop strings grand onClose={() => setShop(false)} />}
    </div>
  );
  if (wurli) return (
    <div className="panel f-tine">
      <PHead title="Reed" meta={a4} />
      <div className="krow">
        <PKnob id="tipMass" label="TONGUE" />
        <PKnob id="resDamp" label="CLAMP" />
        <PKnob id="tune" label="TUNE" />
        <PKnob id="bodyMix" label="BODY" />
      </div>
      {/* A solder-tuned steel tongue on a knife-edge clamp: the tongue knob
          moves its thickness with the mass re-solved, the clamp knob files
          the knife edge, and tuning is the tech's solder move in reverse.
          Body couples every reed through the shared bar: hold the pedal and
          undamped neighbours pick up the strike through the casting. */}
      <MaterialRow inst={2} />
      <BodyRow />
      <div className="note">tongue thickness · knife-edge loss · pedal-down bar wash</div>
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
        <PKnob id="bodyMix" label="BODY" />
      </div>
      {/* One or two strings per note on a rigid bridge; the unison pair is
          deliberately uncoupled, because the measurements forbid coupling.
          Body is the harp frame: struck strings shake it, and with the pedal
          down the strings whose partials coincide ring back -- the octave
          above a struck note lands ~13 dB over the background wash. */}
      <MaterialRow inst={1} />
      <BodyRow />
      <div className="note">unison spread · decay trim · pedal-down frame wash</div>
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
      <MaterialRow inst={0} />
      <BodyRow />
      <div className="note">spring position · tone bar · bloom</div>
      {shop && <TineWorkshop onClose={() => setShop(false)} />}
    </div>
  );
}

/* ---- PICKUP: where the sound is actually made ---- */
function PickupPanel({ inst }) {
  const cp70 = inst === 1, wurli = inst === 2, gpiano = inst === 3, clav = inst === 4;
  const [pos] = useJuceSlider('pickupPos');
  const [sat] = useJuceSlider('coilSat');
  const [shop, setShop] = useState(false);
  const mm = (JuceBridge.PARAMS.pickupPos.map.to(pos) * 2).toFixed(2) + ' mm';
  if (clav) return (
    <div className="panel f-pickup">
      <PHead title="Pickups" meta="TWIN BAR" />
      {/* The D6's two bar pickups at their measured distances, resolved by
          the selector matrix: each position is a different comb written
          onto the same string, and out-of-phase keeps only what the taps
          do not share. GAP is the rest distance -- the operating point on
          the flux curve. */}
      <PSeg id="clavSwitch" options={['CENTER', 'BRIDGE', 'BOTH', 'OUT OF PHASE']} wide />
      <div className="krow" style={{ marginTop: 8 }}>
        <PKnob id="pickupDist" label="GAP" />
      </div>
      <div className="note">position combs · the switch is the voicing</div>
    </div>
  );
  if (gpiano) return (
    <div className="panel f-pickup">
      <div className="phead">
        <h2>Mics</h2>
        <span className="hrule" />
        <button className="wsopen" onClick={() => setShop(true)} title="Pair placement, spread and trims">STUDIO</button>
        <span className="hmeta">PAIR</span>
      </div>
      {/* A spaced pair over the board: bass strings left, treble right, the
          board's own interchannel phase in between. Nothing to voice -- no
          magnet, no gap, no supply. The sound is voiced at the hammer and
          the board. */}
      <div className="note">
        spaced pair · bass left, treble right · voiced at the hammer
      </div>
      {shop && <MicStudio onClose={() => setShop(false)} />}
    </div>
  );
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
      {/* The pickup is one piezo element under each bridge, reading the
          string's termination FORCE -- a +6 dB per octave tilt that is a
          law of the transducer, not a tone control. There is nothing to
          voice: no magnet, no gap, no coil, no resonance. This panel
          earlier duplicated the ACTION panel's damper and strike knobs to
          have something to show -- two knobs on one parameter mirror each
          other, which reads as one control moving another. The honest
          panel has no knobs, because the instrument has none here. */}
      <div className="note">
        force sensing · fixed by construction · the sound is voiced at the
        hammer and the preamp
      </div>
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
function AmpPanel({ inst }) {
  const [depth] = useJuceSlider('tremDepth');
  const [shop, setShop] = useState(false);
  if (inst === 4) return (
    <div className="panel f-amp">
      <PHead title="Tone" meta="ROCKERS" />
      {/* The four rockers as their real RC networks; all up falls back to
          Medium, as the circuit does. Drive is the two-transistor preamp's
          input level against its measured THD points. */}
      <div className="wsrockers">
        <PRocker id="clavBrill" />
        <PRocker id="clavTreb" />
        <PRocker id="clavMed" />
        <PRocker id="clavSoft" />
      </div>
      <div className="krow" style={{ marginTop: 8 }}>
        <PKnob id="preampDrive" label="DRIVE" />
        <PKnob id="clarity" label="CLARITY" />
      </div>
      <div className="note">four rockers · measured THD points · DI to the desk</div>
    </div>
  );
  if (inst === 3) return (
    <div className="panel f-amp">
      <PHead title="Air" />
      {/* The grand has no amplifier: the mics feed the desk. Clarity is the
          one tone control on this path; drive, cabinet and tremolo belong to
          the electrics and would be dead knobs here. */}
      <div className="krow">
        <PKnob id="bass" label="BASS" />
        <PKnob id="treble" label="TREBLE" />
        <PKnob id="clarity" label="CLARITY" />
      </div>
      <div className="note">the desk's shelf pair · not part of the instrument</div>
    </div>
  );
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
        <PCycle id="roomProfile" options={ROOMS} label="SPACE" />
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
