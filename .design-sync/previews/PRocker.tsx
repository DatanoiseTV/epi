import { PRocker } from 'epi-ui';

/* The real Clav tone row — four rocker switches, one of which
   (Medium) defaults ON so both states are visible across the sheet. */
export const Brilliant = () => <PRocker id="clavBrill" />;
export const Treble = () => <PRocker id="clavTreb" />;
export const Medium = () => <PRocker id="clavMed" />;
export const Soft = () => <PRocker id="clavSoft" />;
