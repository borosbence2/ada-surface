import path from 'node:path';
import { describe, expect, it } from 'vitest';

import { resolveViewMode } from '../shared/views';
import { decodeDeviations, decodeF32, decodeTessellation, decodeU32 } from './decode';
import { kernelCandidates, pickKernel } from './kernel-path';

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

  it('runs the most recently built kernel, not a fixed preset', () => {
    const times: Record<string, number> = { release: 100, dev: 200 };
    expect(pickKernel(['release', 'dev', 'ci'], (f) => times[f] ?? null)).toBe('dev');
    expect(pickKernel(['ci'], () => null)).toBeNull();
  });

  it('looks in every preset of the build tree during development', () => {
    const repo = path.resolve('/repo');
    expect(kernelCandidates(base)).toEqual(
      ['release', 'dev', 'ci'].map((preset) => path.join(repo, 'build', preset, 'bin', 'ada-mcp')),
    );
  });
});

describe('--view names', () => {
  it('accepts a mode id or its toolbar label, in any case', () => {
    expect(resolveViewMode('highlight')).toBe('highlight');
    expect(resolveViewMode('Highlights')).toBe('highlight');
    expect(resolveViewMode('mean k')).toBe('mean');
    expect(resolveViewMode(' ZEBRA ')).toBe('zebra');
  });

  it('rejects anything else instead of falling back to a default', () => {
    expect(resolveViewMode('highlites')).toBeNull();
    expect(resolveViewMode('')).toBeNull();
  });
});

describe('fit deviations', () => {
  it('decode to typed arrays, or null without a fit', () => {
    expect(decodeDeviations({ fit: null })).toBeNull();
    const d = decodeDeviations({
      fit: {
        target: 'scan',
        total: 4,
        count: 2,
        positions: f32([0, 0, 0, 1, 2, 3]),
        deviation: f32([0.25, Number.NaN]),
        budget_mm: 0.5,
        positive_towards: '+z',
      },
    });
    expect(d?.target).toBe('scan');
    expect(d?.positions.length).toBe(6);
    expect(d?.deviation[0]).toBe(0.25);
    expect(Number.isNaN(d?.deviation[1])).toBe(true);
    expect(d?.budgetMm).toBe(0.5);
    expect(d?.positiveTowards).toBe('+z');
  });

  it('refuse a payload whose lengths disagree', () => {
    expect(() =>
      decodeDeviations({
        fit: {
          target: 's',
          total: 3,
          count: 3,
          positions: f32([0, 0, 0]),
          deviation: f32([0]),
          budget_mm: null,
        },
      }),
    ).toThrow();
  });
});
