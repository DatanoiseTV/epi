import { VizCard } from 'epi-ui';

/* The harp visualizer: an 88-key strip with each note's tine/string fanning
   up from it, drawn straight from the mock's fabricated `levels` feed (a
   held chord plus sympathetic wash) via useEventRef/useJuceChoice on the
   shared bridge singleton. It takes no props -- the instrument mode is
   read off the same 'instrument' combo every other Epi component shares,
   so a second cell here would just race the first for that shared state
   rather than showing a real variant; one honest cell is what the
   component actually offers without props. VizCard brings its own margin,
   border and background (.vizcard), so no extra framing is needed -- it is
   1128px wide, wider than a grid cell (see learnings: cardMode column). */
export const Default = () => <VizCard />;
