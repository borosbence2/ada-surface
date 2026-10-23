#include "ada/analysis/curvature.hpp"
#include "ada/analysis/differential.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "support/polynomial.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <vector>

using ada::BezierPatch;
using ada::PatchDerivatives;
using ada::Vec3;
using Catch::Approx;
using namespace ada::test;

namespace {

/// Curvature from exact derivatives agrees with closed forms to rounding: a few
/// hundred ulps after the fundamental-form arithmetic.
constexpr double kCurvatureExact = 1e-10;

/// The textbook sphere `r (cos t cos p, sin t cos p, sin p)` with its exact
/// derivatives. Not a polynomial, so it tests the curvature formulas
/// independently of the Bézier code.
PatchDerivatives sphere_derivatives(double r, double t, double p) {
    PatchDerivatives d;
    d(0, 0) = r * Vec3(std::cos(t) * std::cos(p), std::sin(t) * std::cos(p), std::sin(p));
    d(1, 0) = r * Vec3(-std::sin(t) * std::cos(p), std::cos(t) * std::cos(p), 0.0);
    d(0, 1) = r * Vec3(-std::cos(t) * std::sin(p), -std::sin(t) * std::sin(p), std::cos(p));
    d(2, 0) = r * Vec3(-std::cos(t) * std::cos(p), -std::sin(t) * std::cos(p), 0.0);
    d(1, 1) = r * Vec3(std::sin(t) * std::sin(p), -std::cos(t) * std::sin(p), 0.0);
    d(0, 2) = r * Vec3(-std::cos(t) * std::cos(p), -std::sin(t) * std::cos(p), -std::sin(p));
    return d;
}

/// `z = (x^2 + y^2) / (2R)` over `[-a, a]^2`, exactly, as a biquadratic.
BezierPatch paraboloid(double a, double radius, double sign_y2 = 1.0) {
    const double c = a * a / (2.0 * radius);
    PowerPatch power{2, 2, std::vector<Vec3>(9, Vec3::Zero())};
    auto at = [&](int k, int l) -> Vec3& {
        return power.coefficients[static_cast<std::size_t>(k * 3 + l)];
    };
    // x = a (2u - 1), y = a (2v - 1), z = c ((2u - 1)^2 + s (2v - 1)^2).
    at(0, 0) = Vec3(-a, -a, c * (1.0 + sign_y2));
    at(1, 0) = Vec3(2.0 * a, 0.0, -4.0 * c);
    at(2, 0) = Vec3(0.0, 0.0, 4.0 * c);
    at(0, 1) = Vec3(0.0, 2.0 * a, -4.0 * c * sign_y2);
    at(0, 2) = Vec3(0.0, 0.0, 4.0 * c * sign_y2);
    return power.to_bezier();
}

} // namespace

TEST_CASE("a sphere has Gaussian curvature 1/r^2 and normal curvature -1/r everywhere",
          "[analysis][curvature]") {
    const double r = 750.0;
    for (const double t : {0.0, 0.7, 2.1}) {
        for (const double p : {-1.1, 0.0, 0.4}) {
            const PatchDerivatives d = sphere_derivatives(r, t, p);
            const auto k = ada::curvature(d);
            REQUIRE(k.has_value());
            // The parametric normal points outwards, so the sphere bends away
            // from it: mean and normal curvatures are negative.
            CHECK(k->gaussian == Approx(1.0 / (r * r)).epsilon(kCurvatureExact));
            CHECK(k->mean == Approx(-1.0 / r).epsilon(kCurvatureExact));
            for (const Vec3& direction : {Vec3(1, 0, 0), Vec3(0.3, -2, 1), Vec3(0, 0, 1)}) {
                const auto kn = ada::normal_curvature(d, direction);
                if (kn.has_value()) {
                    CHECK(*kn == Approx(-1.0 / r).epsilon(kCurvatureExact));
                }
            }
        }
    }
}

