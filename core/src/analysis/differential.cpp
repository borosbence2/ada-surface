#include "ada/analysis/differential.hpp"

#include "n2s/tolerances.hpp"

#include <cmath>
#include <optional>

namespace ada {

std::optional<Vec3> unit_normal(const PatchDerivatives& d) {
    return n2s::normal_from_derivatives(d.du(), d.dv());
}

std::optional<n2s::SurfaceCurvature> curvature(const PatchDerivatives& d) {
    return n2s::curvature_from_derivatives(d.du(), d.dv(), d(2, 0), d(1, 1), d(0, 2));
}

std::optional<double> normal_curvature(const PatchDerivatives& d, const Vec3& direction) {
    const std::optional<Vec3> normal = unit_normal(d);
    if (!normal.has_value()) {
        return std::nullopt;
    }
    const Vec3 w = direction - direction.dot(*normal) * *normal;

    // Express w = a Su + b Sv through the first fundamental form.
    const double e = d.du().squaredNorm();
    const double f = d.du().dot(d.dv());
    const double g = d.dv().squaredNorm();
    const double det = e * g - f * f;
    const double wu = w.dot(d.du());
    const double wv = w.dot(d.dv());
    const double a = (g * wu - f * wv) / det;
    const double b = (e * wv - f * wu) / det;

    const double first = e * a * a + 2.0 * f * a * b + g * b * b;
    if (first < n2s::tol::kDegenerateDerivative) {
        return std::nullopt;
    }
    const double second = normal->dot(a * a * d(2, 0) + 2.0 * a * b * d(1, 1) + b * b * d(0, 2));
    return second / first;
}

} // namespace ada
