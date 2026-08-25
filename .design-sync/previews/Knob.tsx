import { useState } from 'react';
import { Knob } from 'epi-ui';

/* A row of knobs on the panel surface they normally sit on. Epi's knobs are
   laid out in `.krow` groups inside a panel, so previewing them in a row is
   the honest framing. */
const Row = ({ children, gap = 18 }: { children: React.ReactNode; gap?: number }) => (
  <div style={{ display: 'flex', alignItems: 'flex-end', gap, padding: '8px 4px' }}>{children}</div>
);

const pct = (v: number) => Math.round(v * 100) + '%';
const cents = (v: number) => ((v - 0.5) * 200).toFixed(1) + ' ¢';
const dB = (v: number) => (v * 36 - 24).toFixed(1) + ' dB';
const hz = (v: number) => Math.round(80 * Math.pow(2, v * 7)) + ' Hz';

export const Default = () => (
  <Row>
    <Knob value={0.62} label="HARDNESS" />
  </Row>
);

/* The three sizes all scale from the same 52px reference, so they should read
   as one family rather than three drawings. */
export const Sizes = () => (
  <Row>
    <Knob value={0.62} size="sm" label="SMALL" />
    <Knob value={0.62} size="md" label="MEDIUM" />
    <Knob value={0.62} size="lg" label="LARGE" />
  </Row>
);

/* Bipolar draws the arc out from top-centre, so a centred value shows no arc
   at all — that is the point: centre reads as "no correction applied". */
export const Bipolar = () => (
  <Row>
    <Knob value={0.5} bipolar label="TUNE" format={cents} />
    <Knob value={0.72} bipolar label="TUNE" format={cents} />
    <Knob value={0.28} bipolar label="TUNE" format={cents} />
  </Row>
);

/* `format` is how a normalised 0..1 becomes the unit the player thinks in. */
export const Formatted = () => (
  <Row>
    <Knob value={0.75} label="OUTPUT" format={dB} />
    <Knob value={0.4} label="COIL PEAK" format={hz} />
    <Knob value={0.55} label="BODY" format={pct} />
  </Row>
);

/* Label suppressed and value hidden — the bare dial, as used inline where the
   surrounding row already carries the naming. */
export const BareDial = () => (
  <Row>
    <Knob value={0.62} label={null} showValue={false} />
    <Knob value={0.35} label={null} showValue={false} size="sm" />
  </Row>
);

/* Drag vertically to change (240px covers the full range, shift for fine),
   double-click to snap back to defaultValue. */
export const Interactive = () => {
  const [v, setV] = useState(0.45);
  return (
    <Row>
      <Knob value={v} onChange={setV} defaultValue={0.45} label="DRAG ME" size="lg" />
    </Row>
  );
};
