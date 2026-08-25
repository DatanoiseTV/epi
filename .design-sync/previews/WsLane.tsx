import { useState } from 'react';
import { WsLane } from 'epi-ui';

/* WsLane is not a modal -- it is one paintable per-note lane across the
   88-key compass, the primitive every workshop lane (LENGTH, GAUGE, HEIGHT,
   GAP, WINDING) is built from. It needs working get/set to draw a shape at
   all; a flat lane would be dishonest since every real lane in the plugin
   carries some curve. Each cell below is one of the three curve shapes the
   real workshops actually paint -- a smooth cubic (tuning stretch), a
   Gaussian bump (gauge taper), and deterministic per-note scatter
   (manufacturing tolerance) -- so the variant axis is the shape itself, not
   just the label. Not wrapped in a Stage: WsLane is a plain block element,
   not a full-canvas overlay, but at 968px wide it likely needs
   cfg.overrides.WsLane = { cardMode: "column" } to avoid being cropped in a
   grid cell -- flagged in learnings. */

const N = 88, LO = 21;

/* Railsback-like octave stretch: flat at middle C, bass eased down and
   treble up, as a real piano is laid. Lane units [-1, 1] over a ±100 cent
   span, same as the tine workshop's LENGTH lane. */
const stretchCents = (i: number) => {
  const d = (LO + i - 60) / 48;
  return 32 * d * d * d;
};

/* Gauge taper: heavier wire through the middle register, thinning at both
   ends -- GONG template shape from the tine workshop's GAUGE lane. Lane
   units are log2(diameter multiplier). */
const gaugeGong = (i: number) => {
  const reg = i / (N - 1);
  const dia = 1 + 0.45 * Math.exp(-Math.pow((reg - 0.45) / 0.3, 2));
  return Math.log2(dia);
};

/* Deterministic per-note scatter, same hash the workshops use for
   manufacturing tolerance -- WORN severity on the pickup bench's WINDING
   lane. Lane units are log2(scale)/0.5. */
const hash = (i: number, ch: number) => {
  const h = (((i + 131 * ch) * 2654435761) >>> 0) & 65535;
  return h / 32767.5 - 1;
};
const windingScatter = (i: number) => {
  const sPct = 0.15;
  const s = Math.pow(2, hash(i, 3) * Math.log2(1 + sPct));
  return Math.log2(s) / 0.5;
};

function useLane(seed: (i: number) => number, resetTo = 0) {
  const [vals, setVals] = useState<number[]>(() => Array.from({ length: N }, (_, i) => seed(i)));
  return {
    get: (i: number) => vals[i],
    set: (i: number, v: number) => setVals((c) => { const n = c.slice(); n[i] = v; return n; }),
    resetOne: (i: number) => setVals((c) => { const n = c.slice(); n[i] = resetTo; return n; }),
    at: (i: number) => vals[i],
  };
}

export const Length = () => {
  const lane = useLane((i) => stretchCents(i) / 100);
  return (
    <WsLane
      title="LENGTH" height={172}
      meta="re-cut the steel · pitch follows 1/L² · lane spans ±100 cents · shift paints fine"
      get={lane.get} set={lane.set} resetOne={lane.resetOne}
      format={(i) => { const c = lane.at(i) * 100; return (c >= 0 ? '+' : '') + (Math.abs(c) < 20 ? c.toFixed(1) : Math.round(c)) + ' cents'; }}
    />
  );
};

export const Gauge = () => {
  const lane = useLane(gaugeGong);
  return (
    <WsLane
      title="GAUGE" height={88}
      meta="swap the wire · pitch stands, the overtones move"
      get={lane.get} set={lane.set} resetOne={lane.resetOne}
      format={(i) => Math.round(Math.pow(2, lane.at(i)) * 100) + '% gauge'}
    />
  );
};

export const Winding = () => {
  const lane = useLane(windingScatter, 0);
  return (
    <WsLane
      title="WINDING" height={104}
      meta="turns on the coil · scales this pickup's contribution · 70–140%"
      get={lane.get} set={lane.set} resetOne={lane.resetOne}
      format={(i) => Math.round(Math.pow(2, lane.at(i) * 0.5) * 100) + '%'}
    />
  );
};
