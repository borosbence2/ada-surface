#pragma once

#include "ada/geom/patch_evaluator.hpp"

#include <cstddef>

namespace ada {

/// Curvature over a whole patch, sampled on a uniform `grid x grid` (u, v)
/// grid. Mean and principal curvature signs follow the patch's own normal
/// (`unit_normal`), so they are comparable within a patch, not across patches
/// with unrelated orientations.
struct CurvatureSummary {
    double gaussian_min = 0.0;      ///< 1/mm^2
    double gaussian_max = 0.0;      ///< 1/mm^2
    double mean_min = 0.0;          ///< 1/mm
    double mean_max = 0.0;          ///< 1/mm
    double max_abs_principal = 0.0; ///< 1/mm; 1 / the tightest radius on the patch
    /// Share of defined samples with Gaussian curvature clearly below zero.
    double saddle_fraction = 0.0;
    /// Gaussian curvature takes both signs beyond `tol::kCurvatureSignNoise`
    /// squared (K is a product of two curvatures): the patch has both dome and
    /// saddle regions.
    bool gaussian_changes_sign = false;
    /// Mean curvature takes both signs beyond `tol::kCurvatureSignNoise`: the
    /// patch bends both ways somewhere, which on a Class-A panel usually means
    /// an unintended curvature reversal.
    bool mean_changes_sign = false;
    std::size_t samples = 0;
    std::size_t undefined_samples = 0; ///< No tangent plane (degenerate point).
};

/// Throws `std::invalid_argument` for `grid < 2`.
CurvatureSummary summarize_curvature(const PatchEvaluator& patch, std::size_t grid);

} // namespace ada
