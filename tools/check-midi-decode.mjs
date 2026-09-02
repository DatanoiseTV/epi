// The browser build's MIDI decoding, checked against the published map.
//
//   epi-headless --dump-parameters > parameters.json
//   node tools/check-midi-decode.mjs parameters.json
//
// ui/wasm/epi-midi.js is a SECOND implementation of the surface
// src/epi/MidiControlSurface.h implements, against the same pinned numbers.
// Two implementations of one published interface drift unless something
// compares them, and the C++ suite cannot see the JavaScript one.
//
// The file is written for a browser, so the globals it expects are stubbed
// here rather than the code being changed to suit a test.
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const params = JSON.parse (readFileSync (process.argv[2], 'utf8')).parameters;

let failures = 0;
const row = (id, what, want, got, ok) => {
  if (!ok) failures++;
  console.log (`  ${id.padEnd (5)} ${what.padEnd (52)} ${String (want).padEnd (18)} ${String (got).padEnd (18)} ${ok ? 'PASS' : 'FAIL'}`);
};
const near = (a, b, tol = 1e-6) => Math.abs (a - b) <= tol;

// ---- a host to receive what the decoder produces -------------------------
function makeHost () {
  const norm = {}, byId = {};
  params.forEach ((p) => { byId[p.id] = p; norm[p.id] = p.default; });
  return {
    params, byId, isWasmBuild: true,
    calls: [],
    setNormalised (id, n) { norm[id] = n; this.calls.push (['param', id, n]); },
    getNormalised (id) { return norm[id]; },
    note (n, v, on) { this.calls.push (['note', n, v, on]); },
    sustain (v) { this.calls.push (['sustain', v]); },
    sostenuto (b) { this.calls.push (['sostenuto', b]); },
    soft (b) { this.calls.push (['soft', b]); },
    allNotesOff () { this.calls.push (['allOff']); },
    expression (v) { this.calls.push (['expression', v]); },
    pitchBend (v) { this.calls.push (['bend', v]); },
  };
}

function loadMidi (host) {
  const store = {};
  const sandbox = {
    navigator: {},
    localStorage: {
      getItem: (k) => (k in store ? store[k] : null),
      setItem: (k, v) => { store[k] = String (v); },
    },
    console,
  };
  sandbox.window = sandbox;
  sandbox.global = sandbox;
  vm.createContext (sandbox);
  vm.runInContext (readFileSync ('ui/wasm/epi-midi.js', 'utf8'), sandbox);
  sandbox.__EPI_MIDI__.init (host);
  return { midi: sandbox.__EPI_MIDI__, send: sandbox.__epiTestMidi };
}

const cc = (n, v, ch = 0) => ({ data: new Uint8Array ([0xb0 | ch, n, v]) });

console.log ('Epi browser MIDI decode suite');
console.log (`  ${'id'.padEnd (5)} ${'property'.padEnd (52)} ${'target'.padEnd (18)} ${'measured'.padEnd (18)} verdict\n`);

// 1 -- a published CC reaches its parameter
{
  const host = makeHost(); const { send } = loadMidi (host);
  const p = params.find ((x) => x.cc === 7);
  send (cc (7, 100));
  const got = host.getNormalised (p.id);
  row ('1.1', `CC 7 reaches ${p.id}`, (100 / 127).toFixed (5), got.toFixed (5), near (got, 100 / 127));
}

// 2 -- the full four-message NRPN write
{
  const host = makeHost(); const { send } = loadMidi (host);
  const p = params[params.length - 1];
  send (cc (99, 0)); send (cc (98, p.nrpn)); send (cc (6, 100)); send (cc (38, 42));
  const want = ((100 << 7) | 42) / 16383;
  const got = host.getNormalised (p.id);
  row ('2.1', `NRPN ${p.nrpn} writes 14 bits to ${p.id}`, want.toFixed (5), got.toFixed (5), near (got, want));
}

// 3 -- a data MSB with no LSB still applies
{
  const host = makeHost(); const { send } = loadMidi (host);
  const p = params[3];
  send (cc (99, 0)); send (cc (98, p.nrpn)); send (cc (6, 64));
  const got = host.getNormalised (p.id);
  row ('3.1', 'MSB alone applies, within one part in 128', '0.5000 +/-0.008',
       got.toFixed (4), Math.abs (got - 0.5) < 0.008);
}

// 4 -- RPN arbitration. The instrument reads RPN 0 for MPE tuning, so data
//      entry after an RPN select is NOT ours.
{
  const host = makeHost(); const { send } = loadMidi (host);
  const p = params[0];
  send (cc (99, 0)); send (cc (98, p.nrpn));
  send (cc (101, 0)); send (cc (100, 0));
  const before = host.calls.length;
  send (cc (6, 48));
  row ('4.1', 'after an RPN select, data entry is not ours', '0 applied',
       `${host.calls.length - before} applied`, host.calls.length === before);
}

