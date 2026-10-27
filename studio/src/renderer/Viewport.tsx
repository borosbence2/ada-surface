import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

import type { SceneBundle, Selection } from '../shared/types';
import { deviationRange, kDefaultDeviationRangeMm, type DisplayOptions } from './display';
import { framingDistance } from './framing';
import { nearestEdge } from './picking';
import { edgeStatuses } from './status';
import { buildCombs, buildScene, recolorTarget, type SceneObjects } from './viewport/sceneObjects';
import { kModeIndex } from './viewport/shaders';

// Scene conventions match the kernel: millimetres, Z up, Y=0 is the vehicle
// centre plane.
const kGridSizeMm = 5000;
const kGridDivisions = 50; // 100 mm cells
const kFovYDeg = 35;
const kViewDirection = new THREE.Vector3(1, -1, 0.6).normalize(); // front-left, above
const kSelectionColor = 0x2ee6ff;
/** A press that moves less than this (pixels) before release is a click, not an orbit. */
const kClickSlopPx = 4;

interface ViewportProps {
  bundle: SceneBundle | null;
  options: DisplayOptions;
  selection: Selection | null;
  onPick: (selection: Selection | null) => void;
  /** Called once a new bundle's first frame has been drawn. */
  onRendered?: () => void;
}

/** Everything that lives as long as the viewport, created once on mount. */
interface Stage {
  renderer: THREE.WebGLRenderer;
  scene: THREE.Scene;
  camera: THREE.PerspectiveCamera;
  controls: OrbitControls;
  grid: THREE.GridHelper;
  objects: SceneObjects | null;
  selection: THREE.Group;
  framedPath: string | null;
  combScale: number;
  /** The range the target points are coloured for. */
  deviationRange: number;
  render(): void;
}

function frame(stage: Stage, sphere: THREE.Sphere): void {
  const distance = framingDistance(sphere.radius, kFovYDeg, stage.camera.aspect);
  stage.controls.target.copy(sphere.center);
  stage.camera.position.copy(sphere.center).addScaledVector(kViewDirection, distance);
  stage.camera.near = Math.max(0.1, distance / 1000);
  stage.camera.far = distance * 100;
  stage.camera.updateProjectionMatrix();
  stage.controls.update();
}

function applyOptions(stage: Stage, options: DisplayOptions): void {
  const objects = stage.objects;
  if (!objects) {
    return;
  }
  const u = objects.material.uniforms;
  u.uMode!.value = kModeIndex[options.mode];
  u.uStripes!.value = options.stripes;
  u.uMeanRange!.value = 1 / options.meanRadiusMm;
  u.uGaussianRange!.value = 1 / (options.gaussianRadiusMm * options.gaussianRadiusMm);
  objects.hull.visible = options.showHull;
  objects.edges.visible = options.showEdges;
  objects.combs.visible = options.showCombs;
  if (objects.target) {
    objects.target.visible = options.showTarget;
  }
}

