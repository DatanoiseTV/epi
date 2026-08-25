import { useEffect } from 'react';
import { MaterialRow } from 'epi-ui';

/* MaterialRow's visible content is driven by two things at once: the `inst`
   prop (which coupling rule applies -- magnetic, electrostatic, or none for
   the Grand's mic'd board) and the live `material`/`pickupSel` combo state,
   which the JUCE mock seeds to MUSIC WIRE (ferrous, conductive) and NATIVE.
   That default pair is deaf-proof for every instrument, so a plain inst
   sweep would render four identical rows. The warning line is the actual
   design surface here, so these cells drive the mock combos to the
   material/instrument pairings that hit it -- a nudge on mount through the
   same relay `useJuceChoice` listens on, exactly like the calibration
   Interactive story pattern uses for sliders. Each cell is its own fresh
   page load (design-sync navigates per story), so there is no bleed
   between cells. */
const setMaterial = (materialIdx: number, pickupIdx?: number) => {
  try {
    (window as any).Juce.getComboBoxState('material').setChoiceIndex(materialIdx);
    if (pickupIdx !== undefined) (window as any).Juce.getComboBoxState('pickupSel').setChoiceIndex(pickupIdx);
  } catch {}
};

export const Tine = () => <MaterialRow inst={0} />;

const TineBrass = () => {
  useEffect(() => setMaterial(3), []); // BRASS: non-ferrous, conductive -- magnetic pickup goes eddy-only
  return <MaterialRow inst={0} />;
};
export const TineMagneticBlindSpot = () => <TineBrass />;

const ReedNylon = () => {
  useEffect(() => setMaterial(7), []); // NYLON: non-ferrous, non-conductive -- deaf to the electrostatic plate
  return <MaterialRow inst={2} />;
};
export const ReedElectrostaticBlindSpot = () => <ReedNylon />;

const ClavTitanium = () => {
  useEffect(() => setMaterial(4), []); // TITANIUM: non-ferrous, conductive -- magnetic bar pickup goes eddy-only
  return <MaterialRow inst={4} />;
};
export const ClavMagneticBlindSpot = () => <ClavTitanium />;
