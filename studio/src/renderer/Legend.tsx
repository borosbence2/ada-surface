import type { FitDeviations } from '../shared/types';
import { divergingCss } from './colormap';
import { deviationRange, type DisplayOptions } from './display';

interface LegendProps {
  options: DisplayOptions;
  onChange: (options: DisplayOptions) => void;
  /** The fit target's deviations, when the scene has a fit. */
  deviations: FitDeviations | null;
}

function formatCurvature(value: number): string {
  return value.toExponential(1);
}

/**
 * The colour range of the curvature maps, always displayed and always set by
 * hand (CLAUDE.md: no auto-ranging). Entered as a radius, which is how
 * surfacers think about curvature.
 */
function CurvatureLegend({ options, onChange }: Omit<LegendProps, 'deviations'>) {
  const mean = options.mode === 'mean';
  const radius = mean ? options.meanRadiusMm : options.gaussianRadiusMm;
  const range = mean ? 1 / radius : 1 / (radius * radius);
  const unit = mean ? '1/mm' : '1/mm²';
  const setRadius = (value: number) => {
    if (!(value > 0)) return;
    onChange(mean ? { ...options, meanRadiusMm: value } : { ...options, gaussianRadiusMm: value });
  };

  return (
    <div className="legend">
      <div className="legend__title">{mean ? 'Mean curvature' : 'Gaussian curvature'}</div>
      <div className="legend__bar" style={{ background: divergingCss() }} />
      <div className="legend__labels">
        <span>−{formatCurvature(range)}</span>
        <span>0</span>
        <span>+{formatCurvature(range)}</span>
      </div>
      <label className="legend__range">
        full colour at R =
        <input
          type="number"
          min={1}
          step={100}
          value={radius}
          onChange={(e) => setRadius(Number(e.target.value))}
        />
        mm <span className="legend__unit">({unit})</span>
      </label>
      <div className="legend__note">magenta: undefined (degenerate point)</div>
    </div>
  );
}

/**
 * The target-deviation range: the scene's fit_max_mm budget unless the user
 * sets another, always displayed (CLAUDE.md: no auto-ranging).
 */
function DeviationLegend({ options, onChange, deviations }: LegendProps) {
  if (!deviations || !options.showTarget) {
    return null;
  }
  const range = deviationRange(options, deviations.budgetMm);
  const shown = deviations.positions.length / 3;
  return (
    <div className="legend">
      <div className="legend__title">Target deviation ({deviations.target})</div>
      <div className="legend__bar" style={{ background: divergingCss() }} />
      <div className="legend__labels">
        <span>−{range} mm</span>
        <span>0</span>
        <span>+{range} mm</span>
      </div>
      <label className="legend__range">
        full colour at
        <input
          type="number"
          min={0.001}
          step={0.1}
          value={range}
          onChange={(e) => {
            const value = Number(e.target.value);
            if (value > 0) onChange({ ...options, deviationRangeMm: value });
          }}
        />
        mm
        {deviations.budgetMm !== null &&
          options.deviationRangeMm === null &&
          ' (the fit_max_mm budget)'}
      </label>
      <div className="legend__note">
        red: the target lies on the {deviations.positiveTowards} side of the surface (above a hood,
        outboard of a door); grey: beyond the patches.{' '}
        {shown < deviations.total ? `${shown} of ${deviations.total} points.` : ''}
      </div>
    </div>
  );
}

/** The colour legends in use: curvature in the curvature views, target deviation with a fit. */
export function Legend(props: LegendProps) {
  const curvature = props.options.mode === 'mean' || props.options.mode === 'gaussian';
  if (!curvature && !(props.deviations && props.options.showTarget)) {
    return null;
  }
  return (
    <div className="legends">
      {curvature && <CurvatureLegend options={props.options} onChange={props.onChange} />}
      <DeviationLegend {...props} />
    </div>
  );
}
