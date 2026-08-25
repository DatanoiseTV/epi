import { PSeg } from 'epi-ui';

/* Real usage from the Clav panel: a wide four-way routing switch. */
export const ClavSwitch = () => (
  <PSeg id="clavSwitch" options={['CENTER', 'BRIDGE', 'BOTH', 'OUT OF PHASE']} wide />
);

/* Default (non-wide) width with a shorter option set. */
export const PickupSelect = () => (
  <PSeg id="pickupSel" options={['MAGNETIC', 'NATIVE', 'ELECTRO', 'CONTACT']} />
);

/* Five options, still at default width, to check wrapping/overflow. */
export const Instrument = () => (
  <PSeg id="instrument" options={['Tine', 'E-Grand', 'Reed', 'Grand', 'Clav']} />
);
