/* ============================================================
   Epi UI -> design-sync adapter
   ============================================================
   The interface is not a package. It is six JSX files loaded as
   classic <script> tags (index.html), compiled by in-browser
   Babel, sharing one global scope: no module system, no exports,
   no dist. The design-sync converter wants a built package with
   a `.d.ts` tree.

   This bridges the two WITHOUT touching ui/epi. It reads the
   sources from a pinned git ref, concatenates them in the exact
   order index.html loads them (so the bridge IIFE has installed
   window.JuceBridge before panels.jsx destructures it at module
   top level), transforms the JSX once, and appends an export
   list. Shared global scope becomes shared module scope, which
   is why the concatenation is safe -- the collision check below
   is what proves it stays safe.

   Reading from a git ref rather than the working tree is
   deliberate: previews are graded over a long run, and a commit
   landing mid-run would otherwise change what was graded.
     EPI_SYNC_REF=<sha|HEAD>   (default HEAD)
   ============================================================ */

import { createRequire } from 'node:module';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../..');
const OUT = path.join(REPO, '.design-sync/.cache/adapter');
const REF = process.env.EPI_SYNC_REF || 'HEAD';

const require = createRequire(path.join(REPO, '.ds-sync/package.json'));
const esbuild = require('esbuild');

/* Load order is index.html's, and it is load-bearing. */
const SOURCES = ['juce-bridge', 'knob', 'viz', 'workshop', 'panels', 'app'];

/* Assets the stylesheet reaches by relative url(). */
const ASSETS = ['epi.css', 'cinzel-var.woff2', 'spacegrotesk-var.woff2'];

/* ---- Bootstrap that must not survive into a library ----------------------
   app.jsx ends by mounting <App/> into #root. That is right for the plugin
   window and wrong for a bundle: it runs on import. Where there is no #root
   (the export smoke check) it throws before the exports are assigned, so the
   whole namespace comes up undefined; where there IS one -- every preview
   card uses id="root" too -- it silently mounts the entire interface
   underneath the card being previewed.

   Cut it. The match is asserted below, so if the marker ever moves this
   fails with the reason instead of resurrecting the bug. */
const STRIP = { 'app.jsx': /\n\/\* ---- mount ---- \*\/[\s\S]*$/ };

/* ---- The shipped surface -------------------------------------------------
   Props are transcribed from each component's destructured signature at the
   pinned ref. Types the source actually implies -- `inst` is an instrument
   index 0..4, StageView.onMove is (i, x, z). Anything not listed here is
   deliberately not shipped (see EXCLUDED). */
