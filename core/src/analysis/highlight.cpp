#include "ada/analysis/highlight.hpp"

#include "ada/analysis/differential.hpp"
#include "ada/tolerances.hpp"

#include "n2s/tolerances.hpp"

#include <Eigen/Geometry>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace ada {

namespace {

/// Lines across the default light's footprint, chosen so a 1-2 m panel shows
/// a dozen or so highlights: enough to see waviness, few enough to read.
constexpr double kDefaultLinesAcrossDiagonal = 24.0;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

} // namespace

LightPlane
make_light_plane(const Vec3& origin, const Vec3& normal, const Vec3& direction, double spacing) {
    if (!(spacing > 0.0)) {
        throw std::invalid_argument(
            fmt::format("light line spacing must be positive, got {}", spacing));
    }
    const Vec3 n = normal.normalized();
    const Vec3 d = direction - direction.dot(n) * n;
    if (d.norm() < n2s::tol::kDegenerateDerivative) {
        throw std::invalid_argument("light line direction is parallel to the light plane normal");
    }
    const Vec3 e = d.normalized();
    return LightPlane{origin, n, e, n.cross(e), spacing};
}

LightPlane default_light_plane(const Vec3& lo, const Vec3& hi) {
    const double diagonal = std::max((hi - lo).norm(), n2s::tol::kDegenerateDerivative);
    const Vec3 centre = 0.5 * (lo + hi);
    const Vec3 origin(centre.x(), centre.y(), hi.z() + diagonal);
    return make_light_plane(
        origin, Vec3::UnitZ(), Vec3::UnitX(), diagonal / kDefaultLinesAcrossDiagonal);
}

double highlight_parameter(const LightPlane& light, const Vec3& point, const Vec3& unit_normal) {
    const double facing = light.normal.dot(unit_normal);
    if (std::abs(facing) < tol::kHighlightParallel) {
        return kNaN;
    }
    const double t = light.normal.dot(light.origin - point) / facing;
    const Vec3 hit = point + t * unit_normal;
    return light.across.dot(hit - light.origin);
}

ScalarField highlight_field(const PatchEvaluator& patch, const LightPlane& light) {
    return [&patch, light](double u, double v) {
        PatchDerivatives d;
        d(0, 0) = patch.evaluate(u, v);
        d(1, 0) = patch.derivative(u, v, 1, 0);
        d(0, 1) = patch.derivative(u, v, 0, 1);
        const std::optional<Vec3> normal = unit_normal(d);
        return normal.has_value() ? highlight_parameter(light, d.position(), *normal) : kNaN;
    };
}

ScalarField isophote_field(const PatchEvaluator& patch, const Vec3& light_direction) {
    const Vec3 l = light_direction.normalized();
    return [&patch, l](double u, double v) {
        PatchDerivatives d;
        d(1, 0) = patch.derivative(u, v, 1, 0);
        d(0, 1) = patch.derivative(u, v, 0, 1);
        const std::optional<Vec3> normal = unit_normal(d);
        return normal.has_value() ? normal->dot(l) : kNaN;
    };
}

std::vector<ContourLine>
highlight_lines(const PatchEvaluator& patch, const LightPlane& light, std::size_t grid) {
    const ScalarField field = highlight_field(patch, light);
    const ScalarGrid samples = sample_field(field, grid, grid);
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const double s : samples.values) {
        if (!std::isnan(s)) {
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        }
    }
    std::vector<double> levels;
    if (lo <= hi) {
        for (double k = std::ceil(lo / light.spacing); k * light.spacing <= hi; k += 1.0) {
            levels.push_back(k * light.spacing);
        }
    }
    return extract_contours(samples, field, levels);
}

HighlightLineShape analyze_highlight_line(const PatchEvaluator& patch, const ContourLine& line) {
    HighlightLineShape shape;
    shape.level = line.level;
    shape.points = line.uv.size();

    std::vector<Vec3> points;
    std::vector<Vec3> normals;
    for (const Eigen::Vector2d& uv : line.uv) {
        PatchDerivatives d;
        d(1, 0) = patch.derivative(uv.x(), uv.y(), 1, 0);
        d(0, 1) = patch.derivative(uv.x(), uv.y(), 0, 1);
        points.push_back(patch.evaluate(uv.x(), uv.y()));
        normals.push_back(unit_normal(d).value_or(Vec3::Zero()));
    }

    // Discrete geodesic curvature at each interior vertex: turning angle over
    // the mean of the adjacent segment lengths, signed by the surface normal.
    std::vector<double> curvature;
    std::vector<double> station; // arc length at each curvature sample
    double arc = 0.0;
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        const Vec3 e0 = points[i] - points[i - 1];
        const Vec3 e1 = points[i + 1] - points[i];
        arc += e0.norm();
        const double mean_length = 0.5 * (e0.norm() + e1.norm());
        if (mean_length <= 0.0) {
            continue;
        }
        const Vec3 turn = e0.cross(e1);
        const double angle = std::atan2(turn.norm(), e0.dot(e1));
        const double sign = turn.dot(normals[i]) < 0.0 ? -1.0 : 1.0;
        curvature.push_back(sign * angle / mean_length);
        station.push_back(arc);
    }

    int last_sign = 0;
    for (std::size_t i = 0; i < curvature.size(); ++i) {
        const double k = curvature[i];
        shape.max_abs_curvature = std::max(shape.max_abs_curvature, std::abs(k));
        if (i > 0) {
            const double ds = station[i] - station[i - 1];
            if (ds > 0.0) {
                shape.max_curvature_rate =
                    std::max(shape.max_curvature_rate, std::abs(k - curvature[i - 1]) / ds);
            }
        }
        if (std::abs(k) < tol::kHighlightCurvatureNoise) {
            continue;
        }
        const int sign = k > 0.0 ? 1 : -1;
        if (last_sign != 0 && sign != last_sign) {
            ++shape.inflections;
        }
        last_sign = sign;
    }
    return shape;
}

HighlightSummary
analyze_highlights(const PatchEvaluator& patch, const LightPlane& light, std::size_t grid) {
    HighlightSummary summary;
    for (const ContourLine& line : highlight_lines(patch, light, grid)) {
        HighlightLineShape shape = analyze_highlight_line(patch, line);
        summary.max_inflections = std::max(summary.max_inflections, shape.inflections);
        summary.max_curvature_rate = std::max(summary.max_curvature_rate, shape.max_curvature_rate);
        summary.shapes.push_back(shape);
    }
    summary.lines = summary.shapes.size();
    return summary;
}

} // namespace ada
