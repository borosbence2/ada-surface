#pragma once

// Test oracles for the Bézier code. Each one reaches the same polynomial by a
// different route than the implementation under test (closed-form Bernstein
// sums instead of de Casteljau, the power basis instead of hodographs), so a
// shared mistake is unlikely to cancel out.

#include "ada/geom/bezier_curve.hpp"
#include "ada/geom/bezier_patch.hpp"
#include "ada/geom/vec.hpp"

#include <cstddef>
#include <random>
#include <vector>

namespace ada::test {

/// Two evaluations of the same polynomial by different algorithms agree to
/// rounding. Applied relative to the magnitude of the expected value (floored
/// at 1), because high-order derivatives of high-degree patches reach the
/// hundreds even with unit-sized control nets.
inline constexpr double kExact = 1e-12;

/// Central finite differences with step `kFiniteDifferenceStep` are accurate to
/// O(h^2) truncation plus O(eps / h) rounding, both far below this.
inline constexpr double kFiniteDifference = 1e-7;
inline constexpr double kFiniteDifferenceStep = 1e-5;

/// `|actual - expected| <= tolerance * max(1, |expected|)`.
bool near(const Vec3& actual, const Vec3& expected, double tolerance);

/// Closed-form Bernstein polynomial `B_{i,n}(t) = C(n, i) t^i (1 - t)^(n - i)`.
double bernstein(int n, int i, double t);

/// Direct Bernstein sums, the oracle for de Casteljau.
Vec3 bernstein_sum(const BezierCurve& curve, double t);
Vec3 bernstein_sum(const BezierPatch& patch, double u, double v);

/// `C(t) = sum_k a_k t^k`, with coefficients `a_0 .. a_n`.
struct PowerCurve {
    std::vector<Vec3> coefficients;

    /// Exact derivative of any order (order 0 is the value).
    Vec3 derivative(double t, int order) const;

    /// The same polynomial in the Bernstein basis of the same degree.
    BezierCurve to_bezier() const;
};

/// `S(u, v) = sum_kl a_kl u^k v^l`, with `a_kl` at `k * (degree_v + 1) + l`.
struct PowerPatch {
    int degree_u = 0;
    int degree_v = 0;
    std::vector<Vec3> coefficients;

    /// Exact mixed partial of any order.
    Vec3 derivative(double u, double v, int du_order, int dv_order) const;

    /// The same polynomial in the tensor Bernstein basis of the same degrees.
    BezierPatch to_bezier() const;
};

/// Points with coordinates uniform in [-scale, scale].
std::vector<Vec3> random_points(std::size_t count, std::mt19937& rng, double scale = 1.0);

BezierCurve random_curve(int degree, std::mt19937& rng, double scale = 1.0);
BezierPatch random_patch(int degree_u, int degree_v, std::mt19937& rng, double scale = 1.0);
PowerCurve random_power_curve(int degree, std::mt19937& rng);
PowerPatch random_power_patch(int degree_u, int degree_v, std::mt19937& rng);

/// Where curves and patches are sampled: both ends, the middle, and a few
/// irregular interior values that avoid any special structure.
const std::vector<double>& sample_parameters();

} // namespace ada::test
