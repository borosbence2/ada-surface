#pragma once

// Control-polygon operations shared by BezierCurve and BezierPatch. A patch
// applies them to each row of its net (and to each column via a transpose).
// Callers validate their arguments; these functions assume a non-empty polygon
// and, for splitting, 0 < t < 1.

#include "ada/geom/vec.hpp"

#include <span>
#include <utility>
#include <vector>

namespace ada::detail {

/// Point at `t` by de Casteljau's algorithm. Every step is a convex
/// combination for t in [0, 1], which is what makes it stable.
Vec3 de_casteljau(std::span<const Vec3> points, double t);

/// Control polygon of the first hodograph, `n (P_{i+1} - P_i)`. A degree-0
/// polygon differentiates to a single zero point, so repeated application
/// stays well defined past the degree.
std::vector<Vec3> difference(std::span<const Vec3> points);

/// The same polynomial one degree higher:
/// `Q_i = i/(n+1) P_{i-1} + (1 - i/(n+1)) P_i`.
std::vector<Vec3> elevate(std::span<const Vec3> points);

/// Polygons of the pieces on [0, t] and [t, 1], read off the two sides of the
/// de Casteljau triangle.
std::pair<std::vector<Vec3>, std::vector<Vec3>> split(std::span<const Vec3> points, double t);

} // namespace ada::detail
