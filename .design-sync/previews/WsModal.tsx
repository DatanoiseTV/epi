import { useState } from 'react';
import { WsModal, WsLane, Knob } from 'epi-ui';

/* WsModal is the reusable shell every workshop is built on top of: a
   click-outside scrim, a title bar with RESET ALL / close, and a body that
   can hold anything. The only honest way to preview it is with real
   content inside -- an empty shell teaches nothing about how it is used.

   Same containment note as TineWorkshop: `.modal-back` is
   `position: absolute; inset: 0` and needs a positioned ancestor the size
   of the plugin canvas, or the centred modal hangs off the top of the
   frame. cfg.overrides already sets WsModal to cardMode: single. */
const Stage = ({ children }: { children: React.ReactNode }) => (
  <div style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
    {children}
  </div>
);

const N = 88;
const stretch = (i: number) => {
  const d = (21 + i - 60) / 48;
  return Math.max(-1, Math.min(1, (32 * d * d * d) / 100));
};

/* A shell holding a single lane -- the shape TineWorkshop and PickupWorkshop
   both use, one lane at a time here so the shell itself stays legible. */
export const WithLane = () => {
  const [vals, setVals] = useState<number[]>(() => Array.from({ length: N }, (_, i) => stretch(i)));
  return (
    <Stage>
      <WsModal title="Tine Workshop" onReset={() => setVals(Array.from({ length: N }, () => 0))} onClose={() => {}}>
        <WsLane
          title="LENGTH" height={172}
          meta="re-cut the steel · pitch follows 1/L² · lane spans ±100 cents · shift paints fine"
          get={(i) => vals[i]}
          set={(i, v) => setVals((c) => { const n = c.slice(); n[i] = v; return n; })}
          resetOne={(i) => setVals((c) => { const n = c.slice(); n[i] = 0; return n; })}
          format={(i) => { const cts = vals[i] * 100; return (cts >= 0 ? '+' : '') + cts.toFixed(1) + ' cents'; }}
        />
      </WsModal>
    </Stage>
  );
};

/* The shell also carries a bench of knobs, not just lanes -- CabinetWorkshop's
   shape. Shown here so the two bodies a workshop can hold both read as
   deliberate uses of the same shell, not one canonical layout. */
export const WithKnobs = () => {
  const [box, setBox] = useState(0.74);
  const [cone, setCone] = useState(0.59);
  const [dist, setDist] = useState(0.5);
  return (
    <Stage>
      <WsModal title="Cabinet Workshop" onReset={() => { setBox(0.74); setCone(0.59); setDist(0.5); }} onClose={() => {}}>
        <div className="wscabknobs">
          <Knob value={box} size="lg" label="BOX" format={(v) => 'fc ' + Math.round(140 * Math.pow(60 / 140, v)) + ' Hz'}
                defaultValue={0.74} onChange={setBox} />
          <Knob value={cone} size="lg" label="CONE" format={(v) => (2.5 * Math.pow(6000 / 2500, v)).toFixed(1) + ' kHz'}
                defaultValue={0.59} onChange={setCone} />
          <Knob value={dist} size="lg" label="MIC DIST" format={(v) => Math.round(2 + 58 * v) + ' cm'}
                defaultValue={0.5} onChange={setDist} />
        </div>
        <div className="wsnote">THE BOX SETS THE RESONANCE, THE CONE SETS THE BREAKUP · SAVED WITH THE PROJECT</div>
      </WsModal>
    </Stage>
  );
};
