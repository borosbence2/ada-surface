#include "ada/analysis/cv_hull.hpp"

#include "ada/analysis/differential.hpp"
#include "ada/tolerances.hpp"

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace ada {

namespace {

/// Greville abscissa of control point `index` in a direction of `degree`; the
/// middle of the domain for degree 0, which has a single control point.
double greville(std::size_t index, int degree) {
    return degree == 0 ? 0.5 : static_cast<double>(index) / static_cast<double>(degree);
}

struct RowShape {
    std::size_t inflections = 0;
    double max_turn = 0.0; // radians
};

/// Sign changes along a sequence of bending components (small-angle sizes),
/// ignoring those below `floor`.
std::size_t sign_changes(const std::vector<double>& components, double floor) {
    std::size_t count = 0;
    int last_sign = 0;
    for (const double c : components) {
        if (std::abs(c) < floor) {
            continue;
        }
        const int sign = c > 0.0 ? 1 : -1;
        if (last_sign != 0 && sign != last_sign) {
            ++count;
        }
        last_sign = sign;
    }
    return count;
}

/// A row bends through its second differences `e1 - e0`. The component along
/// the surface normal is bending up or down (out of the surface); the component
/// along `normal x row direction` is bending sideways (within it). Each can
/// zig-zag on its own, so their sign changes are counted separately. The turn
/// vector `e0 x e1` would be the wrong reference: for a row in a plane that
/// contains the normal, which is every row along a principal direction, it is
/// perpendicular to the normal and its sign there is rounding noise.
RowShape analyze_row(const std::vector<Vec3>& points, const std::vector<Vec3>& normals) {
    RowShape shape;
    std::vector<double> vertical;
    std::vector<double> lateral;
    double largest = 0.0;
    for (std::size_t k = 1; k + 1 < points.size(); ++k) {
        const Vec3 e0 = points[k] - points[k - 1];
        const Vec3 e1 = points[k + 1] - points[k];
        const double mean_length = 0.5 * (e0.norm() + e1.norm());
        if (mean_length <= 0.0) {
            continue;
        }
        shape.max_turn = std::max(shape.max_turn, std::atan2(e0.cross(e1).norm(), e0.dot(e1)));

        const Vec3& n = normals[k];
        const Vec3 along = (e0 + e1).normalized();
        const Vec3 side = n.cross(along);
        const Vec3 bend = e1 - e0;
        vertical.push_back(bend.dot(n) / mean_length);
        lateral.push_back(bend.dot(side) / mean_length);
        largest = std::max(largest, bend.norm() / mean_length);
    }
    const double floor = std::max(tol::kHullTurnNoise, tol::kHullRelativeNoise * largest);
    shape.inflections = std::max(sign_changes(vertical, floor), sign_changes(lateral, floor));
    return shape;
}

} // namespace

CvHullSummary analyze_cv_hull(const PatchEvaluator& evaluator) {
    const BezierPatch& patch = evaluator.patch();
    const std::size_t nu = patch.num_u();
    const std::size_t nv = patch.num_v();

    // Reference normal per control point, from the surface at its Greville
    // point; a degenerate spot contributes a zero normal and so never flips a
    // turn's sign.
    std::vector<Vec3> normals(nu * nv);
    for (std::size_t i = 0; i < nu; ++i) {
        for (std::size_t j = 0; j < nv; ++j) {
            const double u = greville(i, patch.degree_u());
            const double v = greville(j, patch.degree_v());
            normals[i * nv + j] = unit_normal(evaluator.derivatives(u, v)).value_or(Vec3::Zero());
        }
    }

    CvHullSummary summary;
    auto add = [&](const std::vector<Vec3>& points, const std::vector<Vec3>& row_normals) {
        if (points.size() < 3) {
            return;
        }
        const RowShape shape = analyze_row(points, row_normals);
        ++summary.rows;
        summary.max_row_inflections = std::max(summary.max_row_inflections, shape.inflections);
        summary.max_turning_angle_deg =
            std::max(summary.max_turning_angle_deg, shape.max_turn * 180.0 / std::numbers::pi);
        if (shape.inflections >= 2) {
            ++summary.zigzag_rows;
        }
    };

    for (std::size_t j = 0; j < nv; ++j) {
        std::vector<Vec3> points;
        std::vector<Vec3> row_normals;
        for (std::size_t i = 0; i < nu; ++i) {
            points.push_back(patch.control_point(i, j));
            row_normals.push_back(normals[i * nv + j]);
        }
        add(points, row_normals);
    }
    for (std::size_t i = 0; i < nu; ++i) {
        std::vector<Vec3> points;
        std::vector<Vec3> row_normals;
        for (std::size_t j = 0; j < nv; ++j) {
            points.push_back(patch.control_point(i, j));
            row_normals.push_back(normals[i * nv + j]);
        }
        add(points, row_normals);
    }
    return summary;
}

} // namespace ada