TEST_CASE("a Bezier paraboloid has the closed-form curvature of z = (x^2 + y^2) / 2R",
          "[analysis][curvature]") {
    const double a = 400.0;
    const double radius = 2500.0;
    const ada::PatchEvaluator patch(paraboloid(a, radius));
    for (const double u : sample_parameters()) {
        for (const double v : sample_parameters()) {
            const double p = a * (2.0 * u - 1.0) / radius;
            const double q = a * (2.0 * v - 1.0) / radius;
            const double w = 1.0 + p * p + q * q;
            const auto k = ada::curvature(patch.derivatives(u, v));
            REQUIRE(k.has_value());
            CHECK(k->gaussian == Approx(1.0 / (radius * radius * w * w)).epsilon(kCurvatureExact));
            CHECK(k->mean == Approx((2.0 + p * p + q * q) / (2.0 * radius * std::pow(w, 1.5)))
                                 .epsilon(kCurvatureExact));
        }
    }
}

TEST_CASE("normal curvature picks out one direction", "[analysis][curvature]") {
    // At the apex of the paraboloid every direction has curvature 1/R; along a
    // ruling of the parabolic cylinder z = y^2 / 2R it is zero.
    const double radius = 1200.0;
    const ada::PatchEvaluator bowl(paraboloid(300.0, radius));
    const PatchDerivatives apex = bowl.derivatives(0.5, 0.5);
    CHECK(*ada::normal_curvature(apex, Vec3(1, 0, 0)) == Approx(1.0 / radius));
    CHECK(*ada::normal_curvature(apex, Vec3(1, 1, 0)) == Approx(1.0 / radius));

    const ada::PatchEvaluator saddle(paraboloid(300.0, radius, -1.0));
    const PatchDerivatives centre = saddle.derivatives(0.5, 0.5);
    CHECK(*ada::normal_curvature(centre, Vec3(1, 0, 0)) == Approx(1.0 / radius));
    CHECK(*ada::normal_curvature(centre, Vec3(0, 1, 0)) == Approx(-1.0 / radius));
    CHECK(std::abs(*ada::normal_curvature(centre, Vec3(1, 1, 0))) < 1e-12);
}

TEST_CASE("curvature summaries tell domes from saddles", "[analysis][curvature]") {
    const ada::CurvatureSummary dome =
        ada::summarize_curvature(ada::PatchEvaluator(paraboloid(400.0, 2500.0)), 17);
    CHECK(dome.undefined_samples == 0);
    CHECK(dome.gaussian_min > 0.0);
    CHECK(dome.saddle_fraction == 0.0);
    CHECK_FALSE(dome.gaussian_changes_sign);
    CHECK_FALSE(dome.mean_changes_sign);
    CHECK(dome.max_abs_principal == Approx(1.0 / 2500.0));

    // z = (x^2 - y^2) / 2R: K < 0 everywhere, H = 0 only on the diagonals, so
    // the mean curvature takes both signs.
    const ada::CurvatureSummary saddle =
        ada::summarize_curvature(ada::PatchEvaluator(paraboloid(400.0, 2500.0, -1.0)), 17);
    CHECK(saddle.gaussian_max < 0.0);
    CHECK(saddle.saddle_fraction == 1.0);
    CHECK(saddle.mean_changes_sign);
    CHECK_FALSE(saddle.gaussian_changes_sign);
}

TEST_CASE("curvature is undefined where the patch degenerates", "[analysis][curvature]") {
    // A bilinear patch whose whole UMin edge collapses to one point.
    const BezierPatch collapsed(1, 1, {Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0)});
    const ada::PatchEvaluator patch(collapsed);
    CHECK_FALSE(ada::curvature(patch.derivatives(0.0, 0.5)).has_value());
    CHECK_FALSE(ada::unit_normal(patch.derivatives(0.0, 0.5)).has_value());
    CHECK(ada::summarize_curvature(patch, 5).undefined_samples == 5);
}
