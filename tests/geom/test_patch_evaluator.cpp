#include "ada/geom/closest_point.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "support/polynomial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <stdexcept>

using ada::BezierCurve;
using ada::BezierPatch;
using ada::PatchDerivatives;
using ada::Vec3;
using namespace ada::test;

namespace {

constexpr unsigned kSeed = 20261023;

/// Newton on a smooth cubic recovers the parameter to well below this.
constexpr double kParameterRecovered = 1e-10;

} // namespace

TEST_CASE("the cached evaluator agrees with the patch", "[geom][evaluator]") {
    std::mt19937 rng(kSeed);
    const BezierPatch patch = random_patch(5, 3, rng, 900.0);
    const ada::PatchEvaluator evaluator(patch);
    for (const double u : sample_parameters()) {
        for (const double v : sample_parameters()) {
            const PatchDerivatives expected = patch.derivatives(u, v);
            const PatchDerivatives actual = evaluator.derivatives(u, v);
            for (int k = 0; k <= PatchDerivatives::kMaxOrder; ++k) {
                for (int l = 0; k + l <= PatchDerivatives::kMaxOrder; ++l) {
                    CHECK(near(actual(k, l), expected(k, l), kExact));
                }
            }
            CHECK(near(evaluator.evaluate(u, v), patch.evaluate(u, v), kExact));
        }
    }
    CHECK_THROWS_AS(evaluator.derivative(0.5, 0.5, 2, 2), std::out_of_range);
}

TEST_CASE("closest point recovers the parameter of a point on the curve", "[geom][closest]") {
    std::mt19937 rng(kSeed);
    const BezierCurve curve = random_curve(5, rng, 500.0);
    for (const double t : sample_parameters()) {
        const ada::CurveProjection p = ada::closest_point(curve, curve.evaluate(t));
        CHECK(std::abs(p.parameter - t) < kParameterRecovered);
        CHECK(p.distance < 1e-9);
    }
}

TEST_CASE("closest point from off the curve lands on the foot of the perpendicular",
          "[geom][closest]") {
    // A planar curve in z = 0: a point lifted straight up from C(t) has C(t) as
    // its closest point, at exactly the lift distance.
    const BezierCurve curve(
        {Vec3(0, 0, 0), Vec3(300, 200, 0), Vec3(700, -100, 0), Vec3(1000, 50, 0)});
    for (const double t : {0.1, 0.45, 0.8}) {
        const ada::CurveProjection p =
            ada::closest_point(curve, curve.evaluate(t) + Vec3(0, 0, 25));
        CHECK(std::abs(p.parameter - t) < kParameterRecovered);
        CHECK(std::abs(p.distance - 25.0) < 1e-9);
    }

    // Past the end the parameter clamps to it.
    const ada::CurveProjection beyond = ada::closest_point(curve, Vec3(1500, 80, 0));
    CHECK(beyond.parameter == 1.0);
}
