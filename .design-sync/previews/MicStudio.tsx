import { useEffect, useRef } from 'react';
import { MicStudio } from 'epi-ui';

/* Same containment note as TineWorkshop: `.modal-back` needs a positioned
   ancestor the size of the plugin canvas or the centred modal hangs off
   the top of the frame. cfg.overrides already sets MicStudio to
   cardMode: single, viewport 1240x840 -- do not change config, just use
   Stage. */
const Stage = ({ children }: { children: React.ReactNode }) => (
  <div style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
    {children}
  </div>
);

/* MicStudio starts `v`/`st` as null and renders nothing until its own
   effect resolves the mock's getMicMods/getMicStage promises -- so the
   MODE chips do not exist in the DOM yet on the tick a mount effect first
   runs. Poll via MutationObserver instead of a plain querySelector so the
   click fires the instant the chip mounts. */
function clickChip(root: HTMLElement, label: string) {
  const tryNow = () => {
    const btn = Array.from(root.querySelectorAll('button')).find((b) => b.textContent === label) as HTMLButtonElement | undefined;
    if (btn) { btn.click(); return true; }
    return false;
  };
  if (tryNow()) return;
  const obs = new MutationObserver(() => { if (tryNow()) obs.disconnect(); });
  obs.observe(root, { childList: true, subtree: true });
}

/* Default: the mock's getMicMods/getMicStage resolve to the CLASSIC PAIR
   calibrated-mode defaults, so this is what a player sees on first open --
   the SPREAD/BALANCE/DISTANCE/MIC L/MIC R knob bank and the calibrated
   PLACEMENTS row. */
export const Default = () => (
  <Stage><MicStudio onClose={() => {}} /></Stage>
);

/* Stage: clicking the real STAGE chip is the only way to reach the mode
   this component exists for -- free mic placement. It swaps the knob bank
   for MicStage's field + per-mic knobs and the PLACEMENTS row for the
   stage templates (CLASSIC PAIR / JAZZ LID / AMBIENT / UNDER + PAIR). */
export const StageMode = () => {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => { if (ref.current) clickChip(ref.current, 'STAGE'); }, []);
  return (
    <div ref={ref}>
      <Stage><MicStudio onClose={() => {}} /></Stage>
    </div>
  );
};
