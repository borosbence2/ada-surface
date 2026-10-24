#pragma once

#include "ada/geom/bezier_curve.hpp"
#include "ada/geom/vec.hpp"

#include <vector>

namespace ada {

/// All degree-`degree` Bernstein polynomials at t: `C(n, i) t^i (1-t)^(n-i)`.
std::vector<double> bernstein_values(int degree, double t);

/// Chord-length parameters of a point sequence on [0, 1]. Throws
/// `std::invalid_argument` for fewer than two points or for two consecutive
/// points that coincide (the parameter would not advance).
std::vector<double> chord_length_parameters(const std::vector<Vec3>& points);

/// A Bézier curve of `degree` through `points`, the way a designer gives a
/// feature line: as points it passes through, not as control points.
///
/// Parameters are chord length. With at most `degree + 1` points the curve
/// interpolates them exactly (at degree `points - 1`, then elevated, which
/// keeps the same curve). With more it is the least-squares fit that still
/// interpolates the first and last point, so curves meeting at a shared
/// corner point meet exactly. Throws `std::invalid_argument` for fewer than
/// two points, a degree below 1, or coincident consecutive points.
BezierCurve curve_through_points(const std::vector<Vec3>& points, int degree);

} // namespace ada
