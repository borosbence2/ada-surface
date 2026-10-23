#include "ada/geom/bezier_curve.hpp"

#include "support/polynomial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <stdexcept>
#include <vector>

using ada::BezierCurve;
using ada::Vec3;
using namespace ada::test;

namespace {

constexpr int kMaxTestedDegree = 7;
constexpr unsigned kSeed = 20261023;

} // namespace

TEST_CASE("de Casteljau agrees with the closed-form Bernstein sum", "[geom][curve]") {
    std::mt19937 rng(kSeed);
    for (int degree = 0; degree <= kMaxTestedDegree; ++degree) {
        // Millimetre-scale coordinates, as a real body panel would have.
        const BezierCurve curve = random_curve(degree, rng, 1500.0);
        for (const double t : sample_parameters()) {
            INFO("degree " << degree << ", t " << t);
            CHECK(near(curve.evaluate(t), bernstein_sum(curve, t), kExact));
        }
    }
}

TEST_CASE("the curve interpolates its end control points", "[geom][curve]") {
    std::mt19937 rng(kSeed);
    const BezierCurve curve = random_curve(5, rng);
    CHECK(near(curve.evaluate(0.0), curve.control_point(0), kExact));
    CHECK(near(curve.evaluate(1.0), curve.control_point(5), kExact));
}

TEST_CASE("derivatives match the power-basis oracle exactly", "[geom][curve]") {
    std::mt19937 rng(kSeed);
    for (int degree = 0; degree <= kMaxTestedDegree; ++degree) {
        const PowerCurve power = random_power_curve(degree, rng);
        const BezierCurve curve = power.to_bezier();
        // Orders past the degree must come back as exactly zero.
        for (int order = 0; order <= degree + 2; ++order) {
            for (const double t : sample_parameters()) {
                INFO("degree " << degree << ", order " << order << ", t " << t);
                CHECK(near(curve.derivative(t, order), power.derivative(t, order), kExact));
            }
        }
    }
}

TEST_CASE("first derivative agrees with central finite differences", "[geom][curve]") {
    std::mt19937 rng(kSeed);
    const BezierCurve curve = random_curve(6, rng);
    for (const double t : {0.2, 0.5, 0.77}) {
        const Vec3 fd = (curve.evaluate(t + kFiniteDifferenceStep) -
                         curve.evaluate(t - kFiniteDifferenceStep)) /
                        (2.0 * kFiniteDifferenceStep);
        CHECK(near(curve.derivative(t, 1), fd, kFiniteDifference));
    }
}

TEST_CASE("hodographs drop one degree per order and vanish past the degree", "[geom][curve]") {
    std::mt19937 rng(kSeed);
    const BezierCurve curve = random_curve(4, rng);
    CHECK(curve.hodograph(0).degree() == 4);
    CHECK(curve.hodograph(1).degree() == 3);
    CHECK(curve.hodograph(4).degree() == 0);

    const BezierCurve beyond = curve.hodograph(5);
    CHECK(beyond.degree() == 0);
    CHECK(beyond.control_point(0) == Vec3::Zero());
}

TEST_CASE("degree elevation leaves the curve unchanged", "[geom][curve]") {
    std::mt19937 rng(kSeed);
    for (int degree = 0; degree <= kMaxTestedDegree; ++degree) {
        const BezierCurve curve = random_curve(degree, rng);
        for (const int times : {1, 3}) {
            const BezierCurve raised = curve.elevated(times);
            REQUIRE(raised.degree() == degree + times);
            for (const double t : sample_parameters()) {
                INFO("degree " << degree << ", times " << times << ", t " << t);
                CHECK(near(raised.evaluate(t), curve.evaluate(t), kExact));
            }
        }
    }
}

TEST_CASE("split pieces reproduce the original curve", "[geom][curve]") {
    std::mt19937 rng(kSeed);
    const BezierCurve curve = random_curve(6, rng);
    for (const double at : {0.3, 0.5, 0.71}) {
        const auto [left, right] = curve.split(at);
        CHECK(left.degree() == curve.degree());
        CHECK(right.degree() == curve.degree());
        for (const double s : sample_parameters()) {
            INFO("split at " << at << ", s " << s);
            CHECK(near(left.evaluate(s), curve.evaluate(s * at), kExact));
            CHECK(near(right.evaluate(s), curve.evaluate(at + s * (1.0 - at)), kExact));
        }
    }
}

TEST_CASE("reversal traverses the same points backwards", "[geom][curve]") {
    std::mt19937 rng(kSeed);
    const BezierCurve curve = random_curve(5, rng);
    const BezierCurve back = curve.reversed();
    for (const double t : sample_parameters()) {
        CHECK(near(back.evaluate(t), curve.evaluate(1.0 - t), kExact));
    }
}

TEST_CASE("invalid curve input is rejected", "[geom][curve]") {
    CHECK_THROWS_AS(BezierCurve(std::vector<Vec3>{}), std::invalid_argument);

    std::mt19937 rng(kSeed);
    const BezierCurve curve = random_curve(3, rng);
    CHECK_THROWS_AS(curve.split(0.0), std::invalid_argument);
    CHECK_THROWS_AS(curve.split(1.0), std::invalid_argument);
    CHECK_THROWS_AS(curve.derivative(0.5, -1), std::invalid_argument);
    CHECK_THROWS_AS(curve.elevated(-1), std::invalid_argument);
}
