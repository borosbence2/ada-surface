// Types shared by the main process, the preload bridge and the renderer. The
// kernel's JSON shapes are mirrored here; ada-mcp is the source of truth for
// them (apps/mcp/tools.cpp and core/src/analysis/quality.cpp).

export type EdgeName = 'UMin' | 'UMax' | 'VMin' | 'VMax';

export type KernelStatus =
  | { state: 'starting' }
  | { state: 'ready'; version: string; binary: string }
  | { state: 'stopped'; message: string };

export interface LightJson {
  origin: number[];
  normal: number[];
  direction: number[];
  across: number[];
  spacing: number;
}

/** One patch tessellated on a grid x grid lattice, attributes decoded from base64. */
export interface PatchMesh {
  id: string;
  positions: Float32Array;
  normals: Float32Array;
  uvs: Float32Array;
  indices: Uint32Array;
  gaussian: Float32Array;
  mean: Float32Array;
  highlight: Float32Array;
}

export interface Tessellation {
  grid: number;
  light: LightJson;
  patches: PatchMesh[];
}

export interface CvHull {
  id: string;
  degree: [number, number];
  control_points: [number, number, number][];
}

export interface CombEdge {
  edge: EdgeName;
  points: number[];
  directions: number[];
  curvature: number[];
}

export interface PatchCombs {
  id: string;
  edges: CombEdge[];
}

export interface Check {
  name: string;
  value: number;
  budget: number;
  pass: boolean;
}

export interface PatchReportJson {
  id: string;
  degree: [number, number];
  cv_count: number;
  curvature: {
    gaussian: [number, number];
    mean: [number, number];
    max_abs_principal: number;
    saddle_fraction: number;
    mean_changes_sign: boolean;
  };
  highlights: { lines: number; max_inflections: number; max_curvature_rate: number };
  cv_hull: { rows: number; zigzag_rows: number; max_row_inflections: number };
  checks: Check[];
  pass: boolean;
}

export interface EdgeReportJson {
  a: string; // "patch.Edge"
  b: string;
  continuity: 'G0' | 'G1' | 'G2';
  g0_gap_mm: number;
  g1_angle_deg: number;
  g2_relative: number;
  fold: boolean;
  checks: Check[];
  pass: boolean;
}

export interface SymmetryReportJson {
  edge: string;
  continuity: 'G0' | 'G1' | 'G2';
  g0_plane_distance_mm: number;
  g1_normal_tilt_deg: number;
  checks: Check[];
  pass: boolean;
}

/** How far the scene's fit target is from its patches (L3). */
export interface FitReportJson {
  target: string;
  loaded: boolean;
  points: number;
  outside: number;
  max_mm: number;
  rms_mm: number;
  p95_mm: number;
  checks: Check[];
  pass: boolean;
}

export interface QualityReportJson {
  pass: boolean;
  patches: PatchReportJson[];
  edges: EdgeReportJson[];
  symmetry: SymmetryReportJson[];
  /** Present when the scene fits a target. */
  fit?: FitReportJson;
}

/** The fit target's points (thinned) and their signed distance from the patches. */
export interface FitDeviations {
  target: string;
  /** Points in the whole target; `positions` may hold fewer. */
  total: number;
  /** xyz per point, mm. */
  positions: Float32Array;
  /** Signed distance per point, mm, positive towards `positiveTowards`; NaN beyond the patches. */
  deviation: Float32Array;
  /** "+z" for a hood or roof, "+y" for a right-hand door: the axis the surface mostly faces. */
  positiveTowards: string;
  /** The scene's `fit_max_mm` budget, the colour range unless the user sets one. */
  budgetMm: number | null;
}

/** Everything the viewport needs for one scene, fetched from the kernel in one go. */
export interface SceneBundle {
  path: string;
  report: QualityReportJson;
  tessellation: Tessellation;
  hulls: CvHull[];
  combs: PatchCombs[];
  /** Null when the scene fits no target. */
  deviations: FitDeviations | null;
}

export type LoadResult = { ok: true; bundle: SceneBundle } | { ok: false; error: string };

/** Result of an IGES export; null when the user cancelled the save dialog. */
export type ExportOutcome =
  | { ok: true; path: string; patches: number }
  | { ok: false; error: string };

/** What the kernel's `solve` tool reports back (it also writes the scene file). */
export type SolveOutcome =
  | {
      ok: true;
      pass: boolean;
      milliseconds: number;
      /** Curves that had to give way for continuity, largest first. */
      moved: { curve: string; max_mm: number }[];
    }
  | { ok: false; error: string };

/** What the user picked in the viewport; written to selection.json for the agent. */
export interface Selection {
  scene: string;
  patch: string;
  edge: EdgeName | null;
  uv: [number, number];
  point: [number, number, number];
}

/** How Studio was asked to start: `--scene=`, `--view=` (or ADA_SCENE), else the last scene. */
export interface Startup {
  scene: string | null;
  /** One of the view modes ('shaded', 'zebra', ...); null for the default. */
  view: string | null;
}
