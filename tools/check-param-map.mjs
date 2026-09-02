// Compare ui/wasm/epi-params.mjs against juce::NormalisableRange itself.
//
//   epi-headless --dump-parameters  > parameters.json
//   epi-headless --dump-param-sweep > sweep.json
//   node tools/check-param-map.mjs parameters.json sweep.json
//
// The web build converts normalised to raw in JavaScript because it has no
// JUCE in it. This is what says the reimplementation is right, rather than
// looking right.
import { readFileSync } from 'node:fs';
import { fromNormalised, f32 } from '../ui/wasm/epi-params.mjs';

const params = JSON.parse (readFileSync (process.argv[2], 'utf8')).parameters;
const sweep  = JSON.parse (readFileSync (process.argv[3], 'utf8')).sweep;
const byId = new Map (params.map ((p) => [p.id, p]));

let worst = 0, worstAt = '', checked = 0, failed = 0;

for (const s of sweep) {
  const p = byId.get (s.id);
  if (!p) { console.log (`  ${s.id}: in the sweep but not in the layout`); failed++; continue; }
  const span = p.kind === 'choice' ? Math.max (1, p.choices.length - 1) : (p.end - p.start);

  for (let k = 0; k < s.raw.length; k++) {
    const want = s.raw[k];
    const got  = f32 (fromNormalised (p, k / (s.raw.length - 1)));
    checked++;
    // Measured against the parameter's own SPAN, not against the value. A
    // parameter whose range is -100..100 passing through zero would otherwise
    // report a millionth of a cent as a relative error of one, and a real
    // mistake on a 0..1 parameter would look identical to it.
    const err = Math.abs (got - want) / span;
    if (err > worst) { worst = err; worstAt = `${s.id} at ${k}%: want ${want}, got ${got}`; }
    if (err > 1e-6) failed++;
  }
}

console.log (`  checked ${checked} conversions across ${sweep.length} parameters`);
console.log (`  worst error, as a fraction of the parameter's span: ${worst.toExponential (2)}${worstAt ? `  (${worstAt})` : ''}`);
if (failed) { console.error (`  ${failed} conversions disagree with juce::NormalisableRange`); process.exit (1); }
console.log ('  every conversion matches juce::NormalisableRange');
