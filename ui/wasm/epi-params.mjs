/*
   Epi — the parameter layout, on the JavaScript side.

   The web build has no JUCE in it, so the layout arrives as data
   (parameters.json, dumped from the running instrument) and the conversion
   between the normalised 0..1 the interface speaks and the raw value the
   engine wants has to happen here.

   That conversion is juce::RangedAudioParameter::convertFrom0to1, which is
   NOT simply the range's own mapping: it snaps afterwards. Reproducing it
   took reading the source rather than guessing, and three details are worth
   writing down because a plausible implementation gets each of them wrong:

     * convertFrom0to1 skews by proportion^(1/skew); convertTo0to1 skews by
       proportion^skew. Opposite directions, and using one for both is a
       mistake that looks correct at the endpoints and nowhere else.

     * A continuous parameter's interval snap is floor(x + 0.5) -- half UP.
       A choice parameter's is juce::roundToInt, which is the magic-number
       trick and therefore half to EVEN. So 2.5 snaps to 3 on a knob and to 2
       on a selector, in the same codebase, and Math.round is wrong for one
       of them.

     * A choice's range runs 0..n-1 with custom mapping functions, so it does
       not go through the skew or interval path at all.

   tools/check-param-map.mjs compares every parameter at a hundred points
   against the real thing, and CI runs it.
*/

// Everything the engine sees is float32, and a reference value that has been
// through a float does not compare equal to a double that has not.
export const f32 = (x) => Math.fround (x);

// juce::roundToInt: adds a magic constant to force the FPU to round, which
// means round-half-to-EVEN. Math.round is half-up and disagrees at every .5.
function roundToInt (v) {
  const floor = Math.floor (v);
  const frac = v - floor;
  if (frac > 0.5) return floor + 1;
  if (frac < 0.5) return floor;
  return (floor % 2 === 0) ? floor : floor + 1;
}

const clamp01 = (v) => (v < 0 ? 0 : (v > 1 ? 1 : v));

export function fromNormalised (p, proportion) {
  proportion = clamp01 (proportion);

  if (p.kind === 'choice') {
    // The custom mapping: jlimit (0, end, v * end), then the custom snap,
    // which is roundToInt.
    const end = p.choices.length - 1;
    const v = f32 (f32 (proportion) * f32 (end));
    return roundToInt (Math.max (0, Math.min (end, v)));
  }

  const { start, end, skew, symmetricSkew, interval } = p;
  let value;

  // Intermediates in double, rounded to float32 once at the end. Matching
  // JUCE bit for bit is NOT the goal and would be a trap: its reference
  // values embed FMA contraction -- clang fuses start + interval * x, so
  // 0.1f * 1000 never rounds to 100 and lands 1.5e-6 above it -- and which
  // operations a compiler contracts differs between platforms. Pinning to
  // one compiler's choices would pass here and fail on the Linux runner.
  // What is checked instead is agreement to a millionth of the parameter's
  // own span, which no knob, display or ear can resolve, and which is still
  // six orders of magnitude tighter than any real mistake.
  if (!symmetricSkew) {
    let q = proportion;
    if (skew !== 1 && q > 0) q = Math.exp (Math.log (q) / skew);
    value = start + (end - start) * q;
  } else {
    let d = 2 * proportion - 1;
    if (skew !== 1 && d !== 0)
      d = Math.exp (Math.log (Math.abs (d)) / skew) * (d < 0 ? -1 : 1);
    value = start + (end - start) / 2 * (1 + d);
  }

  // snapToLegalValue: half UP here, unlike the selector above.
  if (interval > 0)
    value = start + interval * Math.floor ((value - start) / interval + 0.5);

  return (value <= start || end <= start) ? start : (value >= end ? end : value);
}

export function toNormalised (p, v) {
  if (p.kind === 'choice') {
    const end = p.choices.length - 1;
    return end > 0 ? clamp01 (v / end) : 0;
  }

  const { start, end, skew, symmetricSkew } = p;
  const proportion = clamp01 ((v - start) / (end - start));
  if (skew === 1) return proportion;

  if (!symmetricSkew) return Math.pow (proportion, skew);

  const d = 2 * proportion - 1;
  return (1 + Math.pow (Math.abs (d), skew) * (d < 0 ? -1 : 1)) / 2;
}
