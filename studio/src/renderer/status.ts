import type { Check, EdgeName, QualityReportJson } from '../shared/types';

export type Status = 'pass' | 'warn' | 'fail';

/** Green: comfortably inside the budget. Amber: inside, but past half of it. Red: over. */
export const kStatusColors: Record<Status, string> = {
  pass: '#4caf6e',
  warn: '#e0a63a',
  fail: '#e05252',
};

/** Above this share of its budget a passing check shows amber. */
const kWarnShare = 0.5;

/**
 * Share of the budget a check uses. Flags (fold, curvature reversal) have a
 * budget of 0 and a value of 0 or 1: unset uses none of it, set is over.
 */
export function budgetUse(check: Check): number {
  if (check.budget > 0) {
    return check.value / check.budget;
  }
  return check.value > 0 ? Infinity : 0;
}

export function statusOf(checks: readonly Check[]): Status {
  let worst = 0;
  for (const check of checks) {
    if (!check.pass) {
      return 'fail';
    }
    worst = Math.max(worst, budgetUse(check));
  }
  return worst > kWarnShare ? 'warn' : 'pass';
}

const kSeverity: Record<Status, number> = { pass: 0, warn: 1, fail: 2 };

/** "front.VMax" -> { patch: "front", edge: "VMax" }. Patch ids may contain dots. */
export function parseEdgeRef(ref: string): { patch: string; edge: EdgeName } {
  const dot = ref.lastIndexOf('.');
  return { patch: ref.slice(0, dot), edge: ref.slice(dot + 1) as EdgeName };
}

export function edgeKey(patch: string, edge: EdgeName): string {
  return `${patch}.${edge}`;
}

/**
 * Status of every patch edge that takes part in an edge or symmetry link,
 * keyed by "patch.Edge". An edge in several links shows the worst of them;
 * edges in no link are absent (free boundaries).
 */
export function edgeStatuses(report: QualityReportJson): Map<string, Status> {
  const statuses = new Map<string, Status>();
  const add = (ref: string, status: Status) => {
    const { patch, edge } = parseEdgeRef(ref);
    const key = edgeKey(patch, edge);
    const current = statuses.get(key);
    if (current === undefined || kSeverity[status] > kSeverity[current]) {
      statuses.set(key, status);
    }
  };
  for (const e of report.edges) {
    const status = statusOf(e.checks);
    add(e.a, status);
    add(e.b, status);
  }
  for (const s of report.symmetry) {
    add(s.edge, statusOf(s.checks));
  }
  return statuses;
}
