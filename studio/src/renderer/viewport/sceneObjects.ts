import * as THREE from 'three';

import type { EdgeName, FitDeviations, PatchCombs, SceneBundle } from '../../shared/types';
import { deviationColors } from '../colormap';
import { kStatusColors, edgeKey, type Status } from '../status';
import { kSurfaceFragment, kSurfaceVertex } from './shaders';

const kHullColor = 0xe0a040;
const kCombColor = 0x7ec8e3;
const kFreeEdgeColor = 0x8a8e95;

/** The longest comb is this share of the scene's bounding-sphere diameter at scale 1. */
const kCombLengthShare = 0.08;

export interface SceneObjects {
  root: THREE.Group;
  surfaces: THREE.Mesh[];
  material: THREE.ShaderMaterial;
  hull: THREE.Group;
  combs: THREE.Group;
  edges: THREE.Group;
  /** The fit target's points, coloured by deviation; null without a fit. */
  target: THREE.Points | null;
  bounds: THREE.Sphere;
  /** Edge polylines by "patch.Edge", for highlighting a selection. */
  edgePoints: Map<string, THREE.Vector3[]>;
  dispose(): void;
}

function surfaceMaterial(spacing: number): THREE.ShaderMaterial {
  return new THREE.ShaderMaterial({
    vertexShader: kSurfaceVertex,
    fragmentShader: kSurfaceFragment,
    side: THREE.DoubleSide,
    // Push the surface back a little so overlay lines drawn on it win the
    // depth test instead of flickering.
    polygonOffset: true,
    polygonOffsetFactor: 1,
    polygonOffsetUnits: 1,
    uniforms: {
      uMode: { value: 0 },
      uStripes: { value: 24 },
      uStripeAxis: { value: new THREE.Vector3(1, 0, 0) },
      uMeanRange: { value: 1 / 2000 },
      uGaussianRange: { value: 1 / (2000 * 2000) },
      uHighlightSpacing: { value: spacing },
    },
  });
}

function lines(points: number[], color: number): THREE.LineSegments {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(points, 3));
  return new THREE.LineSegments(geometry, new THREE.LineBasicMaterial({ color }));
}

function polyline(points: THREE.Vector3[], color: number): THREE.Line {
  const geometry = new THREE.BufferGeometry().setFromPoints(points);
  return new THREE.Line(geometry, new THREE.LineBasicMaterial({ color }));
}

function buildHull(bundle: SceneBundle): THREE.Group {
  const group = new THREE.Group();
  for (const hull of bundle.hulls) {
    const [du, dv] = hull.degree;
    const at = (i: number, j: number) => hull.control_points[i * (dv + 1) + j]!;
    const segments: number[] = [];
    for (let i = 0; i <= du; i++) {
      for (let j = 0; j <= dv; j++) {
        if (i < du) segments.push(...at(i, j), ...at(i + 1, j));
        if (j < dv) segments.push(...at(i, j), ...at(i, j + 1));
      }
    }
    group.add(lines(segments, kHullColor));
    const pointGeometry = new THREE.BufferGeometry();
    pointGeometry.setAttribute(
      'position',
      new THREE.Float32BufferAttribute(hull.control_points.flat(), 3),
    );
    group.add(
      new THREE.Points(
        pointGeometry,
        new THREE.PointsMaterial({ color: kHullColor, size: 6, sizeAttenuation: false }),
      ),
    );
  }
  return group;
}

/** Curvature combs: a spike per sample, length curvature x scale, plus the envelope through the tips. */
export function buildCombs(combs: PatchCombs[], bounds: THREE.Sphere, scale: number): THREE.Group {
  let maxCurvature = 0;
  for (const patch of combs) {
    for (const edge of patch.edges) {
      for (const k of edge.curvature) maxCurvature = Math.max(maxCurvature, k);
    }
  }
  const group = new THREE.Group();
  if (maxCurvature <= 0) {
    return group;
  }
  const length = ((kCombLengthShare * 2 * bounds.radius) / maxCurvature) * scale;
  const spikes: number[] = [];
  for (const patch of combs) {
    for (const edge of patch.edges) {
      const tips: THREE.Vector3[] = [];
      edge.curvature.forEach((k, s) => {
        const p = new THREE.Vector3().fromArray(edge.points, 3 * s);
        const d = new THREE.Vector3().fromArray(edge.directions, 3 * s);
        const tip = p.clone().addScaledVector(d, k * length);
        spikes.push(p.x, p.y, p.z, tip.x, tip.y, tip.z);
        tips.push(tip);
      });
      group.add(polyline(tips, kCombColor));
    }
  }
  group.add(lines(spikes, kCombColor));
  return group;
}