const COMPONENTS = {
  /* --- atoms (knob.jsx) --- */
  Knob: {
    group: 'Atoms',
    doc: 'The knob every control is built from. `value` is normalised 0..1; `format` turns that into the displayed string. `bipolar` draws the arc out from top-centre instead of from the left. Drag vertically to change (240px covers the full range, shift for fine), double-click to return to `defaultValue`.',
    props: `value?: number;
  onChange?: (value: number) => void;
  size?: 'sm' | 'md' | 'lg';
  /** null hides the label row entirely; undefined renders it empty. */
  label?: string | null;
  format?: (value: number) => string;
  bipolar?: boolean;
  defaultValue?: number;
  showValue?: boolean;`,
  },
  PKnob: {
    group: 'Atoms',
    doc: 'A Knob bound to a plugin parameter. Label, default, formatter and bipolarity all come from the one PARAMS table, so a range change in C++ is mirrored in exactly one place.',
    props: `/** Parameter id from the PARAMS table, e.g. "hammerHard". */
  id: string;
  size?: 'sm' | 'md' | 'lg';
  label?: string;`,
  },
  PFader: {
    group: 'Atoms',
    doc: 'Vertical fader bound to a plugin parameter.',
    props: `id: string;
  label?: string;`,
  },
  PSeg: {
    group: 'Atoms',
    doc: 'Segmented button group bound to a choice parameter. One visible button per option.',
    props: `id: string;
  options: string[];
  wide?: boolean;`,
  },
  PCycle: {
    group: 'Atoms',
    doc: 'Single button that cycles through a choice parameter’s options. Use where a segmented group would not fit.',
    props: `id: string;
  options: string[];
  label?: string;`,
  },
  PRocker: {
    group: 'Atoms',
    doc: 'Two-position rocker switch bound to a parameter.',
    props: `id: string;`,
  },
  PHead: {
    group: 'Atoms',
    doc: 'Panel header: title on the left, a rule, and optional meta text on the right.',
    props: `title: string;
  meta?: string;`,
  },
  HeaderMeters: {
    group: 'Atoms',
    doc: 'Output level meters for the plugin header. Reads the live `levels` feed; takes no props.',
    props: '',
  },
  LiveBar: {
    group: 'Atoms',
    doc: 'Horizontal bar readout of one field from the live engine feed, with a numeric value beside it.',
    props: `/** Field name within the live \`levels\` event payload. */
  field: string;
  full?: number;
  label?: string;
  unit?: string;
  digits?: number;
  scale?: number;`,
  },

  /* --- panels (panels.jsx) --- */
  ActionPanel: {
    group: 'Panels',
    doc: 'Hammer and key action controls, plus the velocity-curve workshop launcher.',
    props: `/** Instrument index, 0..4. */
  inst: number;`,
  },
  MaterialRow: {
    group: 'Panels',
    doc: 'Material selector row for one instrument. Which materials are offered depends on the instrument index.',
    props: `/** Instrument index, 0..4. */
  inst: number;`,
  },
  TinePanel: {
    group: 'Panels',
    doc: 'Resonator panel: tuning, geometry and material for the tine, string, reed or bar, with the tuning workshop launcher. Its contents change per instrument.',
    props: `/** Instrument index, 0..4. */
  inst: number;`,
  },
  PickupPanel: {
    group: 'Panels',
    doc: 'Pickup panel: position, distance and character, with the pickup workshop launcher.',
    props: `/** Instrument index, 0..4. */
  inst: number;`,
  },
  AmpPanel: {
    group: 'Panels',
    doc: 'Amplifier and cabinet panel, with the cabinet workshop launcher.',
    props: `/** Instrument index, 0..4. */
  inst: number;`,
  },
  FxPanel: {
    group: 'Panels',
    doc: 'Effects panel: tremolo, phaser, space. Takes no props — every control is parameter-bound.',
    props: '',
  },
  PresetBrowser: {
    group: 'Panels',
    doc: 'Full-screen preset browser overlay.',
    props: `onClose?: () => void;
  currentName?: string;`,
  },

  /* --- visualisation (viz.jsx) --- */
  VizCard: {
    group: 'Visualisation',
    doc: 'The 88-key keyboard visualiser: playable, shows per-note activity from the live engine feed, and doubles as the note entry surface. Takes no props.',
    props: '',
  },
  PedalLamp: {
    group: 'Visualisation',
    doc: 'Sustain pedal indicator lamp, lit from the live feed. Takes no props.',
    props: '',
  },

  /* --- workshops (workshop.jsx) --- */
  WsModal: {
    group: 'Workshops',
    doc: 'The shell every workshop is built in: titled modal with a reset and a close action. Compose a workshop by putting its lanes and tools in `children`.',
    props: `title: string;
  onReset?: () => void;
  onClose?: () => void;
  children?: React.ReactNode;`,
  },
  WsLane: {
    group: 'Workshops',
    doc: 'One editable per-note lane across the 88-key compass. `get` reads a note’s value, `set` writes it, `resetOne` clears a single note. Drag across it to draw.',
    props: `title: string;
  meta?: string;
  height?: number;
  get: (index: number) => number;
  set: (index: number, value: number) => void;
  resetOne?: (index: number) => void;
  format?: (value: number) => string;`,
  },
  TineWorkshop: {
    group: 'Workshops',
    doc: 'Per-note tuning and geometry editor. Serves the tine, string and grand resonators — `strings` and `grand` pick which, changing both the units and which native event the edits are pushed to.',
    props: `onClose?: () => void;
  strings?: boolean;
  grand?: boolean;`,
  },
  PickupWorkshop: {
    group: 'Workshops',
    doc: 'Per-note pickup height, gap and strength editor.',
    props: `onClose?: () => void;`,
  },
  CabinetWorkshop: {
    group: 'Workshops',
    doc: 'Cabinet geometry editor: box, cone, distance, angle and suspension.',
    props: `onClose?: () => void;`,
  },
  StageView: {
    group: 'Workshops',
    doc: 'Top-down plot of the microphone field, piano at the top and tail toward the mics. Drag a mic to move it. x maps linearly; z on a square root so the close metre has room.',
    props: `/** Flat mic-parameter vector, 1 + 5 * 6 entries. */
  v: number[];
  sel: number;
  onSelect: (index: number) => void;
  onMove: (index: number, x: number, z: number) => void;`,
  },
  MicStage: {
    group: 'Workshops',
    doc: 'StageView plus the per-mic controls: selection chips, on/off, and the selected mic’s parameters.',
    props: `/** Flat mic-parameter vector, 1 + 5 * 6 entries. */
  v: number[];
  push: (v: number[]) => void;`,
  },
  MicStudio: {
    group: 'Workshops',
    doc: 'The full five-microphone studio: stage, per-mic controls and placement templates.',
    props: `onClose?: () => void;`,
  },
  VelocityWorkshop: {
    group: 'Workshops',
    doc: 'Velocity response curve editor, with presets from soft to hard.',
    props: `onClose?: () => void;`,
  },

  /* --- shell (app.jsx) --- */
  Header: {
    group: 'Shell',
    doc: 'Plugin header: preset name with previous/next stepping, and the output meters.',
    props: `onOpenBrowser?: () => void;`,
  },
  App: {
    group: 'Shell',
    doc: 'The whole interface: header, instrument selector, all panels, and the keyboard visualiser, on the fixed 1224x860 design canvas. Takes no props.',
    props: '',
  },
};

