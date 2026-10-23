#pragma once

// Linear constraints on control points, the language the solver speaks. Every
// constraint here acts on x, y and z separately (an equation per coordinate,
// or for a chosen subset of coordinates), which is what lets the solver split
// into three independent problems. That holds for everything L0-L3 needs:
// fixed CVs, shared boundary rows, G1/G2 with constant alpha and gamma, and
// mirror symmetry in Y = 0. Shear terms and tilted symmetry planes would
// couple coordinates; they belong to L4+.

#include "ada/geom/bezier_patch.hpp"
#include "ada/geom/continuity.hpp"
#include "ada/geom/vec.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ada {

/// Control point (i, j) of patch number `patch` in a solve problem.
struct CvRef {
    std::size_t patch;
    std::size_t i;
    std::size_t j;
};

/// `sum(coefficient * P) = rhs`, imposed on each coordinate whose `axes`
/// entry is true.
struct LinearConstraint {
    std::vector<std::pair<CvRef, double>> terms;
    Vec3 rhs = Vec3::Zero();
    std::array<bool, 3> axes{true, true, true};
    /// Names the constraint in residual reports and error messages, e.g.
    /// "G2 front.VMax ~ rear.VMin". Equations from one link share a label.
    std::string label;
};

using Constraints = std::vector<LinearConstraint>;

/// Holds one control point at a position.
void add_fixed(Constraints& out, CvRef cv, const Vec3& position, const std::string& label);

/// Holds every control point of `edge_row(edge, depth)` where it is now.
void add_fixed_row(Constraints& out,
                   std::size_t patch_index,
                   const BezierPatch& patch,
                   Edge edge,
                   int depth,
                   const std::string& label);

/// Two patch edges that must join. alpha and gamma are the reparameterisation
/// factors of MILESTONES.md section 4: `B_s = alpha A_s` and
/// `B_ss = alpha^2 A_ss + gamma A_s`, with s running from A across the edge
/// into B. alpha = 1, gamma = 0 is parametric C1/C2.
struct EdgeJoin {
    std::size_t a;
    Edge edge_a;
    std::size_t b;
    Edge edge_b;
    Continuity continuity;
    double alpha = 1.0;
    double gamma = 0.0;
};

/// True if the two edges run in opposite directions, decided by which pairing
/// of their end points is closer in the current geometry.
bool edges_reversed(const BezierPatch& a, Edge edge_a, const BezierPatch& b, Edge edge_b);

/// The shared row (G0), then the row-1 (G1) and row-2 (G2) conditions, as far
/// as `join.continuity` asks. Throws `std::invalid_argument` if the degrees
/// along the two edges differ (matched edges only before L4), a patch is too
/// low in degree across the edge for the requested continuity, or alpha is not
/// positive.
void add_edge_join(Constraints& out,
                   const std::vector<BezierPatch>& patches,
                   const EdgeJoin& join,
                   const std::string& label);

/// Mirror symmetry in Y = 0 on `edge`: G0 puts row 0 in the plane; G1 also
/// keeps row 1 directly behind row 0 in y (same x and z), which makes the
/// cross-boundary derivative perpendicular to the plane. G2 adds nothing,
/// because mirror G1 implies mirror G2 (MILESTONES.md section 4).
void add_symmetry(Constraints& out,
                  std::size_t patch_index,
                  const BezierPatch& patch,
                  Edge edge,
                  Continuity continuity,
                  const std::string& label);

} // namespace ada
