import { PCycle } from 'epi-ui';

/* Real usage from the Body panel — eight materials. */
export const Body = () => (
  <PCycle
    id="bodyMat"
    options={['STOCK', 'SPRUCE', 'MAPLE', 'BIRCH PLY', 'ALUMINIUM', 'STEEL', 'BRASS', 'CARBON']}
    label="BODY"
  />
);

/* Real usage from the Strings panel — string material. */
export const Material = () => (
  <PCycle
    id="material"
    options={['MUSIC WIRE', 'STAINLESS', 'BRONZE', 'BRASS', 'TITANIUM', 'ALUMINIUM', 'TUNGSTEN', 'NYLON']}
    label="MATERIAL"
  />
);

/* Real usage from the Hammer panel — six options, shorter label. */
export const Hammer = () => (
  <PCycle
    id="hammerMat"
    options={['STOCK', 'SOFT FELT', 'HARD FELT', 'LACQUERED', 'LEATHER', 'WOOD']}
    label="HAMMER"
  />
);

/* Real usage from the Space panel. */
export const Space = () => (
  <PCycle
    id="roomProfile"
    options={['CUSTOM', 'BOOTH', 'STUDIO', 'STAGE', 'HALL', 'CHURCH']}
    label="SPACE"
  />
);