function buildEdges(
  bundle: SceneBundle,
  statuses: Map<string, Status>,
  edgePoints: Map<string, THREE.Vector3[]>,
): THREE.Group {
  const group = new THREE.Group();
  for (const patch of bundle.combs) {
    for (const edge of patch.edges) {
      const key = edgeKey(patch.id, edge.edge as EdgeName);
      const points: THREE.Vector3[] = [];
      for (let s = 0; s < edge.curvature.length; s++) {
        points.push(new THREE.Vector3().fromArray(edge.points, 3 * s));
      }
      edgePoints.set(key, points);
      const status = statuses.get(key);
      const color = status ? new THREE.Color(kStatusColors[status]).getHex() : kFreeEdgeColor;
      group.add(polyline(points, color));
    }
  }
  return group;
}

/** Target points on screen, in pixels: visible over the surface without hiding it. */
const kTargetPointPx = 3;

/** The fit target's points as a point cloud, coloured for `range` mm. */
export function buildTarget(deviations: FitDeviations, range: number): THREE.Points {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(deviations.positions, 3));
  geometry.setAttribute(
    'color',
    new THREE.BufferAttribute(deviationColors(deviations.deviation, range), 3),
  );
  const material = new THREE.PointsMaterial({
    size: kTargetPointPx,
    sizeAttenuation: false,
    vertexColors: true,
  });
  return new THREE.Points(geometry, material);
}

/** Recolours target points for a new range, in place. */
export function recolorTarget(
  target: THREE.Points,
  deviations: FitDeviations,
  range: number,
): void {
  const color = target.geometry.getAttribute('color') as THREE.BufferAttribute;
  (color.array as Float32Array).set(deviationColors(deviations.deviation, range));
  color.needsUpdate = true;
}

export function buildScene(
  bundle: SceneBundle,
  statuses: Map<string, Status>,
  deviationRange: number,
): SceneObjects {
  const root = new THREE.Group();
  const material = surfaceMaterial(bundle.tessellation.light.spacing);
  const surfaces: THREE.Mesh[] = [];
  const box = new THREE.Box3();

  for (const patch of bundle.tessellation.patches) {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(patch.positions, 3));
    geometry.setAttribute('normal', new THREE.BufferAttribute(patch.normals, 3));
    geometry.setAttribute('uv', new THREE.BufferAttribute(patch.uvs, 2));
    geometry.setAttribute('aGaussian', new THREE.BufferAttribute(patch.gaussian, 1));
    geometry.setAttribute('aMean', new THREE.BufferAttribute(patch.mean, 1));
    geometry.setAttribute('aHighlight', new THREE.BufferAttribute(patch.highlight, 1));
    geometry.setIndex(new THREE.BufferAttribute(patch.indices, 1));
    geometry.computeBoundingBox();
    geometry.computeBoundingSphere();
    box.union(geometry.boundingBox!);

    const mesh = new THREE.Mesh(geometry, material);
    mesh.userData = { patchId: patch.id };
    surfaces.push(mesh);
    root.add(mesh);
  }

  const bounds = box.isEmpty()
    ? new THREE.Sphere(new THREE.Vector3(), 1)
    : box.getBoundingSphere(new THREE.Sphere());
  const edgePoints = new Map<string, THREE.Vector3[]>();
  const hull = buildHull(bundle);
  const combs = buildCombs(bundle.combs, bounds, 1);
  const edges = buildEdges(bundle, statuses, edgePoints);
  root.add(hull, combs, edges);
  const target = bundle.deviations ? buildTarget(bundle.deviations, deviationRange) : null;
  if (target) {
    root.add(target);
  }

  return {
    root,
    surfaces,
    material,
    hull,
    combs,
    edges,
    target,
    bounds,
    edgePoints,
    dispose() {
      root.traverse((object) => {
        if (
          object instanceof THREE.Mesh ||
          object instanceof THREE.Line ||
          object instanceof THREE.Points
        ) {
          object.geometry.dispose();
          const materials = Array.isArray(object.material) ? object.material : [object.material];
          for (const m of materials) m.dispose();
        }
      });
    },
  };
}
