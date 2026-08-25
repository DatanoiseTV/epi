import { PHead } from 'epi-ui';

/* Real usage from the Strings panel: title plus a live tuning readout. */
export const Default = () => <PHead title="Strings" meta="440.0 Hz" />;

/* Real usage from the Pickups panel — a short fixed meta tag. */
export const Pickups = () => <PHead title="Pickups" meta="TWIN BAR" />;

/* Real usage from the Bridge panel. */
export const Bridge = () => <PHead title="Bridge" meta="PIEZO" />;

/* No meta at all — the section rule alone. */
export const Air = () => <PHead title="Air" />;
