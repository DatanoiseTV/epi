import { useEffect, useRef, useState } from 'react';
import { MicStage } from 'epi-ui';

/* The stage-mode editor inside MicStudio: the mic-chip row, the field
   (StageView) and the per-mic HEIGHT/GAIN/PAN knobs. Not an overlay --
   just a width the pane actually reads at inside the workshop body. */
const Frame = ({ children }: { children: React.ReactNode }) => (
  <div style={{ width: 520 }}>{children}</div>
);

/* MicStage owns its own `sel` internally (defaults to mic 1) and only
   takes `v`/`push` as props, so the second cell drives the real chip click
   a player would make to move the selection -- the same idiom the other
   workshops in this batch use to reach a non-default internal state. */
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

const WST_DEFAULT = [0,
  1, -0.5, 1.2, 0.6, 0, -0.7,
  1, 0.5, 1.2, 0.6, 0, 0.7,
  0, 0.0, 2.5, 1.0, 0, 0.0,
  0, -1.2, 0.4, 0.3, -6, -1,
  0, 1.2, 0.4, 0.3, -6, 1];

/* Default: CLASSIC PAIR, sel defaults to mic 1 -- HEIGHT 0.60 m, GAIN
   0.0 dB, PAN L70. */
export const Default = () => {
  const [v, setV] = useState<number[]>(WST_DEFAULT);
  return (
    <Frame>
      <MicStage v={v} push={setV} />
    </Frame>
  );
};

/* AMBIENT: three mics live (a wide pair plus a centre mic under the open
   lid), clicking chip "3" selects that centre mic -- HEIGHT 0.70 m, GAIN
   -4.0 dB, PAN C, visibly different knob readings from Default's mic 1. */
const WST_AMBIENT = [1,
  1, -0.8, 3.2, 1.4, 0, -0.8,
  1, 0.8, 3.2, 1.4, 0, 0.8,
  1, 0.0, 1.2, 0.7, -4, 0.0,
  0, -1.2, 0.4, 0.3, -6, -1,
  0, 1.2, 0.4, 0.3, -6, 1];

export const Ambient = () => {
  const [v, setV] = useState<number[]>(WST_AMBIENT);
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => { if (ref.current) clickChip(ref.current, '3'); }, []);
  return (
    <div ref={ref}>
      <Frame>
        <MicStage v={v} push={setV} />
      </Frame>
    </div>
  );
};
