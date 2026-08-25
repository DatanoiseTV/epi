import { PedalLamp } from 'epi-ui';

/* The sustain-pedal telemetry lamp: `.pedal-lamp` is `position: absolute;
   bottom: 62px; right: 14px`, positioned against the vizcard corner it
   normally sits in -- a bare <PedalLamp/> with no positioned ancestor
   would hang off the page. Each Frame recreates that corner's own
   containment (and, for Default, its actual radial-gradient background)
   rather than an arbitrary box.

   The mock's kinematic model emits `pedal: true` unconditionally on its
   16ms interval (juce-bridge.jsx), so within a frame or two of mount every
   instance on this sheet reads as held down -- there is no prop, and no
   reachable mock state, that shows the unlit LED. Two cells vary the
   PRESENTATION of that one real state instead of inventing a second one:
   natural size in its actual corner, and a scaled close-up so the gold LED
   glow and the letter-spaced label are legible at a distance a real corner
   crop would not give. */

const Corner = ({ children }: { children: React.ReactNode }) => (
  <div style={{
    position: 'relative', width: 240, height: 130,
    borderRadius: 10, overflow: 'hidden',
    background: 'radial-gradient(120% 140% at 50% -20%, #16140f 0%, #0b0a08 60%, #080807 100%)',
    border: '1px solid var(--line)',
  }}>{children}</div>
);

export const Default = () => (
  <Corner><PedalLamp /></Corner>
);

export const Detail = () => (
  <div style={{
    width: 340, height: 190, display: 'flex', alignItems: 'center', justifyContent: 'center',
    borderRadius: 10, background: '#0b0a08', border: '1px solid var(--line)',
  }}>
    <div style={{ position: 'relative', width: 150, height: 90, transform: 'scale(2.2)' }}>
      <PedalLamp />
    </div>
  </div>
);
