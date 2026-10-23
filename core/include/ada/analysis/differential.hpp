#pragma once

// Differential geometry at one point, from a patch's derivative table. The
// formulas themselves are nurbs2subd's `curvature_from_derivatives` (copied,
// see core/third_party/n2s/SOURCE.md); these are thin adapters plus the
// directional normal curvature the continuity checks need.

#include "ada/geom/bezier_patch.hpp"
#include "ada/geom/vec.hpp"

#include "n2s/nurbs/differential.hpp"

#include <optional>

namespace ada {

/// `(Su x Sv) / |Su x Sv|`; nullopt where the tangent plane does not exist.
std::optional<Vec3> unit_normal(const PatchDerivatives& d);

/// Mean, Gaussian and principal curvatures. The sign of the mean and
/// principal curvatures follows `unit_normal`; see `n2s::SurfaceCurvature`.
std::optional<n2s::SurfaceCurvature> curvature(const PatchDerivatives& d);

/// Normal curvature `II(w, w) / I(w, w)` in the tangent direction `w` closest
/// to `direction` (its projection onto the tangent plane). Signed relative to
/// `unit_normal`. nullopt where the tangent plane does not exist or
/// `direction` is perpendicular to it.
std::optional<double> normal_curvature(const PatchDerivatives& d, const Vec3& direction);

} // namespace ada
