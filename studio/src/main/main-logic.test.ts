import path from 'node:path';
import { describe, expect, it } from 'vitest';

import { decodeF32, decodeTessellation, decodeU32 } from './decode';
import { kernelCandidates } from './kernel-path';

const f32 = (values: number[]) => Buffer.from(new Float32Array(values).buffer).toString('base64');
const u32 = (values: number[]) => Buffer.from(new Uint32Array(values).buffer).toString('base64');

describe('payload decoding', () => {
  it('round-trips float32 and uint32 arrays, NaN included', () => {
    const floats = decodeF32(f32([1.5, -2.25, Number.NaN]));
    expect(floats[0]).toBe(1.5);
    expect(floats[1]).toBe(-2.25);
    expect(Number.isNaN(floats[2])).toBe(true);
    expect(Array.from(decodeU32(u32([0, 7, 4_000_000_000])))).toEqual([0, 7, 4_000_000_000]);
  });

  it('decodes a tessellation and checks attribute lengths', () => {
    const patch = {
      id: 'p',
      vertex_count: 2,
      positions: f32([0, 0, 0, 1, 0, 0]),
      normals: f32([0, 0, 1, 0, 0, 1]),
      uvs: f32([0, 0, 1, 0]),
      indices: u32([0, 1, 1]),
      gaussian: f32([0, 0]),
      mean: f32([0, 0]),
      highlight: f32([0, 1]),
    };
    const light = {
      origin: [0, 0, 0],
      normal: [0, 0, 1],
      direction: [1, 0, 0],
      across: [0, 1, 0],
      spacing: 10,
    };
    const t = decodeTessellation({ grid: 2, light, patches: [patch] });
    expect(t.patches[0]!.positions).toBeInstanceOf(Float32Array);
    expect(t.patches[0]!.indices).toBeInstanceOf(Uint32Array);
    expect(() =>
      decodeTessellation({ grid: 2, light, patches: [{ ...patch, mean: f32([0]) }] }),
    ).toThrow(/p.mean/);
  });
});

describe('kernel location', () => {
  const base = {
    packaged: false,
    resourcesPath: '/res',
    appPath: path.join('/repo', 'studio'),
    platform: 'linux' as const,
  };

  it('prefers an explicit override', () => {
    expect(kernelCandidates({ ...base, override: '/x/ada-mcp' })).toEqual(['/x/ada-mcp']);
  });

  it('uses the shipped resource when packaged', () => {
    expect(kernelCandidates({ ...base, packaged: true, platform: 'win32' })).toEqual([
      path.join('/res', 'ada-mcp.exe'),
    ]);
  });

  it('looks in the build tree during development, optimised builds first', () => {
    const repo = path.resolve('/repo');
    expect(kernelCandidates(base)).toEqual(
      ['release', 'dev', 'ci'].map((preset) => path.join(repo, 'build', preset, 'bin', 'ada-mcp')),
    );
  });
});
