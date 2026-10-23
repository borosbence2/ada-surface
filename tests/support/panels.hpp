#pragma once

// Hand-built panels with known continuity, shared by the analysis tests and
// the example scenes. Each builder states the property it holds by
// construction; the tests then check that the analysis measures it.

#include "ada/doc/scene.hpp"
#include "ada/geom/bezier_patch.hpp"

#include <cstddef>
#include <utility>

namespace ada::test {

/// A gently domed bicubic panel, about 1000 x 600 mm; v = 1 at y = 600.
BezierPatch domed_panel();

/// A bicubic neighbour across `a`'s VMax edge (a must be bicubic), G2 by
/// construction with reparameterisation factors alpha and gamma:
/// `B_v = alpha A_v` and `B_vv = alpha^2 A_vv + gamma A_v` along the edge.
/// With alpha != 1 or gamma != 0 the join is G2 but not C2. `sign = -1` sends
/// the neighbour back over `a` (a fold).
BezierPatch g2_neighbour(const BezierPatch& a, double alpha, double gamma, double sign = 1.0);

/// The same surface with u running the other way.
BezierPatch flip_u(const BezierPatch& p);

/// The same surface reflected in the plane y = 0.
BezierPatch mirror_y(const BezierPatch& p);

/// Moves control point (i, j) along the surface normal at its Greville point.
BezierPatch nudge(BezierPatch p, std::size_t i, std::size_t j, double distance);

/// A bicubic panel with its VMin edge in y = 0 and row 1 directly behind row 0
/// in y, which makes it G1 with its mirror image. Rows 2 and 3 are arbitrary.
BezierPatch symmetric_panel();

/// Two bicubic hood halves, front (x 0..600) and rear (x 600..~1200), each
/// spanning y 0..700 with u. Front's VMax meets rear's VMin C2 by
/// construction, and both UMin edges lie in y = 0, mirror-G1.
std::pair<BezierPatch, BezierPatch> hood_pair();

/// `hood_pair` as a scene: the C2 edge required G2 and both centre-line edges
/// required G2 against their mirror images. Passes the default budgets.
Scene hood_scene();

/// `hood_scene` with one control point in the rear patch's row 2 raised 4 mm:
/// still G1 at the joint, no longer G2.
Scene hood_scene_g2_broken();

} // namespace ada::test
