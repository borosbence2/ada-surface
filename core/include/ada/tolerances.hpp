#pragma once

namespace ada::tol {

/// Named tolerances. CLAUDE.md forbids magic numbers in geometry code: every
/// comparison against a small quantity names one of these. Model space is in
/// millimetres, so curvatures are in 1/mm.
///
/// These are numerical tolerances: how small "zero" is for an algorithm. The
/// Class-A quality budgets (how much deviation a surface may have) are a
/// separate thing and live in `ada/analysis/budgets.hpp`.

/// Below this curvature magnitude (1/mm; a radius of 100 m) the relative G2
/// deviation stops dividing by the curvature itself. Without a floor, two
/// nearly flat sides with curvatures 1e-9 and 2e-9 would report a 50 %
/// deviation for a difference no instrument could see.
inline constexpr double kCurvatureFloor = 1e-5;

/// Curvature magnitudes below this (1/mm; a radius of 10 km) count as zero
/// when looking for sign changes of Gaussian or mean curvature, so that
/// rounding noise on a flat region does not register as an inflection.
inline constexpr double kCurvatureSignNoise = 1e-7;

/// Closest point on a curve: Newton stops once a step moves the parameter by
/// less than this.
inline constexpr double kClosestPointParameter = 1e-14;

/// The light plane is parallel to a surface normal line, so the highlight
/// parameter is undefined, when `|n_light . n_surface|` falls below this.
inline constexpr double kHighlightParallel = 1e-9;

/// Contour crossings on a grid edge are refined by bisection on the exact
/// field until the bracket is shorter than this, in (u, v) units.
inline constexpr double kContourCrossing = 1e-13;

/// Consecutive contour points closer than this in (u, v) are merged before
/// curvature is estimated. They occur where a contour passes within rounding
/// distance of a grid node, and a segment that short makes the turning angle
/// at its ends meaningless.
inline constexpr double kContourMinSegment = 1e-9;

/// Highlight-line curvature magnitudes below this (1/mm; a radius of 1 km) do
/// not count as a bend in either direction when inflections are counted.
inline constexpr double kHighlightCurvatureNoise = 1e-6;

/// Control-polygon turning angles below this (radians) do not count as a turn
/// in either direction when inflections of a CV row are counted.
inline constexpr double kHullTurnNoise = 1e-4;

} // namespace ada::tol
