# design-sync notes — Epi UI

Project: <https://claude.ai/design/p/7bd124df-8544-4a9a-8a6c-df23cb0b0f33>

## The shape of this repo

Epi is a C++ audio plugin, not a JS design system. The interface is six JSX
files in `ui/epi/`, loaded as classic `<script>` tags by `index.html`,
compiled by in-browser Babel, sharing one global scope. There is no
`package.json`, no module system, no exports, no `dist/`.

`.design-sync/adapter/build-adapter.mjs` bridges that to the converter's
package shape. It reads the sources from a pinned git ref, concatenates them
in `index.html`'s load order, transforms the JSX once, and emits an ESM
package (with a hand-maintained `.d.ts`) into `.design-sync/.cache/adapter/`.
**`ui/epi/` is never touched.** `cfg.buildCmd` runs it.

- The load order is load-bearing: `juce-bridge.jsx` must run first so its IIFE
  has installed `window.JuceBridge` before `panels.jsx` destructures it at
  module top level.
- The concatenation is only safe because no two files declare the same
  top-level name. The generator asserts this and fails with the clashing name
  rather than letting the bundle break somewhere unrelated.
- **The shipped component list lives in `COMPONENTS` in that generator**, with
  its props transcribed from each destructured signature. Add a component to
  `ui/epi` and the generator prints a warning naming it; add it to
  `COMPONENTS` (or to `EXCLUDED`, which currently holds `BodyRow` and
  `Knob2Inline` as pure layout glue).

## Gotchas that cost time

- **`app.jsx` ends in a top-level `ReactDOM` mount** (`/* ---- mount ---- */`).
  It must be stripped, and the generator asserts the marker still exists. Left
  in, it runs on import: where there is no `#root` the whole namespace comes
  up undefined (`[BUNDLE_EXPORT] 29/29 not a component`), and where there IS
  one — every preview card uses `id="root"` too — it silently mounts the
  entire interface underneath the card being previewed.
- **The preview card hardcodes `body{background:#fff}`** in a `<style>` after
  the stylesheet links, so no CSS in the closure can override it. Epi's panels
  bring their own background but its bare atoms (meters, lamps, faders,
  labels) are drawn in gold and bone ON the page surface and are invisible on
  white. `cfg.provider` names `EpiSurface`, a scaffold appended by the
  generator (exported from JS, deliberately absent from the `.d.ts` so it is
  never discovered as a component) that paints `var(--page)` back.
- **Modal/overlay components need a positioned stage.** `.modal-back` is
  `position: absolute; inset: 0`, so it fills its nearest positioned ancestor
  — `#plugin`, the 1180x820 design canvas, in the real app. A preview card has
  none, so the scrim collapses to ~49px and a 509px modal centres to
  `top: -230` and hangs off the frame. Every overlay preview wraps its story
  in a `Stage` (`position: relative; width: 1180; height: 820`). Paired with
  `cfg.overrides.<Name> = {cardMode: single, viewport: 1240x840}`.
- **Parameter-bound atoms need real ids.** `PKnob`/`PFader`/`PSeg`/`PCycle`/
  `PRocker` take an id from the `PARAMS` table (mirrors `ParameterIDs.h`); an
  unknown one throws `mock: unknown slider id <id>`. That is exactly what the
  floor card does — it passes the component's own name — so these show as
  floor cards until authored.
- **Prefer parameters whose default is non-zero** in previews. A knob at 0
  draws no gold arc, so a size/variant sweep on e.g. `tremDepth` renders three
  dark dials that differ only in diameter. `hammerHard` (mid-travel) reads.
- The interface runs standalone: with no JUCE backend the bridge installs a
  mock and a kinematic model fabricates the `levels` feed, so components
  render with believable state in a plain browser.

## Findings in the interface itself (not fixed here — `ui/epi` is not ours)

- `TineWorkshop`'s title is `strings ? 'String Workshop' : 'Tine Workshop'`,
  so the `grand` variant renders pixel-identical to the tine one and a player
  opening it for the Grand reads "Tine Workshop". The `grand` flag does switch
  the native event (`grand_mod` / `getGrandMods`). No `Grand` preview cell
  exists for this reason — it would read as a broken variant axis.

