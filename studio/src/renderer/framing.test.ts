import { describe, expect, it } from 'vitest';

import { framingDistance } from './framing';

describe('framingDistance', () => {
  it('fits the sphere to the vertical field of view in a square window', () => {
    // 90° vertical FOV: the sphere touches both planes at 45°, so d = r / sin 45°.
    expect(framingDistance(1, 90, 1)).toBeCloseTo(Math.SQRT2, 12);
  });

  it('is limited by the vertical field of view in a wide window', () => {
    expect(framingDistance(1, 90, 2)).toBeCloseTo(framingDistance(1, 90, 1), 12);
  });

  it('is limited by the horizontal field of view in a tall window', () => {
    // aspect 0.5: tan(halfX) = 0.5, so sin(halfX) = 0.5 / sqrt(1.25) and d = sqrt(5).
    expect(framingDistance(1, 90, 0.5)).toBeCloseTo(Math.sqrt(5), 12);
  });

  it('scales linearly with the radius', () => {
    expect(framingDistance(2500, 35, 1.6)).toBeCloseTo(2500 * framingDistance(1, 35, 1.6), 9);
  });
});
