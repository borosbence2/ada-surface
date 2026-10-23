#include "ada/geom/bezier_curve.hpp"

#include "geom/polygon_ops.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ada {

namespace {

void require_non_negative(int value, const char* what) {
    if (value < 0) {
        throw std::invalid_argument(fmt::format("{} must be non-negative, got {}", what, value));
    }
}

} // namespace

BezierCurve::BezierCurve(std::vector<Vec3> control_points)
    : points_(std::move(control_points)) {
    if (points_.empty()) {
        throw std::invalid_argument("a Bezier curve needs at least one control point");
    }
}

Vec3 BezierCurve::evaluate(double t) const {
    return detail::de_casteljau(points_, t);
}

Vec3 BezierCurve::derivative(double t, int order) const {
    require_non_negative(order, "derivative order");
    if (order > degree()) {
        return Vec3::Zero();
    }
    return hodograph(order).evaluate(t);
}

BezierCurve BezierCurve::hodograph(int order) const {
    require_non_negative(order, "hodograph order");
    std::vector<Vec3> points = points_;
    for (int k = 0; k < order; ++k) {
        points = detail::difference(points);
    }
    return BezierCurve(std::move(points));
}

BezierCurve BezierCurve::elevated(int times) const {
    require_non_negative(times, "degree elevation count");
    std::vector<Vec3> points = points_;
    for (int k = 0; k < times; ++k) {
        points = detail::elevate(points);
    }
    return BezierCurve(std::move(points));
}

std::pair<BezierCurve, BezierCurve> BezierCurve::split(double t) const {
    if (!(t > 0.0 && t < 1.0)) {
        throw std::invalid_argument(
            fmt::format("split parameter {} is not strictly inside (0, 1)", t));
    }
    auto [left, right] = detail::split(points_, t);
    return {BezierCurve(std::move(left)), BezierCurve(std::move(right))};
}

BezierCurve BezierCurve::reversed() const {
    std::vector<Vec3> points = points_;
    std::reverse(points.begin(), points.end());
    return BezierCurve(std::move(points));
}

} // namespace ada
