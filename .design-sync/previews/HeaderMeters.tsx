import { HeaderMeters } from 'epi-ui';

/* HeaderMeters's own track/fill styling (`.hdr .meters`, `.hdr .mtrack`,
   `.hdr .mfill` in epi.css) is scoped to a `.hdr` ancestor -- the plugin's
   real title-bar container -- so every cell wraps it in that class,
   the same way app.jsx's header row does. Without it the tracks have
   no width/height/background at all and the cell is a true blank.

   Even correctly parented, the live `out` feed's strike-decay envelope
   means a static capture usually lands near the -90 dBFS floor, so the
   gold fill itself is close to invisible -- that's the real idle
   appearance, not a preview bug. Each cell keeps a caption so the card
   reads as intentional rather than broken. */
export const Default = () => (
  <div className="hdr" style={{ padding: 12 }}>
    <span style={{ fontSize: 11, letterSpacing: '0.08em', opacity: 0.65, marginRight: 10 }}>
      OUTPUT
    </span>
    <HeaderMeters />
  </div>
);

/* The real placement: right end of the plugin's title bar, next to
   the instrument name. */
export const InTitleBar = () => (
  <div className="hdr" style={{ padding: 12, justifyContent: 'space-between', width: 260 }}>
    <span style={{ fontSize: 13, letterSpacing: '0.04em' }}>Suitcase &middot; Tine</span>
    <HeaderMeters />
  </div>
);

/* The mock drives a periodic struck-note envelope, so the exact fill
   caught by any one capture varies with when in that 2.4 s decay the
   screenshot lands -- the caption describes the mechanism rather than
   asserting one instantaneous reading. */
export const LiveLevel = () => (
  <div className="hdr" style={{ padding: 12, flexDirection: 'column', alignItems: 'flex-start', gap: 6 }}>
    <span style={{ fontSize: 10, letterSpacing: '0.06em', opacity: 0.55 }}>
      L / R output, live from the `levels` feed
    </span>
    <HeaderMeters />
  </div>
);
