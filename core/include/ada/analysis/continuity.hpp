#pragma once

#include "ada/geom/bezier_patch.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include <cstddef>

namespace ada {

/// Geometric continuity across an edge shared by two patches, sampled along
/// the first patch's edge. Correspondence is by closest point on the second
/// patch's boundary, so the two edges may run in opposite directions and need
/// not share a parameterisation.
///
/// G1 here means equal tangent planes *and* no fold: the second patch must
/// continue away from the first, not double back over it (a fold has parallel
/// normals, so the angle alone would miss it). G2 compares the normal
/// curvature in the cross-boundary direction on both sides. Given G1 along the
/// whole edge, that is the only component of the curvature tensor that can
/// still differ: the along-edge curvature comes from the shared boundary
/// curve, and the mixed term is fixed by how the shared normal turns along it.
struct EdgeContinuity {
    double max_gap = 0.0;                  ///< G0, mm.
    double max_normal_angle_deg = 0.0;     ///< G1, degrees.
    double max_curvature_deviation = 0.0;  ///< G2, relative (see `tol::kCurvatureFloor`).
    double max_curvature_difference = 0.0; ///< G2, absolute, 1/mm.
    bool fold = false;
    /// The two patches' normals point to opposite sides. Not a defect: the
    /// comparison above flips one side first. Reported because export and
    /// rendering care about consistent orientation.
    bool orientation_flipped = false;
    std::size_t samples = 0;
    std::size_t undefined_samples = 0; ///< No tangent plane on one side.
};

/// Throws `std::invalid_argument` for `samples < 2`.
EdgeContinuity measure_edge(const PatchEvaluator& a,
                            Edge edge_a,
                            const PatchEvaluator& b,
                            Edge edge_b,
                            std::size_t samples);

/// Continuity of a patch with its own mirror image across the vehicle centre
/// plane Y = 0, on an edge that should lie in that plane.
///
/// Only G0 and G1 are measured, because for a mirror pair G2 follows from G1.
/// G1 means the normal has no y-component along the whole edge. Differentiating
/// that along the edge makes the mixed term `II(t, y)` vanish, so the curvature
/// tensor is diagonal in the (edge, y) frame, and reflection maps it onto
/// itself. The first condition that adds anything at the plane is G3.
struct SymmetryContinuity {
    double max_plane_distance = 0.0;  ///< G0: max |y| along the edge, mm.
    double max_normal_tilt_deg = 0.0; ///< G1 (hence G2): normal's angle out of the plane.
    std::size_t samples = 0;
    std::size_t undefined_samples = 0;
};

/// Throws `std::invalid_argument` for `samples < 2`.
SymmetryContinuity measure_symmetry(const PatchEvaluator& patch, Edge edge, std::size_t samples);

} // namespace ada
