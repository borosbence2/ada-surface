#pragma once

#include "ada/geom/bezier_patch.hpp"
#include "ada/solve/constraints.hpp"
#include "ada/solve/fairness.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace ada {

/// The two energy terms the solver trades off.
///
/// The fairness energy is normalised by its mean diagonal, so that `move = 1`
/// weighs moving a control point by d about as much as the fairness energy of
/// a distortion of size d, whatever the patch degrees and sizes. With
/// `fairness = 0` the solver returns the configuration closest to the current
/// control points that satisfies the constraints, which is how an edit stays
/// local ("make this edge G2 with the least change").
struct SolveWeights {
    double fairness = 1.0;
    FairnessOrder order = FairnessOrder::E3;
    double move = 0.0;
};

/// A strong pull of one control point towards a position, instead of a hard
/// constraint: `weight * |P - target|^2` joins the energy (weights relative to
/// the normalised fairness, like `move`). For inputs that should be followed
/// where possible but must give way where continuity demands, such as a
/// boundary curve's control points next to a G2 node.
struct SoftTarget {
    CvRef cv;
    Vec3 target;
    double weight;
};

struct SolveProblem {
    /// Current control points: the starting state, and what `move` pulls
    /// towards.
    std::vector<BezierPatch> patches;
    Constraints constraints;
    SolveWeights weights;
    std::vector<SoftTarget> targets;
};

struct ConstraintResidual {
    std::string label;
    double max_residual = 0.0; ///< Over the label's equations and coordinates, mm.
};

struct SolveResult {
    std::vector<BezierPatch> patches;
    std::vector<ConstraintResidual> residuals; ///< One per label, in first-seen order.
    double max_residual = 0.0;                 ///< mm
    std::size_t unknowns = 0;                  ///< Control-point coordinates.
    std::size_t equations = 0;                 ///< Constraint equations, over all axes.
    std::size_t independent_equations = 0;     ///< Their rank, over all axes.
    /// Largest over the three axes of max/min pivot of the reduced energy.
    /// Values near 1/eps mean the energy barely pins some combination of
    /// control points; the solution is then mostly decided by the tie-break.
    double condition_estimate = 1.0;
};

/// A constraint set the solver cannot satisfy, or input it cannot use. The
/// message names the constraints involved.
class SolveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Minimises `fairness * E + move * |x - x_current|^2` over all control points
/// subject to the linear constraints, exactly: the constraints are reduced to
/// an independent set by a rank-revealing QR (shared corners make some of them
/// redundant, which is fine), the energy is minimised over what they leave
/// free, and a tiny pull towards the current control points breaks ties where
/// neither term decides (`tol::kSolverTieBreak`).
///
/// Throws `SolveError` if the constraints contradict each other, naming the
/// labels with the largest residual.
SolveResult solve(const SolveProblem& problem);

} // namespace ada
