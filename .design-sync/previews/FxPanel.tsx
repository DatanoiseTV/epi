import { useEffect } from 'react';
import { FxPanel } from 'epi-ui';

/* FxPanel takes no props -- it's the post-signal-chain phaser and room, the
   same for every instrument. phaserMix defaults to 0 (off) and spaceMix
   defaults to a small 0.15 room, so the panel's resting state is
   deliberately understated. The second cell nudges both mixes and the room
   profile combo up through the mock relays (same pattern as MaterialRow) so
   the engaged state -- a real thing a player reaches for on this panel --
   also gets graded. */

export const Default = () => <FxPanel />;

const Engaged = () => {
  useEffect(() => {
    try {
      (window as any).Juce.getSliderState('phaserMix').setNormalisedValue(0.65);
      (window as any).Juce.getSliderState('phaserDepth').setNormalisedValue(0.8);
      (window as any).Juce.getSliderState('spaceMix').setNormalisedValue(0.5);
      (window as any).Juce.getComboBoxState('roomProfile').setChoiceIndex(4); // HALL
    } catch {}
  }, []);
  return <FxPanel />;
};
export const PhaserAndHallEngaged = () => <Engaged />;
