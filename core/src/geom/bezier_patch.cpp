#include "ada/geom/bezier_patch.hpp"

#include "geom/polygon_ops.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <span>
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

void require_inside(double t, const char* what) {
    if (!(t > 0.0 && t < 1.0)) {
        throw std::invalid_argument(
            fmt::format("{} split parameter {} is not strictly inside (0, 1)", what, t));
    }
}

std::size_t to_index(int value) {
    return static_cast<std::size_t>(value);
}

/// The same surface with u and v exchanged. Operations along u are written
/// once, along v (where each row of the net is contiguous), and reach u
/// through a transpose on either side.
BezierPatch transpose(const BezierPatch& patch) {
    std::vector<Vec3> points;
    points.reserve(patch.control_points().size());
    for (std::size_t j = 0; j < patch.num_v(); ++j) {
        for (std::size_t i = 0; i < patch.num_u(); ++i) {
            points.push_back(patch.control_point(i, j));
        }
    }
    return BezierPatch(patch.degree_v(), patch.degree_u(), std::move(points));
}

std::span<const Vec3> row(const BezierPatch& patch, std::size_t i) {
    return std::span<const Vec3>(patch.control_points()).subspan(i * patch.num_v(), patch.num_v());
}

/// Applies a polygon operation to every row (fixed i, j = 0..m) and
/// reassembles the results; every row comes back the same length.
template<typename Op>
BezierPatch map_rows(const BezierPatch& patch, Op op) {
    std::vector<Vec3> points;
    std::size_t new_num_v = 0;
    for (std::size_t i = 0; i < patch.num_u(); ++i) {
        std::vector<Vec3> mapped = op(row(patch, i));
        new_num_v = mapped.size();
        points.insert(points.end(), mapped.begin(), mapped.end());
    }
    return BezierPatch(patch.degree_u(), static_cast<int>(new_num_v) - 1, std::move(points));
}

BezierPatch hodograph_v(const BezierPatch& patch, int order) {
    BezierPatch result = patch;
    for (int k = 0; k < order; ++k) {
        result = map_rows(result, [](std::span<const Vec3> r) { return detail::difference(r); });
    }
    return result;
}

BezierPatch elevate_v(const BezierPatch& patch, int times) {
    BezierPatch result = patch;
    for (int k = 0; k < times; ++k) {
        result = map_rows(result, [](std::span<const Vec3> r) { return detail::elevate(r); });
    }
    return result;
}

std::pair<BezierPatch, BezierPatch> split_rows(const BezierPatch& patch, double t) {
    std::vector<Vec3> low;
    std::vector<Vec3> high;
    for (std::size_t i = 0; i < patch.num_u(); ++i) {
        auto [left, right] = detail::split(row(patch, i), t);
        low.insert(low.end(), left.begin(), left.end());
        high.insert(high.end(), right.begin(), right.end());
    }
    return {BezierPatch(patch.degree_u(), patch.degree_v(), std::move(low)),
            BezierPatch(patch.degree_u(), patch.degree_v(), std::move(high))};
}

} // namespace

PatchDerivatives::PatchDerivatives() {
    values_.fill(Vec3::Zero());
}

const Vec3& PatchDerivatives::operator()(int du_order, int dv_order) const {
    if (du_order < 0 || dv_order < 0 || du_order + dv_order > kMaxOrder) {
        throw std::out_of_range(
            fmt::format("derivative ({}, {}) is outside the table of total order <= {}",
                        du_order,
                        dv_order,
                        kMaxOrder));
    }
    return values_[to_index(du_order) * kStride + to_index(dv_order)];
}

Vec3& PatchDerivatives::operator()(int du_order, int dv_order) {
    return const_cast<Vec3&>(std::as_const(*this)(du_order, dv_order));
}

BezierPatch::BezierPatch(int degree_u, int degree_v, std::vector<Vec3> control_points)
    : degree_u_(degree_u),
      degree_v_(degree_v),
      points_(std::move(control_points)) {
    if (degree_u_ < 0 || degree_v_ < 0) {
        throw std::invalid_argument(
            fmt::format("patch degrees must be non-negative, got ({}, {})", degree_u_, degree_v_));
    }
    if (points_.size() != num_u() * num_v()) {
        throw std::invalid_argument(
            fmt::format("a degree ({}, {}) patch needs {} control points, got {}",
                        degree_u_,
                        degree_v_,
                        num_u() * num_v(),
                        points_.size()));
    }
}

Vec3 BezierPatch::evaluate(double u, double v) const {
    std::vector<Vec3> column;
    column.reserve(num_u());
    for (std::size_t i = 0; i < num_u(); ++i) {
        column.push_back(detail::de_casteljau(row(*this, i), v));
    }
    return detail::de_casteljau(column, u);
}

