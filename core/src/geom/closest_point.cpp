#include "ada/geom/closest_point.hpp"

#include "ada/tolerances.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ada {

namespace {

/// Samples for the coarse search. A Class-A boundary curve is a single-span
/// Bézier of degree <= 7; 64 samples resolve any wiggle such a curve can have.
constexpr std::size_t kCoarseSamples = 64;

/// Newton converges quadratically from the coarse start; this only bounds a
/// pathological case (a zero second derivative term) from looping.
constexpr int kMaxNewtonIterations = 50;

} // namespace

CurveProjection closest_point(const BezierCurve& curve, const Vec3& target) {
    double best_t = 0.0;
    double best_distance = (curve.evaluate(0.0) - target).norm();
    for (std::size_t i = 1; i <= kCoarseSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kCoarseSamples);
        const double distance = (curve.evaluate(t) - target).norm();
        if (distance < best_distance) {
            best_distance = distance;
            best_t = t;
        }
    }

    const BezierCurve first = curve.hodograph(1);
    const BezierCurve second = curve.hodograph(2);
    double t = best_t;
    for (int iteration = 0; iteration < kMaxNewtonIterations; ++iteration) {
        const Vec3 offset = curve.evaluate(t) - target;
        const Vec3 d1 = first.evaluate(t);
        const double g = offset.dot(d1);
        const double dg = d1.squaredNorm() + offset.dot(second.evaluate(t));
        if (!(dg > 0.0)) {
            break; // Not a local minimum direction; keep the best sample.
        }
        const double next = std::clamp(t - g / dg, 0.0, 1.0);
        const bool converged = std::abs(next - t) < tol::kClosestPointParameter;
        t = next;
        if (converged) {
            break;
        }
    }

    const Vec3 point = curve.evaluate(t);
    const double distance = (point - target).norm();
    if (distance > best_distance) {
        // Newton wandered to a worse critical point; the sample was better.
        return {best_t, curve.evaluate(best_t), best_distance};
    }
    return {t, point, distance};
}

} // namespace ada
