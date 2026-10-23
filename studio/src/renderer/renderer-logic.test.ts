import { describe, expect, it } from 'vitest';

import type { Check, QualityReportJson } from '../shared/types';
import { diverging, divergingCss, divergingGlsl, kDivergingStops } from './colormap';
import { nearestEdge } from './picking';
import { budgetUse, edgeStatuses, parseEdgeRef, statusOf } from './status';

const check = (value: number, budget: number, pass = value <= budget): Check => ({
  name: 'c',
  value,
  budget,
  pass,
});

describe('diverging colour map', () => {
  it('hits every stop exactly and clamps outside [-1, 1]', () => {
    for (const [t, rgb] of kDivergingStops) {
      expect(diverging(t)).toEqual(rgb);
    }
    expect(diverging(-7)).toEqual(kDivergingStops[0]![1]);
    expect(diverging(7)).toEqual(kDivergingStops[kDivergingStops.length - 1]![1]);
  });

  it('is near white at zero, blue below and red above', () => {
    const [r0, g0, b0] = diverging(0);
    expect(Math.min(r0, g0, b0)).toBeGreaterThan(0.95);
    const [rn, , bn] = diverging(-0.8);
    const [rp, , bp] = diverging(0.8);
    expect(bn).toBeGreaterThan(rn);
    expect(rp).toBeGreaterThan(bp);
  });

  it('emits GLSL and CSS from the same stops', () => {
    const glsl = divergingGlsl();
    expect(glsl).toContain('vec3 diverging(float t)');
    expect(glsl.match(/mix\(/g)).toHaveLength(kDivergingStops.length - 1);
    expect(divergingCss().match(/rgb\(/g)).toHaveLength(kDivergingStops.length);
  });
});

describe('check status', () => {
  it('uses green below half the budget, amber up to it, red past it', () => {
    expect(statusOf([check(0.2, 1)])).toBe('pass');
    expect(statusOf([check(0.7, 1)])).toBe('warn');
    expect(statusOf([check(1.5, 1)])).toBe('fail');
    expect(statusOf([check(0.2, 1), check(1.5, 1)])).toBe('fail');
  });

  it('treats zero-budget flags as all or nothing', () => {
    expect(budgetUse(check(0, 0))).toBe(0);
    expect(budgetUse(check(1, 0, false))).toBe(Infinity);
    expect(statusOf([check(1, 0, false)])).toBe('fail');
  });

  it('parses edge references whose patch ids contain dots', () => {
    expect(parseEdgeRef('hood.left.VMax')).toEqual({ patch: 'hood.left', edge: 'VMax' });
  });

  it('gives each linked edge the worst status of its links', () => {
    const report: QualityReportJson = {
      pass: false,
      patches: [],
      edges: [
        {
          a: 'front.VMax',
          b: 'rear.VMin',
          continuity: 'G2',
          g0_gap_mm: 0,
          g1_angle_deg: 0,
          g2_relative: 0.2,
          fold: false,
          checks: [check(0.2, 0.01)],
          pass: false,
        },
      ],
      symmetry: [
        {
          edge: 'front.UMin',
          continuity: 'G1',
          g0_plane_distance_mm: 0,
          g1_normal_tilt_deg: 0,
          checks: [check(0, 0.01)],
          pass: true,
        },
        {
          edge: 'rear.VMin',
          continuity: 'G1',
          g0_plane_distance_mm: 0,
          g1_normal_tilt_deg: 0,
          checks: [check(0, 0.01)],
          pass: true,
        },
      ],
    };
    const statuses = edgeStatuses(report);
    expect(statuses.get('front.VMax')).toBe('fail');
    expect(statuses.get('rear.VMin')).toBe('fail'); // the passing symmetry link does not hide the failure
    expect(statuses.get('front.UMin')).toBe('pass');
    expect(statuses.has('front.UMax')).toBe(false); // free boundary
  });
});

describe('edge picking', () => {
  it('picks the nearest side within the threshold', () => {
    expect(nearestEdge(0.01, 0.5)).toBe('UMin');
    expect(nearestEdge(0.98, 0.5)).toBe('UMax');
    expect(nearestEdge(0.5, 0.02)).toBe('VMin');
    expect(nearestEdge(0.5, 0.99)).toBe('VMax');
  });

  it('picks nothing in the interior', () => {
    expect(nearestEdge(0.5, 0.5)).toBeNull();
    expect(nearestEdge(0.2, 0.8)).toBeNull();
  });
});
