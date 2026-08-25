import { useEffect, useRef } from 'react';
import { PickupWorkshop } from 'epi-ui';

/* Same containment note as TineWorkshop: `.modal-back` needs a positioned
   ancestor the size of the plugin canvas or the centred modal hangs off the
   top of the frame. cfg.overrides already sets PickupWorkshop to
   cardMode: single. */
const Stage = ({ children }: { children: React.ReactNode }) => (
  <div style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
    {children}
  </div>
);

/* PickupWorkshop takes only `onClose` -- there is no prop that selects a
   starting state, so the only way to show its TOLERANCE feature (the whole
   point of the bench: deterministic per-pickup manufacturing scatter) is to
   drive the same click a player would make. This fires the real onClick on
   the real chip, exactly the interaction the workshop is built for. */
/* PickupWorkshop starts `mods` as null and renders nothing until its own
   effect resolves (the fallback array, since there is no native bridge in a
   preview) — so the chip does not exist in the DOM yet on the tick this
   wrapper's effect first runs. Poll via MutationObserver instead of a plain
   querySelector so the click fires the instant the chip mounts, whichever
   render pass that lands on. */
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

export const Default = () => (
  <Stage><PickupWorkshop onClose={() => {}} /></Stage>
);

/* NEGLECTED is the widest tolerance the bench models -- a decade-untouched
   instrument -- so the three lanes actually show visible per-pickup scatter
   instead of the flat factory line. */
export const Neglected = () => {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => { if (ref.current) clickChip(ref.current, 'NEGLECTED'); }, []);
  return (
    <div ref={ref} style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
      <PickupWorkshop onClose={() => {}} />
    </div>
  );
};
