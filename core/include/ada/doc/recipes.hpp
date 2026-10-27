#pragma once

// From recipes to control points: starting geometry, the scene solve, and
// the links recipes imply.

#include "ada/doc/scene.hpp"
#include "ada/fit/fit.hpp"
#include "ada/solve/solver.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace ada {

/// A from_boundaries patch's four curves, oriented to run with the patch
/// parameters (u for VMin/VMax, v for UMin/UMax), raised to a common degree
/// per direction, with shared corners snapped together.
struct Boundaries {
    BezierCurve umin;
    BezierCurve umax;
    BezierCurve vmin;
    BezierCurve vmax;

    int degree_u() const { return vmin.degree(); }

    int degree_v() const { return umin.degree(); }
};

/// Throws `std::invalid_argument` naming the curves if two of them do not
/// meet at a corner (to within `tol::kCurveCornerGap` of the model size).
Boundaries
resolve_boundaries(const Scene& scene, const FromBoundaries& recipe, const std::string& patch_id);

/// The solver's starting point for a recipe patch: for from_boundaries, the
/// boundary curves with a discrete Coons interior; for a blend, straight rows
/// across the gap. A blend's sources must already have control points.
BezierPatch initial_patch(const Scene& scene, const ScenePatch& patch);

/// The scene's explicit edge links plus the ones its recipes imply (a blend
/// joins both its sources), without duplicates. What the report checks.
std::vector<EdgeLink> all_edge_links(const Scene& scene);

/// How far a boundary curve had to give way (see `solve_scene`).
struct CurveDeviation {
    std::string curve;
    double max_mm = 0.0; ///< Largest control-point distance, an upper bound on the curve's.
};

/// How a fit to the scene's target went.
struct FitSummary {
    bool active = false;
    std::size_t iterations = 0; ///< solves the parameter correction took
    FitStats stats;             ///< the final surface against the target
};

struct SceneSolveSummary {
    std::size_t stages = 0;
    std::size_t unknowns = 0;
    double max_residual = 0.0; ///< mm
    double milliseconds = 0.0;
    std::vector<ConstraintResidual> residuals;
    /// Every curve used by a from_boundaries patch, in scene order.
    std::vector<CurveDeviation> curves;
    FitSummary fit;
};

/// Solves every recipe patch and stores the result (`solved = true`).
///
/// Stage 1 solves all from_boundaries patches together: their boundary rows
/// on the curves, the explicit edge links between them and any fixed patches
/// (C1/C2 joins, alpha = 1, gamma = 0), their symmetry links and pins, under
/// the scene's solve settings.
///
/// A curve's control points are held exactly, except the few that a G1/G2 join
/// or a symmetry link also acts on: the ones next to a node where curves
/// cross. Curves drawn independently are never smooth through such a node,
/// and G2 across the joint needs them to be, so there the curve control points
/// become strong soft targets (`tol::kCurveTargetWeight`) and give way exactly
/// as much as continuity demands. `SceneSolveSummary::curves` says how much.
///
/// With `scene.fit`, stage 1 also follows the target: each target point is
/// assigned to the from_boundaries patch it lies over, at its footpoint, the
/// stage is solved with the fit term, the points are projected again onto the
/// result, and this repeats until no footpoint moves by more than
/// `tol::kFitFootpointMove` of the model size (parameter correction). Throws
/// `std::invalid_argument` when the target is unknown or not loaded.
///
/// Each blend is then solved in dependency order with its sources held fixed;
/// its derivative scale on each side is chosen so the blend crosses the gap at
/// an even pace. Patches without a recipe never move.
///
/// Throws `SolveError` for contradictory constraints and
/// `std::invalid_argument` for unusable recipes (curves that do not meet,
/// blends whose sources form a cycle, pins on fixed patches).
SceneSolveSummary solve_scene(Scene& scene);

/// Why a degree cannot be set; `DegreeError` carries it so a caller can say
/// what to do about it.
enum class DegreeProblem {
    FixedPatch,  ///< a patch without a recipe is tied in and has another degree
    BelowCurves, ///< a from_boundaries patch cannot go below its curves' degree
    BlendTooLow, ///< a blend needs 2k + 1 across for Gk
};

struct DegreeError : std::invalid_argument {
    DegreeError(DegreeProblem reason, const std::string& message)
        : std::invalid_argument(message),
          problem(reason) {}

    DegreeProblem problem;
};

/// A from_boundaries patch's degree in one direction (`along_v`: its v
/// degree) without an override: its curves'.
int curve_degree(const Scene& scene, const ScenePatch& patch, bool along_v);

/// Sets a patch's degree in one direction, and the same on every patch
/// direction tied to it by linked edges, since matched edges need equal
/// degrees along them. A from_boundaries patch records it as a degree
/// override (none when it equals its curves'); a blend takes it as its degree
/// across, and along its edges follows its sources, which are tied to it; a
/// patch without a recipe must already have it. Returns the patches whose
/// degree changed. Throws `DegreeError`, and `std::invalid_argument` for an
/// unknown patch.
std::vector<std::string>
set_degree(Scene& scene, const std::string& patch, bool along_v, int degree);

/// What CV economy did.
struct EconomyResult {
    bool met = false; ///< the fit budgets pass
    /// One line per raise: which patch went to which degrees, and why.
    std::vector<std::string> steps;
    SceneSolveSummary last; ///< the final solve
};

/// CV economy for a fit (L3: as few control points as meet the target):
/// every from_boundaries patch starts at its curves' degree; while the fit
/// budgets fail, the patch whose target points are farthest off (RMS) is
/// raised by one degree in each direction (with every patch tied to it) and
/// the scene is solved again, up to `budgets.max_degree` (or
/// `kMaxRecipeDegree`). Throws `std::invalid_argument` for a scene without a
/// fit or without a fit budget.
EconomyResult fit_economically(Scene& scene);

/// A grid of from_boundaries patches over shared curves, for cases like a
/// hood or roof built from a curve network. Cell (r, c) is bounded by
/// `along_u[r][c]` (VMin), `along_u[r + 1][c]` (VMax), `along_v[c][r]` (UMin)
/// and `along_v[c + 1][r]` (UMax); u grows with c and v with r. Every internal
/// edge gets an edge link with `continuity`. Patches are named
/// "<prefix>_<r>_<c>" and start unsolved.
struct NetworkSpec {
    std::string prefix;
    std::vector<std::vector<std::string>> along_u; ///< rows + 1 lines of `cols` curves
    std::vector<std::vector<std::string>> along_v; ///< cols + 1 lines of `rows` curves
    Continuity continuity = Continuity::G2;
};

/// Returns the new patch ids, row by row. Throws `std::invalid_argument` if
/// the grid is ragged or an id is taken.
std::vector<std::string> add_network(Scene& scene, const NetworkSpec& spec);

} // namespace ada
