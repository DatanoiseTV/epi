// The browser build's presets, checked outside a browser.
//
//   epi-headless --dump-parameters > parameters.json
//   epi-headless --dump-presets    > presets.json
//   node tools/check-preset-store.mjs parameters.json presets.json
//
// Saving is a data-integrity feature: a preset that silently drops the tine
// bench, or a bank import that stores the live sound under every imported
// name, is the kind of bug somebody discovers a week later with their work
// already gone. The shim is written for a browser, so the handful of globals
// it touches are stubbed rather than the code being bent to suit a test.
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const paramsJson  = readFileSync (process.argv[2], 'utf8');
const presetsJson = readFileSync (process.argv[3], 'utf8');

let failures = 0;
const row = (id, what, want, got, ok) => {
  if (!ok) failures++;
  console.log (`  ${id.padEnd (5)} ${what.padEnd (54)} ${String (want).padEnd (16)} ${String (got).padEnd (16)} ${ok ? 'PASS' : 'FAIL'}`);
};

// Read a bench out of a snapshot without trusting it to be there. A suite
// that throws on the first missing field reports one failure and hides the
// rest, which is the opposite of what it is for.
const bench = (state, key, a, b) =>
  (Array.isArray (state[key]) ? state[key].slice (a, b).join (',') : 'missing');

function boot (storage = {}) {
  const posted = [];
  const noop = () => {};
  const elStub = () => ({
    style: {}, classList: { add: noop, remove: noop, toggle: noop, contains: () => false },
    appendChild: noop, remove: noop, addEventListener: noop, setAttribute: noop,
    querySelector: () => null, querySelectorAll: () => [], click: noop,
    set textContent (_) {}, get textContent () { return ''; },
  });
  const sandbox = {
    console,
    localStorage: {
      getItem: (k) => (k in storage ? storage[k] : null),
      setItem: (k, v) => { storage[k] = String (v); },
      removeItem: (k) => { delete storage[k]; },
    },
    document: {
      currentScript: { src: 'http://x/juce-framework-frontend.js' },
      readyState: 'complete',
      getElementById: () => null,
      createElement: elStub,
      body: elStub(),
      head: elStub(),
      addEventListener: noop,
    },
    // The wasm never loads here; nothing this suite checks needs audio.
    fetch: () => Promise.resolve ({ ok: false, status: 0 }),
    WebAssembly: { compile: () => Promise.reject (new Error ('not needed')) },
    setTimeout: (fn, ms) => setTimeout (fn, ms),
    clearTimeout,
    setInterval: () => 0,
    Promise, JSON, Math, Object, Array, String, Number, Boolean, Error,
    __EPI_PARAMS__: JSON.parse (paramsJson),
    __EPI_PRESETS__: JSON.parse (presetsJson),
    __EPI_TEST_POSTED__: posted,
  };
  sandbox.window = sandbox;
  sandbox.global = sandbox;
  sandbox.navigator = {};
  vm.createContext (sandbox);

  // The worklet does not exist, so posts queue. Capturing them is how the
  // suite sees that a bench edit reached the engine and not just the mirror.
  let src = readFileSync ('ui/wasm/wasm-frontend.js', 'utf8');
  src = src.replace ('function post (m) { if (node) node.port.postMessage (m); else pending.push (m); }',
                     'function post (m) { global.__EPI_TEST_POSTED__.push (m); }');
  vm.runInContext (src, sandbox);
  return { H: sandbox.__EPI_HOST__, J: sandbox.Juce, posted, storage };
}

console.log ('Epi browser preset store suite');
console.log (`  ${'id'.padEnd (5)} ${'property'.padEnd (54)} ${'target'.padEnd (16)} ${'measured'.padEnd (16)} verdict\n`);

// 1 -- a fresh visitor gets the first factory preset
{
  const { H } = boot();
  row ('1.1', 'a fresh visit loads the first factory preset', 'Suitcase',
       H.currentPreset(), H.currentPreset() === 'Suitcase');
  row ('1.2', '...and has no saved presets', '0',
       H.userPresetNames().length, H.userPresetNames().length === 0);
}

// 2 -- a snapshot is complete
{
  const { H } = boot();
  const s = H.captureState ('X');
  const nParams = JSON.parse (paramsJson).parameters.length;
  row ('2.1', 'a snapshot holds every parameter', nParams,
       Object.keys (s.values).length, Object.keys (s.values).length === nParams);

  // Read defensively. A snapshot that has DROPPED a bench should be reported
  // as a failure, not throw and take the rest of the suite with it.
  const want = { tineMods: 176, stringMods: 176, grandMods: 176, pickupMods: 264,
                 cabMods: 5, micMods: 5, micStage: 31, velMap: 5 };
  const lens = Object.keys (want).map ((k) => (Array.isArray (s[k]) ? s[k].length : 'missing'));
  const ok = Object.keys (want).every ((k) => Array.isArray (s[k]) && s[k].length === want[k]);
  row ('2.2', '...and every per-part bench', Object.values (want).join ('/'),
       lens.join ('/'), ok);
}

