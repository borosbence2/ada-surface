import { kViewModes, type DisplayOptions } from './display';

interface ToolbarProps {
  options: DisplayOptions;
  onChange: (options: DisplayOptions) => void;
  onOpen: () => void;
  onReload: () => void;
  canReload: boolean;
  onSolve: () => void;
  /** Disabled while solving, or with no scene or kernel. */
  canSolve: boolean;
  /** The scene fits a target, so its points can be shown. */
  hasTarget: boolean;
  onExport: () => void;
}

/** Comb scale slider runs over 10^-1 .. 10^1 of the automatic length. */
const kCombScaleDecades = 1;

export function Toolbar({
  options,
  onChange,
  onOpen,
  onReload,
  canReload,
  onSolve,
  canSolve,
  onExport,
  hasTarget,
}: ToolbarProps) {
  const set = (patch: Partial<DisplayOptions>) => onChange({ ...options, ...patch });
  return (
    <header className="toolbar">
      <button onClick={onOpen}>Open…</button>
      <button onClick={onReload} disabled={!canReload} title="Reload the scene from disk">
        Reload
      </button>
      <button
        onClick={onSolve}
        disabled={!canSolve}
        title="Solve the scene's recipes in the kernel and write the result back to the file"
      >
        Solve
      </button>
      <button
        onClick={onExport}
        disabled={!canSolve}
        title="Write the patches as IGES (entity 128, mm) for NX, CATIA or Alias"
      >
        Export IGES…
      </button>
      <span className="toolbar__divider" />
      <div className="segmented" role="radiogroup" aria-label="View mode">
        {kViewModes.map(({ mode, label, title }) => (
          <button
            key={mode}
            role="radio"
            aria-checked={options.mode === mode}
            className={options.mode === mode ? 'is-active' : ''}
            title={title}
            onClick={() => set({ mode })}
          >
            {label}
          </button>
        ))}
      </div>
      {(options.mode === 'zebra' || options.mode === 'reflection') && (
        <label className="toolbar__field" title="Stripes around the reflection axis (vehicle X)">
          stripes
          <input
            type="number"
            min={4}
            max={200}
            step={2}
            value={options.stripes}
            onChange={(e) => set({ stripes: Math.max(4, Number(e.target.value) || 4) })}
          />
        </label>
      )}
      <span className="toolbar__divider" />
      <label className="toolbar__check">
        <input
          type="checkbox"
          checked={options.showEdges}
          onChange={(e) => set({ showEdges: e.target.checked })}
        />
        edges
      </label>
      <label className="toolbar__check">
        <input
          type="checkbox"
          checked={options.showHull}
          onChange={(e) => set({ showHull: e.target.checked })}
        />
        CV hull
      </label>
      <label className="toolbar__check">
        <input
          type="checkbox"
          checked={options.showCombs}
          onChange={(e) => set({ showCombs: e.target.checked })}
        />
        combs
      </label>
      {hasTarget && (
        <label className="toolbar__check" title="The fit target's points, coloured by deviation">
          <input
            type="checkbox"
            checked={options.showTarget}
            onChange={(e) => set({ showTarget: e.target.checked })}
          />
          target
        </label>
      )}
      {options.showCombs && (
        <input
          type="range"
          min={-kCombScaleDecades}
          max={kCombScaleDecades}
          step={0.05}
          value={Math.log10(options.combScale)}
          title={`comb length x${options.combScale.toFixed(2)}`}
          onChange={(e) => set({ combScale: 10 ** Number(e.target.value) })}
        />
      )}
    </header>
  );
}
