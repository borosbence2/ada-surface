import type { EdgeName } from '../shared/types';

/**
 * A click this close to a patch side, in parameter units, selects that edge
 * rather than the patch. A twentieth of the patch is easy to hit on screen
 * without stealing clicks meant for the interior.
 */
export const kEdgePickThreshold = 0.05;

/** The edge of the unit parameter square nearest to (u, v), if within `threshold`. */
export function nearestEdge(u: number, v: number, threshold = kEdgePickThreshold): EdgeName | null {
  const distances: [EdgeName, number][] = [
    ['UMin', u],
    ['UMax', 1 - u],
    ['VMin', v],
    ['VMax', 1 - v],
  ];
  let best: [EdgeName, number] = distances[0]!;
  for (const d of distances) {
    if (d[1] < best[1]) {
      best = d;
    }
  }
  return best[1] <= threshold ? best[0] : null;
}
