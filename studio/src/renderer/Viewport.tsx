import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

import { framingDistance } from './framing';

// Scene conventions match the kernel: millimetres, Z up, Y=0 is the vehicle
// centre plane.
const kGridSizeMm = 5000;
const kGridDivisions = 50; // 100 mm cells
const kFovYDeg = 35;
const kViewDirection = new THREE.Vector3(1, -1, 0.6).normalize(); // front-left, above

export function Viewport() {
  const hostRef = useRef<HTMLDivElement>(null);

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

    const axes = new THREE.AxesHelper(500);
    scene.add(axes);

    const camera = new THREE.PerspectiveCamera(kFovYDeg, 1, 1, 100_000);
    camera.up.set(0, 0, 1);

    const controls = new OrbitControls(camera, renderer.domElement);

    const render = () => renderer.render(scene, camera);

    const frameGrid = () => {
      const radius = (kGridSizeMm / 2) * Math.SQRT2;
      const distance = framingDistance(radius, kFovYDeg, camera.aspect);
      controls.target.set(0, 0, 0);
      camera.position.copy(kViewDirection).multiplyScalar(distance);
      controls.update();
    };

    // Rendering is on demand: nothing in the scene animates, so a frame is only
    // drawn when the camera or the window changes.
    let framed = false;
    const resize = () => {
      const { clientWidth: width, clientHeight: height } = host;
      if (width === 0 || height === 0) {
        return;
      }
      renderer.setSize(width, height);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      if (!framed) {
        frameGrid();
        framed = true;
      }
      render();
    };

    const observer = new ResizeObserver(resize);
    observer.observe(host);
    controls.addEventListener('change', render);

    return () => {
      observer.disconnect();
      controls.removeEventListener('change', render);
      controls.dispose();
      grid.dispose();
      axes.dispose();
      renderer.dispose();
      host.removeChild(renderer.domElement);
    };
  }, []);

  return <div ref={hostRef} className="viewport" />;
}