Vec3 BezierPatch::derivative(double u, double v, int du_order, int dv_order) const {
    require_non_negative(du_order, "u derivative order");
    require_non_negative(dv_order, "v derivative order");
    if (du_order > degree_u_ || dv_order > degree_v_) {
        return Vec3::Zero();
    }
    return hodograph(du_order, dv_order).evaluate(u, v);
}

PatchDerivatives BezierPatch::derivatives(double u, double v) const {
    PatchDerivatives table;
    for (int k = 0; k <= PatchDerivatives::kMaxOrder; ++k) {
        for (int l = 0; k + l <= PatchDerivatives::kMaxOrder; ++l) {
            table(k, l) = derivative(u, v, k, l);
        }
    }
    return table;
}

BezierPatch BezierPatch::hodograph(int du_order, int dv_order) const {
    require_non_negative(du_order, "u hodograph order");
    require_non_negative(dv_order, "v hodograph order");
    const BezierPatch in_v = hodograph_v(*this, dv_order);
    return transpose(hodograph_v(transpose(in_v), du_order));
}

BezierPatch BezierPatch::elevated_u(int times) const {
    require_non_negative(times, "degree elevation count");
    return transpose(elevate_v(transpose(*this), times));
}

BezierPatch BezierPatch::elevated_v(int times) const {
    require_non_negative(times, "degree elevation count");
    return elevate_v(*this, times);
}

std::pair<BezierPatch, BezierPatch> BezierPatch::split_u(double u) const {
    require_inside(u, "u");
    auto [low, high] = split_rows(transpose(*this), u);
    return {transpose(low), transpose(high)};
}

std::pair<BezierPatch, BezierPatch> BezierPatch::split_v(double v) const {
    require_inside(v, "v");
    return split_rows(*this, v);
}

int BezierPatch::degree_along(Edge edge) const noexcept {
    return edge == Edge::UMin || edge == Edge::UMax ? degree_v_ : degree_u_;
}

int BezierPatch::degree_across(Edge edge) const noexcept {
    return edge == Edge::UMin || edge == Edge::UMax ? degree_u_ : degree_v_;
}

std::vector<Vec3> BezierPatch::edge_row(Edge edge, int depth) const {
    if (depth < 0 || depth > degree_across(edge)) {
        throw std::out_of_range(
            fmt::format("row depth {} is outside 0..{}, the degree across this edge",
                        depth,
                        degree_across(edge)));
    }
    const std::size_t d = to_index(depth);
    std::vector<Vec3> result;
    switch (edge) {
    case Edge::UMin:
    case Edge::UMax: {
        const std::size_t i = edge == Edge::UMin ? d : to_index(degree_u_) - d;
        for (std::size_t j = 0; j < num_v(); ++j) {
            result.push_back(control_point(i, j));
        }
        break;
    }
    case Edge::VMin:
    case Edge::VMax: {
        const std::size_t j = edge == Edge::VMin ? d : to_index(degree_v_) - d;
        for (std::size_t i = 0; i < num_u(); ++i) {
            result.push_back(control_point(i, j));
        }
        break;
    }
    }
    return result;
}

BezierCurve BezierPatch::boundary(Edge edge) const {
    return BezierCurve(edge_row(edge, 0));
}

std::pair<double, double> edge_point(Edge edge, double s) {
    switch (edge) {
    case Edge::UMin:
        return {0.0, s};
    case Edge::UMax:
        return {1.0, s};
    case Edge::VMin:
        return {s, 0.0};
    case Edge::VMax:
        return {s, 1.0};
    }
    throw std::invalid_argument("unknown edge");
}

Vec3 inward_derivative(const PatchDerivatives& d, Edge edge) {
    switch (edge) {
    case Edge::UMin:
        return d.du();
    case Edge::UMax:
        return -d.du();
    case Edge::VMin:
        return d.dv();
    case Edge::VMax:
        return -d.dv();
    }
    throw std::invalid_argument("unknown edge");
}

const char* to_string(Edge edge) {
    switch (edge) {
    case Edge::UMin:
        return "UMin";
    case Edge::UMax:
        return "UMax";
    case Edge::VMin:
        return "VMin";
    case Edge::VMax:
        return "VMax";
    }
    throw std::invalid_argument("unknown edge");
}

Edge edge_from_string(const std::string& name) {
    for (const Edge edge : {Edge::UMin, Edge::UMax, Edge::VMin, Edge::VMax}) {
        if (name == to_string(edge)) {
            return edge;
        }
    }
    throw std::invalid_argument(
        fmt::format("'{}' is not an edge; expected UMin, UMax, VMin or VMax", name));
}

} // namespace ada
