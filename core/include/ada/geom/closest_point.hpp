#pragma once

#include "ada/geom/bezier_curve.hpp"
#include "ada/geom/vec.hpp"

namespace ada {

struct CurveProjection {
    double parameter; ///< In [0, 1].
    Vec3 point;       ///< The curve at `parameter`.
    double distance;  ///< |point - target|.
};

/// Closest point on `curve` to `target`, with the parameter restricted to
/// [0, 1]. Coarse sampling picks the start (so the global minimum is found for
/// any curve that does not double back within one sample spacing), and Newton
/// on `(C(t) - P) . C'(t) = 0` refines it.
CurveProjection closest_point(const BezierCurve& curve, const Vec3& target);

} // namespace ada
