import { App } from 'epi-ui';

/* The whole interface on its fixed 1180x820 design canvas. `#plugin` is
   `position: absolute; left/top: 50%; transform: translate(-50%,-50%)`, so
   it needs the same positioned Stage every modal in this batch uses --
   without it the centred plugin card has nothing to centre against. This
   is the showcase card; it is meant to render large. See learnings for the
   cardMode/viewport override this needs. */
const Stage = ({ children }: { children: React.ReactNode }) => (
  <div style={{ position: 'relative', width: 1180, height: 820, overflow: 'hidden', background: 'var(--page)' }}>
    {children}
  </div>
);

export const Default = () => (
  <Stage><App /></Stage>
);
