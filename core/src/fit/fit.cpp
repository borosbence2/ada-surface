#include "ada/fit/fit.hpp"

#include "ada/analysis/differential.hpp"
#include "ada/geom/convert.hpp"
#include "ada/geom/patch_evaluator.hpp"
#include "ada/tolerances.hpp"

#include "n2s/nurbs/projection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ada {

std::vector<FitAssignment> assign_points(const std::vector<BezierPatch>& patches,
                                         const std::vector<Vec3>& points,
                                         const std::vector<FitAssignment>* previous) {
    double size = 1.0;
    for (const BezierPatch& p : patches) {
        for (const Vec3& point : p.control_points()) {
            size = std::max(size, point.cwiseAbs().maxCoeff());
        }
    }
    const double margin = tol::kFitCandidateMargin * size;
    const double outside_offset = tol::kEdgeContact * size;

    std::vector<n2s::NurbsSurface> surfaces;
    std::vector<PatchEvaluator> evaluators;
    std::vector<std::pair<Vec3, Vec3>> boxes;
    for (const BezierPatch& p : patches) {
        surfaces.push_back(to_nurbs(p));
        evaluators.emplace_back(p);
        Vec3 lo = Vec3::Constant(std::numeric_limits<double>::infinity());
        Vec3 hi = -lo;
        for (const Vec3& point : p.control_points()) {
            lo = lo.cwiseMin(point);
            hi = hi.cwiseMax(point);
        }
        boxes.emplace_back(lo - Vec3::Constant(margin), hi + Vec3::Constant(margin));
    }

    // Distance from a point to a patch's control-net box (zero inside it): no
    // point of the surface is closer, since the surface lies in the box.
    auto box_distance = [&](const Vec3& point, std::size_t p) {
        const Vec3 lo = boxes[p].first + Vec3::Constant(margin);
        const Vec3 hi = boxes[p].second - Vec3::Constant(margin);
        return (lo - point).cwiseMax(point - hi).cwiseMax(0.0).norm();
    };

    std::vector<FitAssignment> out(points.size());
    std::vector<std::pair<double, std::size_t>> candidates;
    for (std::size_t k = 0; k < points.size(); ++k) {
        const Vec3& point = points[k];
        const FitAssignment* before =
            previous != nullptr && k < previous->size() && !(*previous)[k].outside ? &(*previous)[k]
                                                                                   : nullptr;
        candidates.clear();
        for (std::size_t p = 0; p < patches.size(); ++p) {
            const auto& [lo, hi] = boxes[p];
            if ((point.array() >= lo.array()).all() && (point.array() <= hi.array()).all()) {
                // The previous patch first: its warm start is cheap and its
                // footpoint usually wins, which lets the others be skipped.
                const bool first = before != nullptr && before->patch == p;
                candidates.emplace_back(first ? -1.0 : box_distance(point, p), p);
            }
        }
        std::sort(candidates.begin(), candidates.end());

        double best = std::numeric_limits<double>::infinity();
        n2s::SurfaceProjection found{};
        std::size_t found_patch = 0;
        for (const auto& [distance, p] : candidates) {
            if (distance >= best) {
                break; // this box, and every later one, is farther than the best footpoint
            }
            n2s::ProjectionOptions options;
            if (before != nullptr && before->patch == p) {
                options.start = std::array<double, 2>{before->u, before->v};
            }
            const n2s::SurfaceProjection projection =
                n2s::project_to_surface(surfaces[p], point, options);
            if (projection.distance < best) {
                best = projection.distance;
                found = projection;
                found_patch = p;
            }
        }
        FitAssignment& a = out[k];
        if (!std::isfinite(best)) {
            a.outside = true;
            continue;
        }
        a.patch = found_patch;
        a.u = found.u;
        a.v = found.v;
        a.foot = found.point;
        const PatchDerivatives d = evaluators[found_patch].derivatives(found.u, found.v);
        const Vec3 offset = point - d.position();
        const std::optional<Vec3> normal = unit_normal(d);
        if (!normal.has_value()) {
            a.signed_distance = offset.norm();
            continue;
        }
        a.signed_distance = offset.dot(*normal);
        // Past an edge: the footpoint is clamped there and the point lies off
        // to the side, not over the patch.
        const double sideways = (offset - a.signed_distance * *normal).norm();
        a.outside = found.on_boundary && sideways > outside_offset;
    }
    return out;
}

FitStats fit_stats(const std::vector<FitAssignment>& assignments) {
    FitStats stats;
    std::vector<double> distances;
    for (const FitAssignment& a : assignments) {
        if (a.outside) {
            ++stats.outside;
        } else {
            distances.push_back(std::abs(a.signed_distance));
        }
    }
    stats.points = distances.size();
    if (distances.empty()) {
        return stats;
    }
    std::sort(distances.begin(), distances.end());
    double sum = 0.0;
    for (const double d : distances) {
        sum += d * d;
    }
    stats.max_mm = distances.back();
    stats.rms_mm = std::sqrt(sum / static_cast<double>(distances.size()));
    // Nearest rank: the smallest value that at least 95 % of the points reach.
    const auto rank =
        static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(distances.size())));
    stats.p95_mm = distances[std::max<std::size_t>(rank, 1) - 1];
    return stats;
}

} // namespace ada
