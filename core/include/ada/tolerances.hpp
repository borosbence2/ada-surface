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

/// Highlight levels sit this fraction of the line spacing off k * spacing. On a
/// symmetric part a level falls exactly on a grid line of the sampled field
/// (the crown of a roof is level 0), where every node is zero up to rounding
/// and marching squares would classify them by noise, zig-zagging the line.
/// Far above rounding, far below anything visible (7e-5 mm at 70 mm spacing).
inline constexpr double kHighlightLevelOffset = 1e-6;

/// Highlight-line curvature magnitudes below this (1/mm; a radius of 1 km) do
/// not count as a bend in either direction when inflections are counted.
inline constexpr double kHighlightCurvatureNoise = 1e-6;

/// Control-polygon turning angles below this (radians) do not count as a turn
/// in either direction when inflections of a CV row are counted.
inline constexpr double kHullTurnNoise = 1e-4;

/// Nor does a bending component smaller than this share of the row's largest
/// bend. A row that bends strongly one way leaks a few percent of that bend
/// into the other component wherever the reference normal tilts out of the
/// row's plane; sign flips in that leak are not a zig-zag anyone could see.
inline constexpr double kHullRelativeNoise = 0.05;

/// Constraint rows are normalised to unit length; a QR pivot below this marks
/// a row as dependent on the others (a redundant constraint, as at shared
/// corners), not a new condition.
inline constexpr double kConstraintRank = 1e-10;

/// The constraints are contradictory when no point satisfies them to within
/// this fraction of the problem's coordinate scale (1e-9 of a 1 m part is a
/// micrometre's thousandth).
inline constexpr double kConstraintConsistency = 1e-9;

/// A pull towards the current control points this weak (relative to the
/// normalised fairness energy) only decides directions that neither the
/// energy nor the constraints pin down; it keeps every solve well-posed
/// without visibly changing a determined result.
inline constexpr double kSolverTieBreak = 1e-9;

/// Boundary curves of a from_boundaries patch meet at a corner when their end
/// points are closer than this fraction of the model size (1e-6 of a 1 m part
/// is a micrometre). Curves through a shared corner point meet exactly; this
/// only absorbs rounding in imported data.
inline constexpr double kCurveCornerGap = 1e-6;

/// Weight (relative to the normalised fairness energy) with which a boundary
/// curve's control point is followed where it cannot be held exactly. Large
/// enough that the curve moves only as far as the constraints force it to.
inline constexpr double kCurveTargetWeight = 1e4;

} // namespace ada::tol
