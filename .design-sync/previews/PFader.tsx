import { PFader } from 'epi-ui';

/* Mid-travel fader with a solid, non-zero fill. */
export const Default = () => <PFader id="hammerHard" />;

/* Another parameter-bound fader at a different default position, to
   confirm the fill height actually tracks the underlying value. */
export const Escapement = () => <PFader id="escapement" />;

/* Bipolar parameter — centred at unity gain, so the fill sits at the
   midpoint rather than empty or full. */
export const Output = () => <PFader id="outGain" />;

/* The `label` prop overriding the PARAMS table's default label text. */
export const CustomLabel = () => <PFader id="velCurve" label="Velocity" />;
