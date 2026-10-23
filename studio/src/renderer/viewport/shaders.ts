import { divergingGlsl } from '../colormap';
import type { ViewMode } from '../display';

export const kModeIndex: Record<ViewMode, number> = {
  shaded: 0,
  zebra: 1,
  reflection: 2,
  highlight: 3,
  mean: 4,
  gaussian: 5,
};

// three.js compiles ShaderMaterial as GLSL ES 3.00 on WebGL2 and maps the
// GLSL 1 spellings (attribute, varying, gl_FragColor) onto it, so fwidth and
// isnan are available.

export const kSurfaceVertex = /* glsl */ `
attribute float aGaussian;
attribute float aMean;
attribute float aHighlight;

varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying float vGaussian;
varying float vMean;
varying float vHighlight;

void main() {
  vec4 world = modelMatrix * vec4(position, 1.0);
  vWorldPos = world.xyz;
  vWorldNormal = mat3(modelMatrix) * normal;
  vGaussian = aGaussian;
  vMean = aMean;
  vHighlight = aHighlight;
  gl_Position = projectionMatrix * viewMatrix * world;
}
`;

export const kSurfaceFragment = /* glsl */ `
uniform int uMode;
uniform float uStripes;
uniform vec3 uStripeAxis;
uniform float uMeanRange;
uniform float uGaussianRange;
uniform float uHighlightSpacing;

varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying float vGaussian;
varying float vMean;
varying float vHighlight;

const float kTwoPi = 6.28318530718;
const vec3 kPaint = vec3(0.78, 0.80, 0.83);
const vec3 kUndefined = vec3(0.85, 0.15, 0.75);

${divergingGlsl()}

// Neutral studio light, fixed to the world (Z up) so that turning the camera
// does not move the lighting.
float lightness(vec3 n, vec3 v) {
  vec3 key = normalize(vec3(-0.4, -0.6, 1.0));
  vec3 fill = normalize(vec3(0.7, 0.3, 0.4));
  float diffuse = 0.62 * max(dot(n, key), 0.0) + 0.28 * max(dot(n, fill), 0.0);
  float ambient = 0.16 + 0.12 * (0.5 + 0.5 * n.z);
  return ambient + diffuse;
}

vec3 shade(vec3 base, vec3 n, vec3 v) {
  vec3 key = normalize(vec3(-0.4, -0.6, 1.0));
  vec3 h = normalize(key + v);
  float specular = 0.22 * pow(max(dot(n, h), 0.0), 48.0);
  return base * lightness(n, v) + vec3(specular);
}

// 1 on the integer level sets of f, 0 away from them, with an anti-aliased
// width of about widthPx screen pixels whatever the zoom.
float lineMask(float f, float widthPx) {
  float d = abs(fract(f + 0.5) - 0.5) / max(fwidth(f), 1e-6);
  return 1.0 - smoothstep(0.5 * widthPx, 0.5 * widthPx + 1.0, d);
}

// Angle of the reflected view ray around the stripe axis, in stripe periods.
// An environment of stripes parallel to the axis gives exactly this pattern.
float stripeCoordinate(vec3 n, vec3 v) {
  vec3 r = reflect(-v, n);
  vec3 a = normalize(uStripeAxis);
  vec3 b = normalize(abs(a.z) < 0.9 ? cross(a, vec3(0.0, 0.0, 1.0)) : cross(a, vec3(1.0, 0.0, 0.0)));
  vec3 c = cross(a, b);
  return atan(dot(r, c), dot(r, b)) / kTwoPi * uStripes;
}

vec3 curvatureColour(float value, float range, vec3 n, vec3 v) {
  if (isnan(value)) {
    return kUndefined;
  }
  float t = value / range;
  vec3 colour = diverging(t);
  // Faint isolines at quarter steps of the range make the gradient readable.
  colour *= 1.0 - 0.18 * lineMask(4.0 * t, 1.0);
  return colour * (0.55 + 0.45 * lightness(n, v));
}

void main() {
  vec3 n = normalize(vWorldNormal);
  if (!gl_FrontFacing) {
    n = -n;
  }
  vec3 v = normalize(cameraPosition - vWorldPos);
  vec3 colour;

  if (uMode == 1) {
    // Zebra: equal-width stripes, edges anti-aliased through the derivative.
    float f = stripeCoordinate(n, v);
    float s = clamp(0.5 + sin(kTwoPi * f) / (kTwoPi * max(fwidth(f), 1e-6)), 0.0, 1.0);
    colour = mix(vec3(0.06), vec3(0.94), s);
  } else if (uMode == 2) {
    float f = stripeCoordinate(n, v);
    colour = mix(shade(kPaint, n, v), vec3(0.04), lineMask(f, 1.6));
  } else if (uMode == 3) {
    colour = shade(kPaint, n, v);
    if (!isnan(vHighlight)) {
      colour = mix(colour, vec3(0.05, 0.08, 0.30), lineMask(vHighlight / uHighlightSpacing, 1.6));
    }
  } else if (uMode == 4) {
    colour = curvatureColour(vMean, uMeanRange, n, v);
  } else if (uMode == 5) {
    colour = curvatureColour(vGaussian, uGaussianRange, n, v);
  } else {
    colour = shade(kPaint, n, v);
  }
  gl_FragColor = vec4(colour, 1.0);
}
`;
