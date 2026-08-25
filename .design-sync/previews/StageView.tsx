import { useState } from 'react';
import { StageView } from 'epi-ui';

/* The top-down mic-placement field inside MicStage (itself inside
   MicStudio's STAGE mode). It is a plain SVG that reads `width: 100%` of
   its parent -- not an overlay, so no positioned Stage/canvas wrapper is
   needed, just a width the field actually reads at inside the workshop
   body it normally lives in. */
const Frame = ({ children }: { children: React.ReactNode }) => (
  <div style={{ width: 480 }}>{children}</div>
);

/* CLASSIC PAIR, mic 1 selected: the field's own default (WST_DEFAULT). Two
   active mics out front of the bridge (gold = selected, dim gold =
   active-but-not-selected), two muted mics further back (pale, unselected). */
const WST_DEFAULT = [0,
  1, -0.5, 1.2, 0.6, 0, -0.7,
  1, 0.5, 1.2, 0.6, 0, 0.7,
  0, 0.0, 2.5, 1.0, 0, 0.0,
  0, -1.2, 0.4, 0.3, -6, -1,
  0, 1.2, 0.4, 0.3, -6, 1];

export const ClassicPair = () => {
  const [v, setV] = useState<number[]>(WST_DEFAULT);
  const [sel, setSel] = useState(0);
  return (
    <Frame>
      <StageView v={v} sel={sel} onSelect={setSel}
                 onMove={(i, x, z) => setV((c) => { const n = c.slice(); n[2 + i * 6] = x; n[3 + i * 6] = z; return n; })} />
    </Frame>
  );
};

/* UNDER + PAIR, the under-board mic selected: it sits at h -0.5 (negative
   height), which the field marks with a dashed ring -- a state Classic
   Pair never shows. Selecting that mic rather than mic 1 is deliberate: it
   proves the dashed ring is the SELECTED mic's own reading, not scenery. */
const WST_UNDER_PAIR = [1,
  1, -0.5, 1.0, 0.6, 0, -0.7,
  1, 0.5, 1.0, 0.6, 0, 0.7,
  1, 0.0, 0.6, -0.5, -3, 0.0,
  0, -1.2, 0.4, 0.3, -6, -1,
  0, 1.2, 0.4, 0.3, -6, 1];

export const UnderBoard = () => {
  const [v, setV] = useState<number[]>(WST_UNDER_PAIR);
  const [sel, setSel] = useState(2);
  return (
    <Frame>
      <StageView v={v} sel={sel} onSelect={setSel}
                 onMove={(i, x, z) => setV((c) => { const n = c.slice(); n[2 + i * 6] = x; n[3 + i * 6] = z; return n; })} />
    </Frame>
  );
};