/* Defined at top level but deliberately not shipped: pure layout glue with
   no standalone meaning. Recorded so a future run knows the omission was a
   decision, not an oversight. */
const EXCLUDED = {
  BodyRow: 'fixed row of one instrument’s body controls',
  Knob2Inline: 'inline knob layout glue',
  WsCabIcon: 'cabinet pictogram drawn inside the cabinet workshop’s own chips',
};

const show = (spec, enc) =>
  execFileSync('git', ['show', `${REF}:${spec}`], {
    cwd: REPO, maxBuffer: 64 << 20, encoding: enc,
  });

const die = (msg) => { console.error(`✗ adapter: ${msg}`); process.exit(1); };

/* ---- read ---- */
const files = SOURCES.map((n) => {
  const name = `${n}.jsx`;
  let text = show(`ui/epi/${name}`, 'utf8');
  const re = STRIP[name];
  if (re) {
    if (!re.test(text)) die(`${name} at ${REF} no longer contains the block STRIP expects (${re}). It used to end with a top-level ReactDOM mount that must not ship in a library — re-check the file and update STRIP.`);
    text = text.replace(re, '\n');
  }
  return { name, text };
});

/* ---- prove the concatenation is safe ----
   Six files that shared one global scope now share one module scope. In the
   browser a duplicate top-level `const` across two classic scripts would
   already be a redeclaration error, so the sources are collision-free today
   -- but nothing enforces it. If someone adds a clashing name, the bundle
   would fail far away from the cause, so fail here with the real reason. */
const seen = new Map();
for (const f of files) {
  for (const m of f.text.matchAll(/^(?:function|const|let|var)\s+([A-Za-z_$][\w$]*)/gm)) {
    const prev = seen.get(m[1]);
    if (prev && prev !== f.name) die(`top-level name "${m[1]}" is declared in both ${prev} and ${f.name}. The interface loads these into one shared scope, so this is a redeclaration. Rename one.`);
    seen.set(m[1], f.name);
  }
}

/* The preview-surface scaffold is appended below; it must not clash. */
if (seen.has('EpiSurface')) die('ui/epi now defines EpiSurface, which collides with the preview scaffold appended by this script. Rename one.');

/* Every shipped component must actually exist at this ref. */
const missing = Object.keys(COMPONENTS).filter((n) => !seen.has(n));
if (missing.length) die(`declared but not defined at ${REF}: ${missing.join(', ')}. Update COMPONENTS in this file.`);

/* And flag anything new that is neither shipped nor explicitly excluded, so a
   component added upstream does not silently fail to sync. */
const known = new Set([...Object.keys(COMPONENTS), ...Object.keys(EXCLUDED)]);
const unlisted = [...seen.keys()].filter((n) => /^[A-Z]/.test(n) && !known.has(n) && !/^[A-Z0-9_]+$/.test(n));
if (unlisted.length) console.error(`! adapter: PascalCase top-level names not shipped and not excluded: ${unlisted.join(', ')} — add to COMPONENTS or EXCLUDED in ${path.relative(REPO, fileURLToPath(import.meta.url))}`);

