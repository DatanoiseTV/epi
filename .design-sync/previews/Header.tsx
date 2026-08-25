import { Header } from 'epi-ui';

/* The app shell's top bar: wordmark, preset pill (reads the shared
   `presetInfo` event, which the mock fires as `{ name: 'Suitcase', dirty:
   false }` shortly after mount), output knob and the live meters. It has
   no width of its own (`.hdr` sizes to its flex content), so this frames
   it the way it actually sits: the top edge of the 1180px plugin card,
   same gradient and border, rounded only at the top since VizCard and the
   rack continue below it in the real shell.

   Header takes only `onOpenBrowser`, which affects a click handler and
   nothing paints from it -- with no other prop and the preset/meter state
   coming off the one shared bridge singleton every cell on this sheet
   would read, a second cell would not show a real variant, just the same
   bar rendered twice. One honest cell. */
const TopOfPanel = ({ children }: { children: React.ReactNode }) => (
  <div style={{
    width: 1160, borderRadius: '14px 14px 0 0', overflow: 'hidden',
    background: 'linear-gradient(180deg, var(--card-a), var(--card-b))',
    border: '1px solid var(--line3)', borderBottom: 'none',
  }}>{children}</div>
);

export const Default = () => (
  <TopOfPanel><Header onOpenBrowser={() => {}} /></TopOfPanel>
);