## Ancestor-scoped CSS: components that are NOT self-contained

`epi.css` styles some children only under a container class, so those
components render completely unstyled (0x0, invisible) when mounted on their
own — which is exactly what a preview card does.

- **`HeaderMeters` needs a `.hdr` ancestor.** Its rules are `.hdr .meters`,
  `.hdr .mtrack`, `.hdr .mfill` (`ui/epi/epi.css:124`). Its preview wraps each
  cell in `<div className="hdr">`. This — not the signal feed sitting at
  silence — is why it first came up as `[RENDER_BLANK]`; the mock's
  `levels.out` does carry visible signal.
- Every other descendant rule in `epi.css` is scoped to a class the component
  renders itself, so nothing else needs a wrapper. Verified for all of
  `.knob .dial|.klabel|.kval`, `.fader .ffill|.flabel|.ftrack|.fval`,
  `.pedal-lamp .led`, `.phead .hmeta|.hrule`, `.presetbar .*`, `.matrow .note`,
  `.mrow .cat`, `.bodyrow .*`, `.bodysize .knob`, and `.hdr .brand|.mid|.name|
  .right|.tag` (`Header` renders `.hdr` itself).
- **If a future component stops rendering in its card, check this first**:
  `grep -oE '^\.[a-z][a-z0-9-]* +\.[a-z][a-z0-9-]*' ui/epi/epi.css` lists every
  ancestor-scoped pair.

## The shared-mock trap (the subtlest thing in this repo)

Several components have **no prop that selects their state** — they read it
from the bridge (`useJuceChoice` / `useJuceSlider`) or fetch it through
`Juce.getNativeFunction`. The obvious way to get a second cell is to set the
state on the mock in a mount effect. **That works in per-story capture and is
wrong in the card.**

`package-capture.mjs` navigates per story, so each captured cell gets a fresh
page and fresh mock — variants look perfect on the review sheet. But the card
the product actually renders puts **every cell on one page**, and the mock has
exactly one relay per parameter id. The last cell to mount wins and every
sibling silently re-renders into that state. Measured on this build before it
was fixed: all four `MaterialRow` cells showed `BRASS`; `FxPanel`'s "Default"
cell showed `PHASER 65%`, its sibling's setting; `AmpPanel`'s plain `Tine`
cell showed `TREMOLO 55%`. The graded sheets were right and the shipped cards
were lying.

Rules that follow:

- **Prop-driven variation is always safe** (`inst`, and anything passed in).
  Prefer it.
- **A cell that mutates shared mock state cannot sit in a grid with a sibling
  that contradicts it.** Either drop it (`AmpPanel` lost its tremolo cell) or
  put the component in `cardMode: single` so only one story mounts (`FxPanel`,
  `MaterialRow` — the other stories stay reachable at `?story=<Export>`).
- **Verify in grid mode, never only from the review sheet.** Load
  `components/<group>/<Name>/<Name>.html` with no query string and compare the
  cells' text. Do not compare screenshots: the mock's `levels` feed animates
  every 16 ms, so pixel hashes differ even when two cells are in the identical
  state, which reads as a pass.
- `[RENDER_THIN] variants render identically` is the validator's hint for
  this, but it only caught one of the three — treat it as a prompt to check
  all of them, not as the detector.

## Components that render `null` on first paint

`PickupWorkshop`, `CabinetWorkshop` and `VelocityWorkshop` take only
`onClose`; each fetches state via `Juce.getNativeFunction` in an internal
effect, which throws in a preview, so a `catch` installs a hardcoded fallback
and the component returns `null` until then. A one-shot
`querySelector(...).click()` in a wrapper effect therefore finds nothing and
silently no-ops — which produced identical cells. The previews use a
`MutationObserver` that clicks the target chip the moment it mounts. The same
fallback-then-null pattern applies to `TineWorkshop` and `MicStudio`.

`CabinetWorkshop`'s fallback `[0.74, 0.59, 0.5, 0.25, 0.5]` is pixel-identical
to its own SUITCASE template, so its second cell uses BASS 1x15.

