import { PKnob } from 'epi-ui';

/* PKnob takes only a parameter id — the label, default, formatter and
   bipolarity all come from the PARAMS table, which mirrors ParameterIDs.h.
   These are real ids; passing one that is not in the table throws
   "mock: unknown slider id <id>". */
const Row = ({ children }: { children: React.ReactNode }) => (
  <div style={{ display: 'flex', alignItems: 'flex-end', gap: 18, padding: '8px 4px' }}>{children}</div>
);

/* The hammer group, exactly as ActionPanel lays it out. */
export const Default = () => (
  <Row>
    <PKnob id="hammerHard" label="HARDNESS" />
    <PKnob id="hammerMass" label="MASS" />
    <PKnob id="escapement" label="ESCAPEMENT" />
  </Row>
);

/* Labels are optional: without one the parameter's own label from the table
   is used, which is how most panels are written. */
export const LabelFromTable = () => (
  <Row>
    <PKnob id="preampDrive" />
    <PKnob id="bass" />
    <PKnob id="treble" />
  </Row>
);

/* A bipolar parameter — tune sits at centre by default and the arc grows out
   from top-centre in either direction. */
export const BipolarParameter = () => (
  <Row>
    <PKnob id="tune" />
    <PKnob id="outGain" />
  </Row>
);

/* hammerHard rather than an effect depth: its default is mid-travel, so the
   gold arc is actually drawn and the three sizes can be compared. A
   parameter defaulting to 0 renders three dark dials that differ only in
   diameter. */
export const Sizes = () => (
  <Row>
    <PKnob id="hammerHard" size="sm" label="SMALL" />
    <PKnob id="hammerHard" size="md" label="MEDIUM" />
    <PKnob id="hammerHard" size="lg" label="LARGE" />
  </Row>
);