export function Viewport({ bundle, options, selection, onPick, onRendered }: ViewportProps) {
  const hostRef = useRef<HTMLDivElement>(null);
  const stageRef = useRef<Stage | null>(null);
  const bundleRef = useRef<SceneBundle | null>(null);
  const onPickRef = useRef(onPick);
  onPickRef.current = onPick;
  const onRenderedRef = useRef(onRendered);
  onRenderedRef.current = onRendered;

  // Mount: renderer, camera, controls, picking. Rendering is on demand;
  // nothing animates, so a frame is drawn only when something changes.
  useEffect(() => {
    const host = hostRef.current;
    if (!host) {
      return;
    }
    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(window.devicePixelRatio);
    host.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x2b2d31);
    const grid = new THREE.GridHelper(kGridSizeMm, kGridDivisions, 0x5a5d63, 0x3a3c41);
    grid.rotation.x = Math.PI / 2; // GridHelper lies in XZ; the ground here is XY.
    scene.add(grid);
    const selectionGroup = new THREE.Group();
    scene.add(selectionGroup);

    const camera = new THREE.PerspectiveCamera(kFovYDeg, 1, 1, 100_000);
    camera.up.set(0, 0, 1);
    const controls = new OrbitControls(camera, renderer.domElement);

    let pending = false;
    const stage: Stage = {
      renderer,
      scene,
      camera,
      controls,
      grid,
      objects: null,
      selection: selectionGroup,
      framedPath: null,
      combScale: 1,
      deviationRange: kDefaultDeviationRangeMm,
      render() {
        if (pending) return;
        pending = true;
        requestAnimationFrame(() => {
          pending = false;
          renderer.render(scene, camera);
        });
      },
    };
    stageRef.current = stage;
    frame(stage, new THREE.Sphere(new THREE.Vector3(), (kGridSizeMm / 2) * Math.SQRT2));

    const resize = () => {
      const { clientWidth: width, clientHeight: height } = host;
      if (width === 0 || height === 0) return;
      renderer.setSize(width, height);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      stage.render();
    };
    const observer = new ResizeObserver(resize);
    observer.observe(host);
    controls.addEventListener('change', stage.render);

    // Picking: a click (not a drag) on a surface selects the patch, or the
    // edge if the hit is close to one.
    const raycaster = new THREE.Raycaster();
    let down: { x: number; y: number } | null = null;
    const onDown = (e: PointerEvent) => {
      down = { x: e.clientX, y: e.clientY };
    };
    const onUp = (e: PointerEvent) => {
      if (
        !down ||
        Math.hypot(e.clientX - down.x, e.clientY - down.y) > kClickSlopPx ||
        e.button !== 0
      ) {
        down = null;
        return;
      }
      down = null;
      const objects = stage.objects;
      const current = bundleRef.current;
      if (!objects || !current) return;
      const rect = renderer.domElement.getBoundingClientRect();
      const ndc = new THREE.Vector2(
        ((e.clientX - rect.left) / rect.width) * 2 - 1,
        -((e.clientY - rect.top) / rect.height) * 2 + 1,
      );
      raycaster.setFromCamera(ndc, camera);
      const hit = raycaster.intersectObjects(objects.surfaces, false)[0];
      if (!hit || !hit.uv) {
        onPickRef.current(null);
        return;
      }
      onPickRef.current({
        scene: current.path,
        patch: hit.object.userData.patchId as string,
        edge: nearestEdge(hit.uv.x, hit.uv.y),
        uv: [hit.uv.x, hit.uv.y],
        point: [hit.point.x, hit.point.y, hit.point.z],
      });
    };
    renderer.domElement.addEventListener('pointerdown', onDown);
    renderer.domElement.addEventListener('pointerup', onUp);

    return () => {
      observer.disconnect();
      renderer.domElement.removeEventListener('pointerdown', onDown);
      renderer.domElement.removeEventListener('pointerup', onUp);
      controls.removeEventListener('change', stage.render);
      controls.dispose();
      stage.objects?.dispose();
      grid.dispose();
      renderer.dispose();
      host.removeChild(renderer.domElement);
      stageRef.current = null;
    };
  }, []);

  // A new bundle replaces the scene objects. The camera is framed only when a
  // different scene is opened, so hot reloads of the same file keep the view.
  useEffect(() => {
    const stage = stageRef.current;
    bundleRef.current = bundle;
    if (!stage) return;
    if (stage.objects) {
      stage.scene.remove(stage.objects.root);
      stage.objects.dispose();
      stage.objects = null;
    }
    if (bundle) {
      // Coloured for the budget's range; the options effect below recolours
      // the points if the user has set another.
      const range = bundle.deviations?.budgetMm ?? kDefaultDeviationRangeMm;
      const objects = buildScene(bundle, edgeStatuses(bundle.report), range);
      stage.objects = objects;
      stage.combScale = 1;
      stage.deviationRange = range;
      stage.scene.add(objects.root);
      if (stage.framedPath !== bundle.path) {
        frame(stage, objects.bounds);
        stage.framedPath = bundle.path;
      }
    }
    // The options effect below runs after this one and applies the current
    // display options to the new objects.
    stage.render();
    if (bundle) {
      // render() draws on the next animation frame; one frame after that the
      // new scene is on screen.
      requestAnimationFrame(() => requestAnimationFrame(() => onRenderedRef.current?.()));
    }
  }, [bundle]);

  useEffect(() => {
    const stage = stageRef.current;
    if (!stage) return;
    const objects = stage.objects;
    if (objects && bundle && options.combScale !== stage.combScale) {
      objects.root.remove(objects.combs);
      objects.combs.traverse((o) => {
        if (o instanceof THREE.Line) {
          o.geometry.dispose();
          (o.material as THREE.Material).dispose();
        }
      });
      objects.combs = buildCombs(bundle.combs, objects.bounds, options.combScale);
      objects.root.add(objects.combs);
      stage.combScale = options.combScale;
    }
    if (objects?.target && bundle?.deviations) {
      const range = deviationRange(options, bundle.deviations.budgetMm);
      if (range !== stage.deviationRange) {
        recolorTarget(objects.target, bundle.deviations, range);
        stage.deviationRange = range;
      }
    }
    applyOptions(stage, options);
    stage.render();
  }, [options, bundle]);

  // Selection: the picked edge in cyan, or the picked point as a marker.
  useEffect(() => {
    const stage = stageRef.current;
    if (!stage) return;
    // Copy first: removing children while iterating the live array skips some.
    for (const child of stage.selection.children.slice()) {
      stage.selection.remove(child);
      if (child instanceof THREE.Line || child instanceof THREE.Points) {
        child.geometry.dispose();
        (child.material as THREE.Material).dispose();
      }
    }
    if (selection && stage.objects) {
      const points = selection.edge
        ? stage.objects.edgePoints.get(`${selection.patch}.${selection.edge}`)
        : undefined;
      if (points) {
        const line = new THREE.Line(
          new THREE.BufferGeometry().setFromPoints(points),
          new THREE.LineBasicMaterial({ color: kSelectionColor, depthTest: false }),
        );
        line.renderOrder = 1;
        stage.selection.add(line);
      } else {
        const marker = new THREE.Points(
          new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(...selection.point)]),
          new THREE.PointsMaterial({
            color: kSelectionColor,
            size: 10,
            sizeAttenuation: false,
            depthTest: false,
          }),
        );
        marker.renderOrder = 1;
        stage.selection.add(marker);
      }
    }
    stage.render();
  }, [selection, bundle]);

  return <div ref={hostRef} className="viewport" />;
}