// 3 -- save, move away, come back
{
  const { H, J } = boot();
  H.setNormalised ('pickupPos', 0.83);
  J.backend.emitEvent ('tine_mod', { index: 5, len: 1.25, dia: 0.8 });
  J.backend.emitEvent ('preset_save', { name: 'Mine' });

  H.loadPreset ('Clav Classic');
  const moved = H.getNormalised ('pickupPos');
  H.loadPreset ('Mine');
  const back = H.getNormalised ('pickupPos');
  const benchAt5 = bench (H.captureState(), 'tineMods', 10, 12);

  row ('3.1', 'saving names a preset', 'Mine', H.userPresetNames().join (','),
       H.userPresetNames().length === 1 && H.userPresetNames()[0] === 'Mine');
  row ('3.2', 'loading elsewhere moves the parameter', 'not 0.83',
       moved.toFixed (3), Math.abs (moved - 0.83) > 1e-6);
  row ('3.3', 'loading it back restores the parameter', '0.830',
       back.toFixed (3), Math.abs (back - 0.83) < 1e-6);
  row ('3.4', '...and the bench edit with it', '1.25,0.8',
       benchAt5, benchAt5 === '1.25,0.8');
}

// 4 -- a preset is a COMPLETE snapshot: a bench it does not name goes back to
//      stock rather than keeping whatever the last preset left behind.
//
//      This needs a preset that OMITS one. Every factory preset carries the
//      full bench set, so loading one of those cannot tell the two behaviours
//      apart -- which the first version of this row did not notice.
{
  const { H, J } = boot();
  J.backend.emitEvent ('tine_mod', { index: 3, len: 1.4, dia: 1.4 });
  const before = bench (H.captureState(), 'tineMods', 6, 8);

  const partial = H.captureState ('Partial');
  delete partial.tineMods;
  const bank = H.userBank();
  bank['Partial'] = partial;
  H.writeBank (bank);

  H.loadPreset ('Partial');
  const after = bench (H.captureState(), 'tineMods', 6, 8);
  row ('4.0', 'the bench edit took effect first', '1.4,1.4',
       before, before === '1.4,1.4');
  row ('4.1', 'a preset that omits a bench resets it to stock', '1,1',
       after, after === '1,1');
}

// 5 -- the session survives a reload, unsaved edits included
{
  const store = {};
  {
    const { H } = boot (store);
    H.loadPreset ('Clav Classic');
    H.setNormalised ('coilFreq', 0.21);
  }
  // The debounce has to land before the tab would have gone away.
  await new Promise ((r) => setTimeout (r, 700));
  {
    const { H } = boot (store);
    row ('5.1', 'a reload restores the preset', 'Clav Classic',
         H.currentPreset(), H.currentPreset() === 'Clav Classic');
    row ('5.2', '...and an edit that was never saved', '0.210',
         H.getNormalised ('coilFreq').toFixed (3),
         Math.abs (H.getNormalised ('coilFreq') - 0.21) < 1e-6);
  }
}

// 6 -- deleting
{
  const { H, J } = boot();
  J.backend.emitEvent ('preset_save', { name: 'Temp' });
  J.backend.emitEvent ('preset_delete', { name: 'Temp' });
  row ('6.1', 'deleting removes it from the bank', '0',
       H.userPresetNames().length, H.userPresetNames().length === 0);
}

// 7 -- an imported bank keeps the FILE's sound, not the live one. Writing it
//      through saveUser would store the current instrument under every name.
{
  const { H } = boot();
  H.setNormalised ('outGain', 0.1);
  const fileState = H.captureState ('FromFile');
  fileState.values.outGain = 0.9;

  H.setNormalised ('outGain', 0.4);          // the live sound is now different
  const bank = H.userBank();
  bank['FromFile'] = fileState;
  H.writeBank (bank);
  H.loadPreset ('FromFile');
  row ('7.1', 'an imported preset keeps the value from the file', '0.900',
       H.getNormalised ('outGain').toFixed (3),
       Math.abs (H.getNormalised ('outGain') - 0.9) < 1e-6);
}

// 8 -- the interface's own list sees them
{
  const { H, J } = boot();
  J.backend.emitEvent ('preset_save', { name: 'Listed' });
  const listed = await J.getNativeFunction ('listUserPresets')();
  row ('8.1', 'listUserPresets reports the bank', 'Listed',
       listed.join (','), listed.length === 1 && listed[0] === 'Listed');
  const factory = await J.getNativeFunction ('listFactoryPresets')();
  row ('8.2', 'the factory bank is still whole',
       JSON.parse (presetsJson).presets.length, factory.length,
       factory.length === JSON.parse (presetsJson).presets.length);
}

console.log (`\n${failures} failure(s)`);
process.exit (failures ? 1 : 0);
