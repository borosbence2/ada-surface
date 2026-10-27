#pragma once

// Fitting patches to target points: which patch each point belongs to and
// where its footpoint is, and how far the surface is from the points.

#include "ada/geom/bezier_patch.hpp"

#include <cstddef>
#include <vector>

namespace ada {

/// Where a target point lands on the patches.
struct FitAssignment {
    std::size_t patch = 0; ///< index into the patches it was assigned among
    double u = 0.0;
    double v = 0.0;
    Vec3 foot = Vec3::Zero(); ///< the footpoint S(u, v)
    /// Distance from the footpoint along the surface normal there, so the
    /// sign says which side the point is on (|signed_distance| = distance for
    /// a point inside the patch).
    double signed_distance = 0.0;
    /// The point lies beyond every patch's edges, not over a patch: it takes
    /// no part in the fit and is left out of the statistics.
    bool outside = false;
};

/// The closest footpoint of each point over `patches`. Points are tested only
/// against patches whose control-net box, grown by `tol::kFitCandidateMargin`
/// of the model size, contains them, nearest box first, and a patch whose box
/// is farther away than the best footpoint so far is skipped (the surface
/// lies inside its control net's box). With `previous` (the assignment of the
/// same points onto earlier versions of the same patches), the previous
/// patch goes first and its footpoint seeds Newton instead of a grid search:
/// this is the parameter correction step of a fit.
std::vector<FitAssignment> assign_points(const std::vector<BezierPatch>& patches,
                                         const std::vector<Vec3>& points,
                                         const std::vector<FitAssignment>* previous = nullptr);

/// Distances of the points inside, in millimetres.
struct FitStats {
    std::size_t points = 0;  ///< inside a patch
    std::size_t outside = 0; ///< beyond every patch, not counted below
    double max_mm = 0.0;
    double rms_mm = 0.0;
    double p95_mm = 0.0; ///< 95 % of the points are at most this far away
};

FitStats fit_stats(const std::vector<FitAssignment>& assignments);

} // namespace ada
