#include "ada/geom/convert.hpp"

#include "n2s/nurbs/knot_vector.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ada {

namespace {

n2s::KnotVector unit_single_span(int degree) {
    std::vector<double> knots(static_cast<std::size_t>(degree) + 1, 0.0);
    knots.insert(knots.end(), static_cast<std::size_t>(degree) + 1, 1.0);
    return n2s::KnotVector(degree, std::move(knots));
}

/// A clamped knot vector with exactly `degree + 1` control points is
/// `[a^(p+1), b^(p+1)]`, the only kind with an exact single Bézier form.
void require_single_span(const n2s::KnotVector& knots, const char* direction) {
    const auto expected = static_cast<std::size_t>(knots.degree()) + 1;
    if (!knots.is_clamped() || knots.num_control_points() != expected) {
        throw std::invalid_argument(fmt::format(
            "surface has {} spans in {} (clamped: {}); only a single clamped span has an exact "
            "Bezier form, so split it at its interior knots first",
            knots.num_spans(),
            direction,
            knots.is_clamped()));
    }
}

} // namespace

n2s::NurbsSurface to_nurbs(const BezierPatch& patch) {
    if (patch.degree_u() < 1 || patch.degree_v() < 1) {
        throw std::invalid_argument(
            fmt::format("a degree ({}, {}) patch has no B-spline form; B-splines need degree >= 1",
                        patch.degree_u(),
                        patch.degree_v()));
    }
    return n2s::NurbsSurface::bspline(unit_single_span(patch.degree_u()),
                                      unit_single_span(patch.degree_v()),
                                      patch.control_points());
}

BezierPatch to_bezier(const n2s::NurbsSurface& surface) {
    require_single_span(surface.knots_u(), "u");
    require_single_span(surface.knots_v(), "v");
    if (surface.is_rational()) {
        throw std::invalid_argument(
            "surface has varying weights; a rational surface has no polynomial Bezier form");
    }
    return BezierPatch(surface.degree_u(), surface.degree_v(), surface.control_points());
}

} // namespace ada
