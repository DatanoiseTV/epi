import { useEffect, useRef } from 'react';
import { CabinetWorkshop } from 'epi-ui';

/* Same containment note as TineWorkshop: `.modal-back` needs a positioned
   ancestor the size of the plugin canvas or the centred modal hangs off the
   top of the frame. cfg.overrides already sets CabinetWorkshop to
   cardMode: single. */
/* CabinetWorkshop starts `v` as null and renders nothing until its own
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

/* CabinetWorkshop takes only `onClose`; with no native bridge to answer
   getCabMods it falls back to [0.74, 0.59, 0.5, 0.25, 0.5] -- which is
   exactly the SUITCASE template's own values. So the untouched default and
   a "Suitcase" cell would be pixel-identical; instead the second cell
   clicks BASS 1×15, the template furthest from the fallback (low box
   resonance, small cone breakup, close mic, more suspension travel), so the
   five knobs visibly move. */
export const Default = () => (
  <div style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
    <CabinetWorkshop onClose={() => {}} />
  </div>
);

export const Bass1x15 = () => {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => { if (ref.current) clickChip(ref.current, 'BASS 1×15'); }, []);
  return (
    <div ref={ref} style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
      <CabinetWorkshop onClose={() => {}} />
    </div>
  );
};
