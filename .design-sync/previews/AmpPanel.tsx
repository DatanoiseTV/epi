import { AmpPanel } from 'epi-ui';

/* AmpPanel only branches on inst for the last two instruments: Clav (4)
   gets its four tone rockers and drive/clarity pair, Grand (3) gets the
   desk's bare bass/treble/clarity shelf (no amplifier at all), and every
   other instrument (0-2) shares the electric amp with drive, tone,
   cabinet and the tremolo/pan section -- so Tine stands in for that shared
   path.

   There is deliberately NO second Tine cell showing tremolo engaged. The
   only way to raise tremDepth is to set it on the mock relay, and there is
   exactly one relay per parameter id shared by the whole page: in the card's
   grid render every cell reads it, so the nudge leaked into the plain Tine
   cell and both rendered "TREMOLO 55%". A cell whose state is a global
   mutation cannot coexist with a sibling that contradicts it — it makes the
   quiet cell lie. Variation here stays prop-driven (`inst`), which is
   per-cell and honest. */

export const Tine = () => <AmpPanel inst={0} />;

export const Grand = () => <AmpPanel inst={3} />;

export const Clav = () => <AmpPanel inst={4} />;