// 5 -- ...and comes back when an NRPN is selected again
{
  const host = makeHost(); const { send } = loadMidi (host);
  send (cc (101, 0)); send (cc (100, 0));
  const p = params[1];
  send (cc (99, 0)); send (cc (98, p.nrpn));
  const before = host.calls.length;
  send (cc (6, 48));
  row ('5.1', 'selecting an NRPN takes data entry back', '1 applied',
       `${host.calls.length - before} applied`, host.calls.length === before + 1);
}

// 6 -- an unassigned NRPN is swallowed, not applied
{
  const host = makeHost(); const { send } = loadMidi (host);
  send (cc (99, 0)); send (cc (98, 127));
  const before = host.calls.length;
  send (cc (6, 64));
  row ('6.1', 'an unassigned NRPN applies nothing', '0 applied',
       `${host.calls.length - before} applied`, host.calls.length === before);
}

// 7 -- the pedals, and the sustain read as depth rather than as a switch
{
  const host = makeHost(); const { send } = loadMidi (host);
  send (cc (64, 90)); send (cc (66, 127)); send (cc (67, 127));
  const sus = host.calls.find ((c) => c[0] === 'sustain');
  row ('7.1', 'CC 64 is a position, not a switch', (90 / 127).toFixed (4),
       sus[1].toFixed (4), near (sus[1], 90 / 127));
  row ('7.2', 'CC 66 and 67 reach sostenuto and soft', 'both',
       `${host.calls.some ((c) => c[0] === 'sostenuto') ? 'sostenuto ' : ''}` +
       `${host.calls.some ((c) => c[0] === 'soft') ? 'soft' : ''}`,
       host.calls.some ((c) => c[0] === 'sostenuto') && host.calls.some ((c) => c[0] === 'soft'));
}

// 8 -- notes and the wheel
{
  const host = makeHost(); const { send } = loadMidi (host);
  send ({ data: new Uint8Array ([0x90, 60, 100]) });
  send ({ data: new Uint8Array ([0x90, 60, 0]) });      // running-status note off
  send ({ data: new Uint8Array ([0xe0, 0x00, 0x60]) }); // wheel up
  const on = host.calls.find ((c) => c[0] === 'note' && c[3] === true);
  const off = host.calls.find ((c) => c[0] === 'note' && c[3] === false);
  const bend = host.calls.find ((c) => c[0] === 'bend');
  row ('8.1', 'note on, and velocity 0 counts as note off', 'both', on && off ? 'both' : 'missing', !!(on && off));
  row ('8.2', 'pitch wheel spans two semitones', '+1.0000',
       bend ? bend[1].toFixed (4) : 'none', bend && near (bend[1], (0x3000 - 8192) / 8192 * 2));
}

// 9 -- channel filtering
{
  const host = makeHost(); const { midi, send } = loadMidi (host);
  midi.setChannel (3);
  const before = host.calls.length;
  send (cc (7, 100, 0));         // channel 1 -- filtered out
  const afterWrong = host.calls.length;
  send (cc (7, 100, 2));         // channel 3 -- accepted
  row ('9.1', 'a channel filter rejects other channels', 'ignored, then accepted',
       `${afterWrong - before} then ${host.calls.length - afterWrong}`,
       afterWrong === before && host.calls.length === before + 1);
}

// 10 -- learn, and the collision it creates
{
  const host = makeHost(); const { midi, send } = loadMidi (host);
  const target = params.find ((p) => p.cc >= 0 && p.id !== 'hammerMass');
  const victim = params.find ((p) => p.cc === 21);
  midi.learn (target.id);
  send (cc (21, 64));                       // binds CC 21 to target
  row ('10.1', 'learn binds the next controller', String (21),
       String (midi.effectiveCc (target.id)), midi.effectiveCc (target.id) === 21);
  if (victim && victim.id !== target.id) {
    row ('10.2', 'the parameter it displaced reports itself unbound', 'undefined',
         String (midi.effectiveCc (victim.id)), midi.effectiveCc (victim.id) === undefined);
    row ('10.3', '...and says which parameter took it', target.id,
         String (midi.shadowedBy (victim.id)), midi.shadowedBy (victim.id) === target.id);
  }
  const before = host.calls.length;
  send (cc (21, 127));
  const last = host.calls[host.calls.length - 1];
  row ('10.4', 'and the learned CC now drives it', target.id,
       last[1], host.calls.length > before && last[1] === target.id);

  // A parameter that learns a new CC must RELEASE its published one, or it
  // answers two controllers and the map has quietly grown a duplicate.
  {
    const before = host.calls.length;
    send (cc (target.cc, 20));
    const stillAnswers = host.calls.length > before &&
                         host.calls[host.calls.length - 1][1] === target.id;
    row ('10.5', 'a learned CC releases the published one it replaced',
         `CC ${target.cc} inert`, stillAnswers ? 'still drives it' : 'inert', !stillAnswers);
  }

  midi.clearOverride (target.id);
  row ('10.6', 'clearing the override restores the published map',
       String (victim ? victim.cc : '-'), String (victim ? midi.effectiveCc (victim.id) : '-'),
       !victim || midi.effectiveCc (victim.id) === victim.cc);
}

console.log (`\n${failures} failure(s)`);
process.exit (failures ? 1 : 0);
