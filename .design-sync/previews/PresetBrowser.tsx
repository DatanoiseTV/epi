import { PresetBrowser } from 'epi-ui';

/* Modal overlay: .modal-back is position:absolute; inset:0, so it needs a
   positioned ancestor the size of the real plugin canvas or the scrim
   collapses and the modal hangs off the frame. cfg.overrides for
   PresetBrowser is already {"cardMode":"single","viewport":"1240x840"}. */
const Stage = ({ children }: { children: React.ReactNode }) => (
  <div style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
    {children}
  </div>
);

/* The mock's listUserPresets always resolves empty, so the "User" section
   never appears regardless of props -- the only real variance available is
   currentName, which highlights whichever factory preset is loaded. */
export const Browsing = () => (
  <Stage>
    <PresetBrowser onClose={() => {}} currentName="Suitcase" />
  </Stage>
);

export const NoCurrentPreset = () => (
  <Stage>
    <PresetBrowser onClose={() => {}} />
  </Stage>
);
