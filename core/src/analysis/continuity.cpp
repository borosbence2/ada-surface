#include "ada/analysis/continuity.hpp"

#include "ada/analysis/differential.hpp"
#include "ada/analysis/sampling.hpp"
#include "ada/geom/closest_point.hpp"
#include "ada/tolerances.hpp"

#include "n2s/tolerances.hpp"

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace ada {

namespace {

double degrees(double radians) {
    return radians * 180.0 / std::numbers::pi;
}

/// Angle between two unit vectors, accurate down to tiny angles (acos of a dot
/// product loses half the digits there, which is exactly the G1 range).
double angle_between(const Vec3& a, const Vec3& b) {
    return std::atan2(a.cross(b).norm(), a.dot(b));
}

} // namespace

EdgeContinuity measure_edge(const PatchEvaluator& a,
                            Edge edge_a,
                            const PatchEvaluator& b,
                            Edge edge_b,
                            std::size_t samples) {
    const BezierCurve boundary_a = a.patch().boundary(edge_a);
    const BezierCurve tangent_a = boundary_a.hodograph(1);
    const BezierCurve boundary_b = b.patch().boundary(edge_b);

    EdgeContinuity result;
    for (const double s : uniform_parameters(samples)) {
        ++result.samples;
        const Vec3 point = boundary_a.evaluate(s);
        const CurveProjection match = closest_point(boundary_b, point);
        result.max_gap = std::max(result.max_gap, match.distance);

        const auto [ua, va] = edge_point(edge_a, s);
        const auto [ub, vb] = edge_point(edge_b, match.parameter);
        const PatchDerivatives da = a.derivatives(ua, va);
        const PatchDerivatives db = b.derivatives(ub, vb);
        const std::optional<Vec3> na = unit_normal(da);
        const std::optional<Vec3> nb_raw = unit_normal(db);
        const Vec3 along = tangent_a.evaluate(s);
        if (!na.has_value() || !nb_raw.has_value() ||
            along.norm() < n2s::tol::kDegenerateDerivative) {
            ++result.undefined_samples;
            continue;
        }

        const double orientation = na->dot(*nb_raw) < 0.0 ? -1.0 : 1.0;
        result.orientation_flipped = result.orientation_flipped || orientation < 0.0;
        const Vec3 nb = orientation * *nb_raw;
        result.max_normal_angle_deg =
            std::max(result.max_normal_angle_deg, degrees(angle_between(*na, nb)));

        // Fold: both patches leave the edge towards the same side.
        const Vec3 t = along.normalized();
        Vec3 into_a = inward_derivative(da, edge_a);
        Vec3 into_b = inward_derivative(db, edge_b);
        into_a -= into_a.dot(t) * t;
        into_b -= into_b.dot(t) * t;
        result.fold = result.fold || into_a.dot(into_b) > 0.0;

        const Vec3 across = na->cross(t);
        const std::optional<double> ka = normal_curvature(da, across);
        const std::optional<double> kb_raw = normal_curvature(db, across);
        if (!ka.has_value() || !kb_raw.has_value()) {
            ++result.undefined_samples;
            continue;
        }
        const double kb = orientation * *kb_raw;
        const double difference = std::abs(*ka - kb);
        const double scale = std::max({std::abs(*ka), std::abs(kb), tol::kCurvatureFloor});
        result.max_curvature_difference = std::max(result.max_curvature_difference, difference);
        result.max_curvature_deviation =
            std::max(result.max_curvature_deviation, difference / scale);
    }
    return result;
}

SymmetryContinuity measure_symmetry(const PatchEvaluator& patch, Edge edge, std::size_t samples) {
    const BezierCurve boundary = patch.patch().boundary(edge);
    SymmetryContinuity result;
    for (const double s : uniform_parameters(samples)) {
        ++result.samples;
        result.max_plane_distance =
            std::max(result.max_plane_distance, std::abs(boundary.evaluate(s).y()));

        const auto [u, v] = edge_point(edge, s);
        const std::optional<Vec3> normal = unit_normal(patch.derivatives(u, v));
        if (!normal.has_value()) {
            ++result.undefined_samples;
            continue;
        }
        const double in_plane = std::hypot(normal->x(), normal->z());
        result.max_normal_tilt_deg = std::max(result.max_normal_tilt_deg,
                                              degrees(std::atan2(std::abs(normal->y()), in_plane)));
    }
    return result;
}

} // namespace ada
