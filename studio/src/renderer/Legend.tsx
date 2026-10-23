import { divergingCss } from './colormap';
import type { DisplayOptions } from './display';

interface LegendProps {
  options: DisplayOptions;
  onChange: (options: DisplayOptions) => void;
}

function formatCurvature(value: number): string {
  return value.toExponential(1);
}

/**
 * The colour range of the curvature maps, always displayed and always set by
 * hand (CLAUDE.md: no auto-ranging). Entered as a radius, which is how
 * surfacers think about curvature.
 */
export function Legend({ options, onChange }: LegendProps) {
  if (options.mode !== 'mean' && options.mode !== 'gaussian') {
    return null;
  }
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
