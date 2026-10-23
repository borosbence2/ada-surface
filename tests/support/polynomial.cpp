#include "support/polynomial.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ada::test {

namespace {

double binomial(int n, int k) {
    if (k < 0 || k > n) {
        return 0.0;
    }
    double result = 1.0;
    for (int i = 0; i < k; ++i) {
        result = result * static_cast<double>(n - i) / static_cast<double>(i + 1);
    }
    return result;
}

/// `k! / (k - r)!`, the factor that differentiating `t^k` r times brings down.
double falling_factorial(int k, int r) {
    double result = 1.0;
    for (int i = 0; i < r; ++i) {
        result *= static_cast<double>(k - i);
    }
    return result;
}

/// Power-to-Bernstein basis change: `t^k = sum_{i >= k} C(i, k) / C(n, k) B_{i,n}(t)`.
double power_to_bernstein(int n, int i, int k) {
    return k > i ? 0.0 : binomial(i, k) / binomial(n, k);
}

std::size_t index(int i) {
    return static_cast<std::size_t>(i);
}

} // namespace

bool near(const Vec3& actual, const Vec3& expected, double tolerance) {
    return (actual - expected).norm() <= tolerance * std::max(1.0, expected.norm());
}

double bernstein(int n, int i, double t) {
    return binomial(n, i) * std::pow(t, i) * std::pow(1.0 - t, n - i);
}

Vec3 bernstein_sum(const BezierCurve& curve, double t) {
    Vec3 sum = Vec3::Zero();
    const int n = curve.degree();
    for (int i = 0; i <= n; ++i) {
        sum += bernstein(n, i, t) * curve.control_point(index(i));
    }
    return sum;
}

Vec3 bernstein_sum(const BezierPatch& patch, double u, double v) {
    Vec3 sum = Vec3::Zero();
    const int n = patch.degree_u();
    const int m = patch.degree_v();
    for (int i = 0; i <= n; ++i) {
        for (int j = 0; j <= m; ++j) {
            sum +=
                bernstein(n, i, u) * bernstein(m, j, v) * patch.control_point(index(i), index(j));
        }
    }
    return sum;
}

Vec3 PowerCurve::derivative(double t, int order) const {
    Vec3 sum = Vec3::Zero();
    const int n = static_cast<int>(coefficients.size()) - 1;
    for (int k = order; k <= n; ++k) {
        sum += falling_factorial(k, order) * std::pow(t, k - order) * coefficients[index(k)];
    }
    return sum;
}

BezierCurve PowerCurve::to_bezier() const {
    const int n = static_cast<int>(coefficients.size()) - 1;
    std::vector<Vec3> points(coefficients.size(), Vec3::Zero());
    for (int i = 0; i <= n; ++i) {
        for (int k = 0; k <= i; ++k) {
            points[index(i)] += power_to_bernstein(n, i, k) * coefficients[index(k)];
        }
    }
    return BezierCurve(std::move(points));
}

Vec3 PowerPatch::derivative(double u, double v, int du_order, int dv_order) const {
    Vec3 sum = Vec3::Zero();
    const std::size_t stride = index(degree_v) + 1;
    for (int k = du_order; k <= degree_u; ++k) {
        for (int l = dv_order; l <= degree_v; ++l) {
            sum += falling_factorial(k, du_order) * falling_factorial(l, dv_order) *
                   std::pow(u, k - du_order) * std::pow(v, l - dv_order) *
                   coefficients[index(k) * stride + index(l)];
        }
    }
    return sum;
}

BezierPatch PowerPatch::to_bezier() const {
    const std::size_t stride = index(degree_v) + 1;
    std::vector<Vec3> points(coefficients.size(), Vec3::Zero());
    for (int i = 0; i <= degree_u; ++i) {
        for (int j = 0; j <= degree_v; ++j) {
            Vec3& point = points[index(i) * stride + index(j)];
            for (int k = 0; k <= i; ++k) {
                for (int l = 0; l <= j; ++l) {
                    point += power_to_bernstein(degree_u, i, k) *
                             power_to_bernstein(degree_v, j, l) *
                             coefficients[index(k) * stride + index(l)];
                }
            }
        }
    }
    return BezierPatch(degree_u, degree_v, std::move(points));
}

std::vector<Vec3> random_points(std::size_t count, std::mt19937& rng, double scale) {
    std::uniform_real_distribution<double> coordinate(-scale, scale);
    std::vector<Vec3> points;
    points.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double x = coordinate(rng);
        const double y = coordinate(rng);
        const double z = coordinate(rng);
        points.emplace_back(x, y, z);
    }
    return points;
}

BezierCurve random_curve(int degree, std::mt19937& rng, double scale) {
    return BezierCurve(random_points(index(degree) + 1, rng, scale));
}

BezierPatch random_patch(int degree_u, int degree_v, std::mt19937& rng, double scale) {
    const std::size_t count = (index(degree_u) + 1) * (index(degree_v) + 1);
    return BezierPatch(degree_u, degree_v, random_points(count, rng, scale));
}

PowerCurve random_power_curve(int degree, std::mt19937& rng) {
    return PowerCurve{random_points(index(degree) + 1, rng)};
}

PowerPatch random_power_patch(int degree_u, int degree_v, std::mt19937& rng) {
    const std::size_t count = (index(degree_u) + 1) * (index(degree_v) + 1);
    return PowerPatch{degree_u, degree_v, random_points(count, rng)};
}

const std::vector<double>& sample_parameters() {
    static const std::vector<double> values{0.0, 0.0731, 0.25, 0.5, 0.6180339887, 0.9113, 1.0};
    return values;
}

} // namespace ada::test
