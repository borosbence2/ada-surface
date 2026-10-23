/**
 * Distance from a bounding sphere's centre at which a perspective camera sees
 * the whole sphere. `fovYDeg` is the vertical field of view; in a window
 * narrower than it is tall the horizontal field of view is the tighter limit,
 * so both are checked.
 */
export function framingDistance(radius: number, fovYDeg: number, aspect: number): number {
  const halfY = (fovYDeg * Math.PI) / 360;
  const halfX = Math.atan(Math.tan(halfY) * aspect);
  return radius / Math.sin(Math.min(halfX, halfY));
}