/* ---- transform ---- */
const banner = `/* Generated by .design-sync/adapter/build-adapter.mjs from ${REF}. Do not edit. */
import React from 'react';
const { useState, useEffect, useRef, useCallback, useMemo } = React;
/* The sources reach for React on the global (index.html puts it there). */
if (typeof window !== 'undefined' && !window.React) window.React = React;
`;

const body = files.map((f) => `\n/* ==== ${f.name} ==== */\n${f.text}`).join('\n');

/* ---- Preview scaffolding -------------------------------------------------
   Not part of the interface, and deliberately absent from index.d.ts so it
   is never discovered as a component -- it exists only to be named by
   cfg.provider.

   The generated preview card hardcodes `body{background:#fff}` in a <style>
   that comes after the stylesheet links, so nothing in the CSS closure can
   override it. Epi is a near-black instrument: its panels bring their own
   background, but the bare atoms (meters, lamps, faders, labels) are drawn
   in gold and bone ON the page surface, and against white they are simply
   invisible. This paints that surface back, bleeding past the card's 24px
   padding so it reaches the edges. */
const surface = `
function EpiSurface({ children }) {
  /* Bleed past the card's padding so the surface reaches the edges -- but
     read that padding rather than assuming it. The grid card pads the body
     by 24px; a single-story card sets padding to 0 before mounting. A fixed
     margin of -24 is right for the first and pushes the surface 24px off the
     right edge of the second. The body's padding is already applied by the
     time this renders, so measuring it here is safe and synchronous. */
  var pad = 0;
  try { pad = parseFloat(getComputedStyle(document.body).paddingLeft) || 0; } catch (e) { pad = 0; }
  return React.createElement('div', {
    style: {
      background: 'var(--page)',
      color: 'var(--ink)',
      fontFamily: "'Space Grotesk', ui-sans-serif, system-ui, sans-serif",
      padding: pad, margin: -pad, minHeight: '100%', boxSizing: 'border-box',
    },
  }, children);
}
`;

const footer = `${surface}\nexport { ${Object.keys(COMPONENTS).join(', ')}, EpiSurface };\n`;

const js = esbuild.transformSync(banner + body + footer, {
  loader: 'jsx',
  jsx: 'transform',
  jsxFactory: 'React.createElement',
  jsxFragment: 'React.Fragment',
  format: 'esm',
  sourcefile: 'epi-ui.jsx',
});
for (const w of js.warnings) console.error(`! esbuild: ${w.text}`);

/* ---- emit ---- */
fs.mkdirSync(path.join(OUT, 'dist'), { recursive: true });
fs.writeFileSync(path.join(OUT, 'dist/index.js'), js.code);

const dts = `/* Generated by .design-sync/adapter/build-adapter.mjs from ${REF}. Do not edit. */
import * as React from 'react';

${Object.entries(COMPONENTS).map(([name, c]) => {
  const iface = c.props
    ? `export interface ${name}Props {\n  ${c.props}\n}`
    : `export interface ${name}Props {}`;
  return `/**\n * ${c.doc.replace(/\n/g, '\n * ')}\n */\n${iface}\nexport declare function ${name}(props: ${name}Props): React.ReactElement;`;
}).join('\n\n')}
`;
fs.writeFileSync(path.join(OUT, 'dist/index.d.ts'), dts);

for (const a of ASSETS) {
  fs.writeFileSync(path.join(OUT, a), show(`ui/epi/${a}`, 'buffer'));
}

/* Group assignments, for the converter's docsMap stubs. */
fs.mkdirSync(path.join(OUT, 'docs'), { recursive: true });
for (const [name, c] of Object.entries(COMPONENTS)) {
  fs.writeFileSync(path.join(OUT, `docs/${name}.md`),
    `---\ncategory: ${c.group}\n---\n\n# ${name}\n\n${c.doc}\n`);
}

console.error(`✓ adapter: ${Object.keys(COMPONENTS).length} components from ${REF} (${execFileSync('git', ['rev-parse', '--short', REF], { cwd: REPO, encoding: 'utf8' }).trim()}) -> ${path.relative(REPO, OUT)}`);
