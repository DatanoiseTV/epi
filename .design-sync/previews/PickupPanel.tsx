import { PickupPanel } from 'epi-ui';

/* Same shape as TinePanel: PickupPanel switches its whole body per
   instrument, because each instrument makes sound through a genuinely
   different transducer -- a magnetic pickup with a workshop, a spaced mic
   pair, an electrostatic plate off the polarising supply, a pair of fixed
   piezo bridges with literally no knobs, and the Clav's twin bar pickups
   behind a phase-select switch. All five are real, distinct panels. */

export const Tine = () => <PickupPanel inst={0} />;

export const EGrand = () => <PickupPanel inst={1} />;

export const Reed = () => <PickupPanel inst={2} />;

export const Grand = () => <PickupPanel inst={3} />;

export const Clav = () => <PickupPanel inst={4} />;
