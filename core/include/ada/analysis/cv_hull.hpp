#pragma once

#include "ada/geom/patch_evaluator.hpp"

#include <cstddef>

namespace ada {

/// Shape of a patch's control net, row by row (along u for each j, and along v
/// for each i). Class-A modellers keep CV rows "harmonious": each row turns
/// gradually and in one direction. A row that turns back and forth puts a
/// ripple into the surface that the highlights show long before the curvature
/// plot does.
///
/// Bending is split into a component along the surface normal (at the
/// vertex's Greville point) and a sideways component within the surface, and
/// sign changes are counted for each, ignoring components too small to see
/// next to the row's largest bend (`tol::kHullRelativeNoise`). One sign change
/// in a row can be a deliberate S-shape; two or more in either component make
/// the row a zig-zag.
struct CvHullSummary {
    std::size_t rows = 0;        ///< Rows with at least three points.
    std::size_t zigzag_rows = 0; ///< Rows with two or more inflections.
    std::size_t max_row_inflections = 0;
    double max_turning_angle_deg = 0.0; ///< Sharpest turn between consecutive CV segments.
};

CvHullSummary analyze_cv_hull(const PatchEvaluator& patch);

} // namespace ada
