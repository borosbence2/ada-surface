import type { ViewMode } from '../shared/views';

export { kViewModes, type ViewMode } from '../shared/views';

export interface DisplayOptions {
  mode: ViewMode;
  showHull: boolean;
  showCombs: boolean;
  showEdges: boolean;
  /** Multiplies the automatic comb length (longest comb = 8 % of the scene). */
  combScale: number;
  /** Stripes around the reflection axis, for zebra and reflection lines. */
  stripes: number;
  /**
   * Fixed colour ranges, never auto-ranged (CLAUDE.md): the radius in mm that
   * maps to full colour. Mean curvature spans +-1/R; Gaussian +-1/R^2.
   */
  meanRadiusMm: number;
  gaussianRadiusMm: number;
}

/** A radius of 2 m covers the body panels of a car without saturating. */
const kDefaultRangeRadiusMm = 2000;

export const kDefaultDisplay: DisplayOptions = {
  mode: 'zebra',
  showHull: false,
  showCombs: false,
  showEdges: true,
  combScale: 1,
  stripes: 64,
  meanRadiusMm: kDefaultRangeRadiusMm,
  gaussianRadiusMm: kDefaultRangeRadiusMm,
};
