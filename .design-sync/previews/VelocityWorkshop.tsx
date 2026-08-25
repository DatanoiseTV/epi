import { useEffect, useRef } from 'react';
import { VelocityWorkshop } from 'epi-ui';

/* Same containment note as TineWorkshop: `.modal-back` needs a positioned
   ancestor the size of the plugin canvas or the centred modal hangs off the
   top of the frame. cfg.overrides already sets VelocityWorkshop to
   cardMode: single. */
/* VelocityWorkshop starts `y` as null and renders nothing until its own
   effect resolves (the fallback identity map, since there is no native
   bridge in a preview) — so the chip does not exist in the DOM yet on the
   tick this wrapper's effect first runs. Poll via MutationObserver instead
   of a plain querySelector so the click fires the instant the chip mounts,
   whichever render pass that lands on. */
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

/* VelocityWorkshop takes only `onClose`; with no bridge answering getVelMap
   it falls back to the identity map -- REAL, the true bypass, dashed line
   only. There is no prop to start it anywhere else, so the second cell
   clicks HEAVY, the preset furthest from identity (a strongly compressed
   low end), which both bends the curve visibly off the dashed reference
   and swaps the mode readout from "REAL · CURVE BYPASSED" to "CURVED". */
export const Default = () => (
  <div style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
    <VelocityWorkshop onClose={() => {}} />
  </div>
);

export const Heavy = () => {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => { if (ref.current) clickChip(ref.current, 'HEAVY'); }, []);
  return (
    <div ref={ref} style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
      <VelocityWorkshop onClose={() => {}} />
    </div>
  );
};
