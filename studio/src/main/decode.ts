import type { PatchMesh, Tessellation, LightJson } from '../shared/types';

// ada-mcp sends bulk arrays as base64 of little-endian float32/uint32. Decoding
// happens in the main process (Buffer is not available in the sandboxed
// renderer); the typed arrays then cross IPC by structured clone.

function bytes(base64: string): ArrayBuffer {
  const buffer = Buffer.from(base64, 'base64');
  // Node pools small Buffers, so the bytes may start at an offset that is not
  // 4-byte aligned; slicing copies them into a fresh, aligned ArrayBuffer.
  return buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
}

export function decodeF32(base64: string): Float32Array {
  return new Float32Array(bytes(base64));
}

export function decodeU32(base64: string): Uint32Array {
  return new Uint32Array(bytes(base64));
}

interface RawPatch {
  id: string;
  vertex_count: number;
  positions: string;
  normals: string;
  uvs: string;
  indices: string;
  gaussian: string;
  mean: string;
  highlight: string;
}

interface RawTessellation {
  grid: number;
  light: LightJson;
  patches: RawPatch[];
}

function expectLength(array: ArrayLike<number>, length: number, what: string): void {
  if (array.length !== length) {
    throw new Error(`tessellation ${what}: expected ${length} values, got ${array.length}`);
  }
}

export function decodeTessellation(raw: unknown): Tessellation {
  const t = raw as RawTessellation;
  const patches = t.patches.map((p): PatchMesh => {
    const mesh: PatchMesh = {
      id: p.id,
      positions: decodeF32(p.positions),
      normals: decodeF32(p.normals),
      uvs: decodeF32(p.uvs),
      indices: decodeU32(p.indices),
      gaussian: decodeF32(p.gaussian),
      mean: decodeF32(p.mean),
      highlight: decodeF32(p.highlight),
    };
    const n = p.vertex_count;
    expectLength(mesh.positions, 3 * n, `${p.id}.positions`);
    expectLength(mesh.normals, 3 * n, `${p.id}.normals`);
    expectLength(mesh.uvs, 2 * n, `${p.id}.uvs`);
    expectLength(mesh.gaussian, n, `${p.id}.gaussian`);
    expectLength(mesh.mean, n, `${p.id}.mean`);
    expectLength(mesh.highlight, n, `${p.id}.highlight`);
    return mesh;
  });
  return { grid: t.grid, light: t.light, patches };
}
