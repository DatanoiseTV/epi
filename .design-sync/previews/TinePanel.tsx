import { TinePanel } from 'epi-ui';

/* TinePanel is a full per-instrument switch -- each of the five branches is
   a different resonator model with its own knob set, workshop button and
   note line (tine spring/bar/bloom, twin unison strings, a reed tongue and
   clamp, a grand's three-string course, or the Clav's tangent-held strings).
   All five are genuinely distinct, so all five get a cell. */

export const Tine = () => <TinePanel inst={0} />;

export const EGrand = () => <TinePanel inst={1} />;

export const Reed = () => <TinePanel inst={2} />;

export const Grand = () => <TinePanel inst={3} />;

export const Clav = () => <TinePanel inst={4} />;
