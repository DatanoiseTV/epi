# Building with Epi UI

Epi is an electric-piano plugin. Its interface is a **fixed 1180x820 design
canvas** of near-black lacquer panels with a single gold accent — not a
responsive web app. Build screens at that size and scale the whole canvas to
fit, the way the plugin does; do not reflow panels into a fluid grid.

## Setup, and what the wrapper actually is

`EpiSurface` is **not a theme or i18n provider** — it takes no configuration
and supplies no React context. It paints the page surface (`var(--page)`) and
sets the base font and text colour, which components rely on inheriting: the
panels bring their own background, but the bare controls (meters, lamps,
faders, labels) are drawn in gold and bone directly onto the page and are
invisible without it. Wrap the tree in it once.

Components read live parameter state from a `JuceBridge` global installed by
the bundle, not from context. With no plugin behind the page the bundle
installs a mock and fabricates a plausible signal feed, so everything renders
and animates standalone.

```jsx
const { EpiSurface, PHead, PKnob, Knob } = window.EpiUI;

<EpiSurface>
  <div className="panel">
    <PHead title="Action" meta="hammer and key" />
    <div className="krow">
      <PKnob id="hammerHard" label="HARDNESS" />
      <PKnob id="hammerMass" label="MASS" />
      <Knob value={0.62} label="TRIM" format={(v) => (v * 24 - 12).toFixed(1) + ' dB'} />
    </div>
  </div>
</EpiSurface>
```

## Two kinds of control: bound vs controlled

- **`PKnob`, `PFader`, `PSeg`, `PCycle`, `PRocker`** are bound to a plugin
  parameter by `id`. Label, default, unit formatter and bipolarity all come
  from one parameter table — do not pass your own. Ids are real:
  `hammerHard, hammerMass, escapement, strikeNoise, damperGrip, tune,
  velCurve, resDamp, barCouple, bodyMix, nonlinAmt, pickupPos, pickupDist,
  coilFreq, coilQ, coilSat, preampDrive, bass, treble, tremRate, tremDepth,
  tremStereo, phaserMix, phaserRate, phaserDepth, phaserFb, cabMix, clarity,
  bodySize, wearAmount, spaceMix, spaceSize, outGain` and more. **An id that
  is not in the table throws** (`unknown slider id …`). `PSeg`/`PCycle` also
  take an `options` array and bind to a choice id (`instrument, pickupSel,
  material, bodyMat, damperFelt, keyBed, hammerMat, roomProfile, softMode`).
- **`Knob`** is the plain controlled version: `value` normalised 0..1,
  `onChange`, and a `format` that turns 0..1 into the unit the player reads.
  Use it for anything that is not a plugin parameter.

## Styling idiom: semantic classes + tokens, no utilities

There are **no utility classes** and no style props. One global stylesheet
defines ~67 semantic class names; compose with those and with the 16 tokens,
and never invent a parallel naming system.

| Purpose | Classes |
|---|---|
| Structure | `panel`, `rack`, `phead`, `krow`, `prow`, `mrow`, `hdr`, `footer`, `presetbar` |
| Controls | `knob`, `fader`, `seg`, `cyc`, `cycbody`, `cycarrow`, `cyclabel`, `cycval` |
| Readouts | `hbar`, `hbrow`, `hbtrack`, `hbfill`, `hblabel`, `hbval`, `bars`, `note` |
| Modals | `modal`, `modal-back`, `mhead`, `mlist`, `msec`, `msave`, `mdel` |
| Workshops | `wsmodal`, `wsbody`, `wshead`, `wstitle`, `wsmeta`, `wsread`, `wslane`, `wstools`, `wstoollabel`, `wschip`, `wsopen`, `wsreset`, `wsnote` |
| Visualiser | `vizcard`, `viz-top`, `viz-note`, `viz-hint`, `pedal-lamp` |

Tokens: surfaces `--page --card-a --card-b --panel --well --well2`; rules
`--line --line2 --line3`; accent `--gold --gold-hi`; text `--ink --dim
--faint --ghost --mute`. Gold is the **only** accent — a second hue reads as
a different instrument. Type is Space Grotesk throughout, with Cinzel used
once, for the wordmark.

## Overlays need a positioned ancestor

Every modal and workshop scrim is `position: absolute; inset: 0`, so it fills
its nearest **positioned** ancestor. In the plugin that is the design canvas.
Render `PresetBrowser`, `WsModal` or any `*Workshop` inside a
`position: relative` box of canvas size, or the scrim collapses and the
centred modal hangs off the top of the frame.

## Where the truth is

Read `styles.css` and its imports for the real rules behind every class
above, and `components/<group>/<Name>/<Name>.prompt.md` plus `<Name>.d.ts`
for a component's own API before composing with it.
