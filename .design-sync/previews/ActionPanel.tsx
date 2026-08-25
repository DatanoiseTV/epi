import { ActionPanel } from 'epi-ui';

/* ActionPanel's material rows are keyed off `inst`, not a straight 0..4
   sweep: instruments 0-2 (Tine, E-Grand, Reed) render the identical felt/
   hammer rows, Grand (3) adds a soft-pedal cycle to the hammer row, and Clav
   (4) drops both material rows entirely -- it has no hammer felt or keybed,
   only the strike-noise knob shared with the rest of the action. Three
   cells cover the three shapes this panel actually takes. */

export const Tine = () => <ActionPanel inst={0} />;

export const Grand = () => <ActionPanel inst={3} />;

export const Clav = () => <ActionPanel inst={4} />;