## Card geometry

Epi's components are wide — panels are ~1010px, the canvas 1180px — so almost
nothing fits a default grid cell. Every component is pinned in
`cfg.overrides`: `cardMode: column` for the ordinary ones, `single` (with a
viewport) for the full-canvas overlays and for the two shared-mock cards.
`App` needs `1280x900` and `VizCard` `1220x460` or they crop.

## Preview authoring contract

`.design-sync/previews/<Name>.tsx`, one **named export per card cell**; each
export is a component rendered with no props (`export const Default = () =>
<X … />`). Import components from `'epi-ui'` and hooks from `'react'`; JSX is
compiled with the automatic runtime, so no React import is needed.

## Preview content choices worth keeping

- `LiveBar` uses feed fields that are constant or monotonic in the mock
  (`noteHz`, `lastNote`, `strikes`, `pedal`) rather than the strike-decay
  ones (`tip`, `flux`), so every bar shows a steady non-zero fill instead of
  flickering with the envelope.
- `PRocker`'s cells include `clavMed`, the one rocker that defaults ON, so the
  sheet shows both toggle states.
- `PresetBrowser` can only vary by `currentName`: the mock's
  `listUserPresets` always resolves `[]`, so the User section is unreachable
  from a preview without stubbing `Juce.getNativeFunction`, which is not part
  of the documented mock surface.
- `ActionPanel` ships 3 cells, not 5: `inst` 0/1/2 are the same panel. Only
  the Grand (soft pedal) and Clav (no material rows) branches differ.

## The material pass (2026-08-25)

`ui/epi` got a visual pass on branch `feat/ui-material-pass` (knob caps,
panel and case depth, ivory/ebony key rendering, a case-light pass over the
rod field, cabinet pictograms, stacked selectors, the wordmark lockup). Two
things that matter for syncing:

- **A styling change does NOT invalidate grades.** The re-sync driver read
  the anchor and reported all 29 `unchanged`, 0 `pendingGrade`, with
  `upload.bundle` and `upload.styling` true. That is correct and by design:
  grades follow the authored `.tsx` and preview-affecting config, and the
  component APIs and previews did not move -- only their appearance. The
  render check still ran full and clean (0 bad / thin / blank / identical),
  and the contact sheets were eyeballed before uploading.
- **`WsCabIcon` is in `EXCLUDED`** in the adapter. It is a pictogram drawn
  inside the cabinet workshop's own chips, not a design-system component.
  Any new top-level PascalCase name in `ui/epi` gets named by the
  generator's warning and must land in `COMPONENTS` or `EXCLUDED`.

## Previewing the interface while working on it

`ui-serve.py` (session scratchpad, not committed -- recreate if useful)
serves the real `ui/epi` working tree with `ui/vendor` as a fallback, since
`index.html` expects both flattened the way the plugin's resource provider
presents them, and appends a poller that reloads on save. No build step: the
page compiles its own JSX through Babel exactly as the plugin does. That,
plus `python3 tools/ui-check.py`, is the whole loop -- the smoke test drives
every instrument and opens every workshop, and reports JS exceptions.

## Re-sync risks

- **The build is pinned to a git ref** (`EPI_SYNC_REF`, default `HEAD`) rather
  than the working tree, deliberately: previews are graded over a long run and
  commits landing mid-run would otherwise change what was graded. This run
  used `9d40843`. A re-sync from a later HEAD re-verifies whatever moved.
- **The `.d.ts` is hand-maintained** in the generator, transcribed from the
  destructured signatures. It cannot notice a prop that changed type or was
  renamed upstream — the contract silently rots. On any re-sync where
  `ui/epi/*.jsx` changed, re-read the changed components' signatures against
  `COMPONENTS`.
- `EpiSurface` and the per-preview `Stage` both hardcode Epi's canvas metrics
  (`1180x820`) and `var(--page)`. If the design canvas or the palette moves,
  they need updating with it.
- Chromium comes from the installed Google Chrome via
  `DS_CHROMIUM_PATH="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"`;
  no playwright browser download was needed. `playwright` (npm) is installed
  in `.ds-sync/` only.
