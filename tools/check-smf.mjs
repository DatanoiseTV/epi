// The browser build's Standard MIDI File reader.
//
//   node tools/check-smf.mjs
//
// Builds real files -- both layouts, running status, a tempo map, a drum
// channel, SMPTE division -- and reads them back. A MIDI file is a format
// somebody else wrote, so the reader is only worth what it can be shown to
// survive, and every one of the cases below is a file that exists in the wild
// and that a plausible reader gets wrong.
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const sandbox = { console };
sandbox.globalThis = sandbox;
vm.createContext (sandbox);
vm.runInContext (readFileSync ('ui/wasm/epi-smf.js', 'utf8'), sandbox);
const SMF = sandbox.__EPI_SMF__;

let failures = 0;
const row = (id, what, want, got, ok) => {
  if (!ok) failures++;
  console.log (`  ${id.padEnd (5)} ${what.padEnd (52)} ${String (want).padEnd (18)} ${String (got).padEnd (18)} ${ok ? 'PASS' : 'FAIL'}`);
};

// ---- a tiny MIDI file writer, so the tests are real files ---------------
const varint = (n) => {
  const out = [n & 0x7f];
  n >>= 7;
  while (n > 0) { out.unshift ((n & 0x7f) | 0x80); n >>= 7; }
  return out;
};
const be16 = (n) => [(n >> 8) & 0xff, n & 0xff];
const be32 = (n) => [(n >> 24) & 0xff, (n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff];
const ascii = (s) => [...s].map ((c) => c.charCodeAt (0));

function chunk (id, bytes) { return [...ascii (id), ...be32 (bytes.length), ...bytes]; }
function file (format, division, tracks) {
  const head = chunk ('MThd', [...be16 (format), ...be16 (tracks.length), ...be16 (division)]);
  const body = tracks.flatMap ((t) => chunk ('MTrk', [...t, 0x00, 0xff, 0x2f, 0x00]));
  return new Uint8Array ([...head, ...body]).buffer;
}
const trackName = (n) => [0x00, 0xff, 0x03, n.length, ...ascii (n)];
const tempo = (dt, us) => [...varint (dt), 0xff, 0x51, 0x03, (us >> 16) & 0xff, (us >> 8) & 0xff, us & 0xff];
const program = (dt, ch, p) => [...varint (dt), 0xc0 | ch, p];
const noteOn = (dt, ch, n, v) => [...varint (dt), 0x90 | ch, n, v];
const noteOff = (dt, ch, n) => [...varint (dt), 0x80 | ch, n, 0];
const cc = (dt, ch, c, v) => [...varint (dt), 0xb0 | ch, c, v];

console.log ('Epi Standard MIDI File suite');
console.log (`  ${'id'.padEnd (5)} ${'property'.padEnd (52)} ${'target'.padEnd (18)} ${'measured'.padEnd (18)} verdict\n`);

// 1 -- format 1: several tracks, one channel each
{
  const piano = [...trackName ('Piano'), ...program (0, 0, 0),
                 ...noteOn (0, 0, 60, 100), ...noteOff (480, 0, 60)];
  const bass  = [...trackName ('Bass'), ...program (0, 1, 33),
                 ...noteOn (0, 1, 40, 100), ...noteOff (480, 1, 40)];
  const drums = [...trackName ('Drums'), ...noteOn (0, 9, 36, 100), ...noteOff (240, 9, 36)];
  const p = SMF.parse (file (1, 480, [piano, bass, drums]));

  row ('1.1', 'format 1 reads its tracks', '3', p.numTracks, p.numTracks === 3);
  row ('1.2', 'and finds three parts', '3', p.parts.length, p.parts.length === 3);

  const chosen = SMF.choosePiano (p);
  row ('1.3', 'the piano part is chosen, alone', '0:0',
       chosen.selected.join (','), chosen.selected.length === 1 && chosen.selected[0] === '0:0');
  row ('1.4', '...confidently', 'yes', chosen.confident ? 'yes' : 'no', chosen.confident);
}

// 2 -- format 0: ONE track, everything on channels
{
  const all = [
    ...trackName ('My Song'),
    ...program (0, 0, 0),        // channel 1: piano
    ...program (0, 1, 33),       // channel 2: bass
    ...noteOn (0, 0, 60, 100), ...noteOn (0, 1, 40, 100),
    ...noteOn (0, 9, 36, 100),   // channel 10: drums
    ...noteOff (480, 0, 60), ...noteOff (0, 1, 40), ...noteOff (0, 9, 36),
  ];
  const p = SMF.parse (file (0, 480, [all]));

  row ('2.1', 'format 0 is one track', '1', p.numTracks, p.numTracks === 1);
  row ('2.2', '...split into a part per channel', '3', p.parts.length, p.parts.length === 3);

  const chosen = SMF.choosePiano (p);
  row ('2.3', 'the piano CHANNEL is chosen, not the track', '0:0',
       chosen.selected.join (','), chosen.selected.length === 1 && chosen.selected[0] === '0:0');

  const drums = SMF.scoreParts (p).find ((s) => s.part.channel === 9);
  row ('2.4', 'channel 10 is never a piano', 'excluded',
       drums && drums.score < 0 ? 'excluded' : 'included', !!drums && drums.score < 0);
}

// 2b -- a format 0 file whose ONE track name mentions a piano.
//
// The name belongs to the track, and in format 0 the track is the whole file,
// so it says nothing about which CHANNEL is the piano. Crediting every channel
// for it would let a bass line inherit the word "Piano" from the file's title
// and be played as one -- and, worse, would report that as a confident choice.
{
  const all = [
    ...trackName ('Piano Sonata'),           // the piece is called this
    ...noteOn (0, 0, 60, 100), ...noteOn (0, 1, 40, 100),
    ...noteOff (480, 0, 60), ...noteOff (0, 1, 40),
  ];
  const p = SMF.parse (file (0, 480, [all]));
  const scored = SMF.scoreParts (p);
  const named = scored.filter ((x) => x.why.includes ('named as a piano'));

  row ('2.5', 'a format 0 track name credits no channel', '0 parts',
       named.length + ' parts', named.length === 0);

  const chosen = SMF.choosePiano (p);
  row ('2.6', '...so the choice is not claimed as confident', 'not confident',
       chosen.confident ? 'confident' : 'not confident', !chosen.confident);
  row ('2.7', '...and both channels play rather than a guess', '2 parts',
       chosen.selected.length + ' parts', chosen.selected.length === 2);
}

// 2c -- the same file as format 1, where the name DOES belong to its track
{
  const piano = [...trackName ('Piano'), ...noteOn (0, 0, 60, 100), ...noteOff (480, 0, 60)];
  const bass  = [...trackName ('Bass'),  ...noteOn (0, 1, 40, 100), ...noteOff (480, 1, 40)];
  const p = SMF.parse (file (1, 480, [piano, bass]));
  const chosen = SMF.choosePiano (p);
  row ('2.8', 'in format 1 the name picks the part', '0:0',
       chosen.selected.join (','), chosen.selected.length === 1 && chosen.selected[0] === '0:0');
}

// 3 -- the tempo map. A single tempo taken from the top of the file gets this
//      wrong, and gets it more wrong the longer the piece runs.
{
  //  120 bpm for one bar (4 quarters), then 60 bpm for one bar.
  const t = [
    ...tempo (0, 500000),                       // 120 bpm: a quarter is 0.5 s
    ...noteOn (0, 0, 60, 100), ...noteOff (1920, 0, 60),   // 4 quarters = 2.0 s
    ...tempo (0, 1000000),                      // 60 bpm: a quarter is 1.0 s
    ...noteOn (0, 0, 62, 100), ...noteOff (1920, 0, 62),   // 4 quarters = 4.0 s
  ];
  const p = SMF.parse (file (0, 480, [t]));
  const ons = p.events.filter ((e) => e.kind === 'on');
  const offs = p.events.filter ((e) => e.kind === 'off');

  row ('3.1', 'the first bar is played at its own tempo', '2.000 s',
       offs[0].time.toFixed (3) + ' s', Math.abs (offs[0].time - 2.0) < 1e-9);
  row ('3.2', 'the second bar follows the tempo change', '6.000 s',
       offs[1].time.toFixed (3) + ' s', Math.abs (offs[1].time - 6.0) < 1e-9);
  row ('3.3', 'a single tempo would have said', '4.000 s', '4.000 s', true);
  row ('3.4', 'the second note starts where the first ended', '2.000 s',
       ons[1].time.toFixed (3) + ' s', Math.abs (ons[1].time - 2.0) < 1e-9);
}

// 4 -- running status, and meta events that must not break it
{
  // One status byte, then three note-ons with no status, then a meta event,
  // then two more running-status notes. A reader that lets the meta event
  // clobber the running status reads the last two as nonsense.
  const t = [
    ...varint (0), 0x90, 60, 100,
    ...varint (0), 62, 100,
    ...varint (0), 64, 100,
    ...trackName ('after'),                    // meta in the middle
    ...varint (0), 65, 100,
    ...varint (0), 67, 100,
    ...noteOff (480, 0, 60),
  ];
  const p = SMF.parse (file (0, 480, [t]));
  const notes = p.events.filter ((e) => e.kind === 'on').map ((e) => e.note);
  row ('4.1', 'running status carries across a meta event', '60,62,64,65,67',
       notes.join (','), notes.join (',') === '60,62,64,65,67');
}

// 5 -- note-on with velocity zero is a release, in a FILE
{
  const t = [...noteOn (0, 0, 60, 100), ...varint (480), 0x90, 60, 0];
  const p = SMF.parse (file (0, 480, [t]));
  const offs = p.events.filter ((e) => e.kind === 'off');
  row ('5.1', 'velocity zero in a file is a note off', '1 note off',
       offs.length + ' note off', offs.length === 1 && offs[0].note === 60);
}

// 6 -- the schedule
{
  const piano = [...trackName ('Piano'), ...program (0, 0, 0),
                 ...noteOn (0, 0, 60, 100), ...cc (0, 0, 64, 127),
                 ...cc (0, 0, 74, 64),               // a controller not ours
                 ...noteOff (480, 0, 60)];
  const bass = [...trackName ('Bass'), ...program (0, 1, 33), ...noteOn (0, 1, 40, 100)];
  const p = SMF.parse (file (1, 480, [piano, bass]));
  const sched = SMF.schedule (p, ['0:0']);

  row ('6.1', 'only the chosen part is scheduled', 'no bass notes',
       sched.some ((e) => e.channel === 1) ? 'bass present' : 'no bass notes',
       !sched.some ((e) => e.channel === 1));
  row ('6.2', 'the sustain pedal is kept', 'CC 64 present',
       sched.some ((e) => e.kind === 'cc' && e.note === 64) ? 'CC 64 present' : 'missing',
       sched.some ((e) => e.kind === 'cc' && e.note === 64));
  row ('6.3', 'a controller aimed at another synth is dropped', 'CC 74 absent',
       sched.some ((e) => e.note === 74 && e.kind === 'cc') ? 'present' : 'CC 74 absent',
       !sched.some ((e) => e.note === 74 && e.kind === 'cc'));

  // At one instant, releases come before strikes: a repeated note must let go
  // of the key before it is struck again, or the strike lands on a held key.
  const t2 = [...noteOn (0, 0, 60, 100), ...noteOff (480, 0, 60), ...noteOn (0, 0, 60, 100)];
  const p2 = SMF.parse (file (0, 480, [t2]));
  const s2 = SMF.schedule (p2, ['0:0']);
  const at = s2.filter ((e) => Math.abs (e.time - 0.5) < 1e-9).map ((e) => e.kind);
  row ('6.4', 'at one instant a release precedes a strike', 'off,on',
       at.join (','), at.join (',') === 'off,on');
}

// 7 -- a file with nothing to go on still plays
{
  const t = [...noteOn (0, 3, 60, 100), ...noteOff (480, 3, 60)];   // no name, no program
  const p = SMF.parse (file (0, 480, [t]));
  const chosen = SMF.choosePiano (p);
  row ('7.1', 'with no evidence, the part is still played', '1 part',
       chosen.selected.length + ' part', chosen.selected.length === 1);
  row ('7.2', '...and says it was not confident', 'not confident',
       chosen.confident ? 'confident' : 'not confident', !chosen.confident);
}

// 8 -- SMPTE division, where a tick is absolute time
{
  // 25 fps, 40 ticks per frame: 1000 ticks per second.
  const division = ((256 - 25) << 8) | 40;
  const t = [...noteOn (0, 0, 60, 100), ...noteOff (1000, 0, 60)];
  const p = SMF.parse (file (0, division, [t]));
  const off = p.events.find ((e) => e.kind === 'off');
  row ('8.1', 'SMPTE division is read as absolute time', '1.000 s',
       off.time.toFixed (3) + ' s', Math.abs (off.time - 1.0) < 1e-9);
}

// 9 -- rubbish in
{
  let threw = false;
  try { SMF.parse (new Uint8Array ([1, 2, 3, 4]).buffer); } catch { threw = true; }
  row ('9.1', 'a file that is not a MIDI file is refused', 'throws',
       threw ? 'throws' : 'accepted', threw);

  // A truncated track must not run off the end or loop.
  const good = new Uint8Array (file (0, 480, [[...noteOn (0, 0, 60, 100), ...noteOff (480, 0, 60)]]));
  let ok = true;
  try { SMF.parse (good.slice (0, good.length - 6).buffer); } catch { ok = false; }
  row ('9.2', 'a truncated file is read as far as it goes', 'no crash',
       ok ? 'no crash' : 'threw', ok);
}

console.log (`\n${failures} failure(s)`);
process.exit (failures ? 1 : 0);
