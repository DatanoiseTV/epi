import { LiveBar } from 'epi-ui';

/* `noteHz` in the mock feed is a fixed 82 Hz drone (not tied to the
   strike-decay envelope), so this bar sits at a steady, informative
   fill rather than one that flickers with the mock's 2.4 s strike
   cycle. */
export const Pitch = () => <LiveBar field="noteHz" full={200} label="Pitch" unit=" Hz" digits={0} />;

/* `lastNote` is a constant MIDI number in the mock — a steady mid-way
   fill, distinct from Pitch's. */
export const LastNote = () => <LiveBar field="lastNote" full={88} label="Last Note" digits={0} />;

/* `strikes` only ever counts up from the mock's very first tick, so
   it is reliably non-zero the instant the page renders. */
export const Strikes = () => <LiveBar field="strikes" full={10} label="Strikes" digits={0} />;

/* `pedal` is held down in the mock — a full bar, to show the track at
   its 100% end rather than only ever mid-fill. */
export const Sustain = () => <LiveBar field="pedal" full={1} label="Sustain" digits={0} />;
