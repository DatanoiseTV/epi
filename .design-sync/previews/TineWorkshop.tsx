import { TineWorkshop } from 'epi-ui';

/* The per-note tuning and geometry editor, opened over the interface as a
   modal. One workshop serves three resonators: the flags pick which, and
   they change the units, the lane scaling and which native event the edits
   are pushed to (tine_mod / string_mod / grand_mod).

   Every workshop scrim is `position: absolute; inset: 0`, so it fills its
   nearest POSITIONED ancestor. In the plugin that is #plugin, the fixed
   1180x820 design canvas. A preview card has no such ancestor, so the scrim
   collapses and the centred modal hangs off the top of the frame. Stage puts
   the canvas back -- this is the containment the component is written
   against, not preview decoration.

   Rendered one story per card (cardMode: single): these are full-canvas
   overlays and would paint over each other in a grid. */
const Stage = ({ children }: { children: React.ReactNode }) => (
  <div style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
    {children}
  </div>
);

export const Tine = () => (
  <Stage><TineWorkshop onClose={() => {}} /></Stage>
);

/* Strings read in 1200-cent octaves rather than the tine's 2400, so the same
   lane covers a different span — and the gauge lane re-reads as wire. */
export const Strings = () => (
  <Stage><TineWorkshop onClose={() => {}} strings /></Stage>
);

/* No `grand` cell on purpose. The flag switches the native event and which
   mods are fetched (grand_mod / getGrandMods), but the title is
   `strings ? 'String Workshop' : 'Tine Workshop'` -- so the grand renders
   pixel-identical to Tine. A third cell that repeats the second teaches the
   design agent nothing and reads as a broken variant axis. */
