// The diverging colour map for signed curvature, defined once: the legend's
// CSS gradient and the surface shader's GLSL are both generated from these
// stops, so the two cannot disagree.

export type Rgb = readonly [number, number, number];

/**
 * ColorBrewer RdBu (5 classes), reversed so that positive curvature is red:
 * blue for negative, near-white for zero, red for positive. Positions are in
 * [-1, 1], the curvature divided by the displayed range.
 */
export const kDivergingStops: readonly (readonly [number, Rgb])[] = [
  [-1.0, [0x21 / 255, 0x66 / 255, 0xac / 255]],
  [-0.5, [0x67 / 255, 0xa9 / 255, 0xcf / 255]],
  [0.0, [0xf7 / 255, 0xf7 / 255, 0xf7 / 255]],
  [0.5, [0xef / 255, 0x8a / 255, 0x62 / 255]],
  [1.0, [0xb2 / 255, 0x18 / 255, 0x2b / 255]],
];

/** Colour at `t`, clamped to [-1, 1]; linear between stops. */
export function diverging(t: number): Rgb {
  const x = Math.min(1, Math.max(-1, t));
  for (let i = 1; i < kDivergingStops.length; i++) {
    const [p1, c1] = kDivergingStops[i]!;
    if (x <= p1) {
      const [p0, c0] = kDivergingStops[i - 1]!;
      const a = (x - p0) / (p1 - p0);
      // (1 - a) c0 + a c1, like GLSL's mix: exact at both ends, unlike
      // c0 + a (c1 - c0), which can miss c1 by an ulp.
      return [
        (1 - a) * c0[0] + a * c1[0],
        (1 - a) * c0[1] + a * c1[1],
        (1 - a) * c0[2] + a * c1[2],
      ];
    }
  }
  return kDivergingStops[kDivergingStops.length - 1]![1];
}

function glslVec3(c: Rgb): string {
  return `vec3(${c.map((v) => v.toFixed(6)).join(', ')})`;
}

/** GLSL `vec3 diverging(float t)` matching `diverging` above. */
export function divergingGlsl(): string {
  const lines = ['vec3 diverging(float t) {', '  float x = clamp(t, -1.0, 1.0);'];
  for (let i = 1; i < kDivergingStops.length; i++) {
    const [p0, c0] = kDivergingStops[i - 1]!;
    const [p1, c1] = kDivergingStops[i]!;
    lines.push(
      `  if (x <= ${p1.toFixed(6)}) return mix(${glslVec3(c0)}, ${glslVec3(c1)}, (x - (${p0.toFixed(6)})) / ${(p1 - p0).toFixed(6)});`,
    );
  }
  lines.push(`  return ${glslVec3(kDivergingStops[kDivergingStops.length - 1]![1])};`, '}');
  return lines.join('\n');
}

/** Left-to-right CSS gradient for the legend, from -1 to +1. */
export function divergingCss(): string {
  const stops = kDivergingStops.map(([p, c]) => {
    const rgb = c.map((v) => Math.round(v * 255)).join(', ');
    return `rgb(${rgb}) ${((p + 1) * 50).toFixed(1)}%`;
  });
  return `linear-gradient(to right, ${stops.join(', ')})`;
}
