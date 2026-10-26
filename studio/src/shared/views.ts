// Studio's view modes, shared by the toolbar and the `--view=` flag, so that
// the flag can be checked in the main process before a window opens.

export type ViewMode = 'shaded' | 'zebra' | 'reflection' | 'highlight' | 'mean' | 'gaussian';

export const kViewModes: readonly { mode: ViewMode; label: string; title: string }[] = [
  { mode: 'shaded', label: 'Shaded', title: 'Neutral studio lighting' },
  {
    mode: 'zebra',
    label: 'Zebra',
    title: 'Reflected stripes: they jump at G0 gaps, kink at G1 joins, flow smoothly across G2',
  },
  { mode: 'reflection', label: 'Reflection', title: 'Reflected lines: thin-line version of zebra' },
  {
    mode: 'highlight',
    label: 'Highlights',
    title: 'Beier-Chen highlight lines: the ones the quality report counts',
  },
  { mode: 'mean', label: 'Mean K', title: 'Mean curvature, fixed colour range' },
  { mode: 'gaussian', label: 'Gauss K', title: 'Gaussian curvature, fixed colour range' },
];

/**
 * The mode a `--view=` value names: its id or its toolbar label, in any case
 * ("highlight", "Highlights", "mean k"). Null for anything else, so a typo is
 * reported instead of quietly showing the default view.
 */
export function resolveViewMode(name: string): ViewMode | null {
  const wanted = name.trim().toLowerCase();
  const found = kViewModes.find((m) => m.mode === wanted || m.label.toLowerCase() === wanted);
  return found?.mode ?? null;
}
