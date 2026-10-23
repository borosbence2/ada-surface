#pragma once

#include "ada/geom/bezier_patch.hpp"

#include "n2s/nurbs/surface.hpp"

namespace ada {

/// The patch as a clamped, single-span, non-rational B-spline surface on
/// [0, 1]^2, the form IGES export and the copied n2s algorithms take. Throws
/// `std::invalid_argument` for a degree-0 direction, which B-splines lack.
n2s::NurbsSurface to_nurbs(const BezierPatch& patch);

/// The Bézier form of a clamped, single-span surface whose weights are all
/// equal. Its domain `[a, b] x [c, d]` maps affinely onto [0, 1]^2, and the
/// control points carry over unchanged. Throws `std::invalid_argument`, naming
/// the reason, for a surface with more than one span in a direction or with
/// varying weights.
BezierPatch to_bezier(const n2s::NurbsSurface& surface);

} // namespace ada
