#include "ada/geom/convert.hpp"

#include "n2s/nurbs/knot_vector.hpp"
#include "n2s/nurbs/surface.hpp"

#include "support/polynomial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

using ada::BezierPatch;
using ada::PatchDerivatives;
using ada::Vec3;
using namespace ada::test;

namespace {

constexpr unsigned kSeed = 20261023;

std::vector<double> clamped_single_span(int degree, double a, double b) {
    std::vector<double> knots(static_cast<std::size_t>(degree) + 1, a);
    knots.insert(knots.end(), static_cast<std::size_t>(degree) + 1, b);
    return knots;
}

} // namespace

TEST_CASE("n2s evaluation of the converted patch agrees with the Bezier patch", "[geom][convert]") {
    // Cross-library oracle: n2s evaluates through Cox-de Boor basis functions
    // (Piegl & Tiller A2.2/A3.6), ADA through de Casteljau and hodographs. This
    // is also the check on the copied NurbsSurface code (see SOURCE.md).
    std::mt19937 rng(kSeed);
    const std::vector<std::pair<int, int>> degrees{{1, 1}, {3, 3}, {5, 2}, {7, 7}};
    for (const auto& [du, dv] : degrees) {
        const BezierPatch patch = random_patch(du, dv, rng);
        const n2s::NurbsSurface nurbs = ada::to_nurbs(patch);
        REQUIRE_FALSE(nurbs.is_rational());
        REQUIRE(nurbs.knots_u().num_spans() == 1);
        REQUIRE(nurbs.knots_v().num_spans() == 1);

        for (const double u : sample_parameters()) {
            for (const double v : sample_parameters()) {
                const n2s::SurfaceDerivatives expected =
                    nurbs.derivatives(u, v, PatchDerivatives::kMaxOrder);
                const PatchDerivatives actual = patch.derivatives(u, v);
                for (int k = 0; k <= PatchDerivatives::kMaxOrder; ++k) {
                    for (int l = 0; k + l <= PatchDerivatives::kMaxOrder; ++l) {
                        INFO("degree (" << du << ", " << dv << "), d^" << k << "u d^" << l
                                        << "v at (" << u << ", " << v << ")");
                        CHECK(near(actual(k, l), expected(k, l), kExact));
                    }
                }
            }
        }
    }
}

TEST_CASE("Bezier -> NURBS -> Bezier is the identity", "[geom][convert]") {
    std::mt19937 rng(kSeed);
    const BezierPatch patch = random_patch(4, 6, rng, 800.0);
    const BezierPatch back = ada::to_bezier(ada::to_nurbs(patch));
    REQUIRE(back.degree_u() == patch.degree_u());
    REQUIRE(back.degree_v() == patch.degree_v());
    CHECK(back.control_points() == patch.control_points());
}

TEST_CASE("a single-span surface on any domain maps onto the unit square", "[geom][convert]") {
    std::mt19937 rng(kSeed);
    const double a = 2.0;
    const double b = 5.0;
    const double c = -1.0;
    const double d = 3.0;
    // Equal but non-unit weights still describe a polynomial surface.
    const std::vector<Vec3> points = random_points(4 * 3, rng);
    const n2s::NurbsSurface nurbs(n2s::KnotVector(3, clamped_single_span(3, a, b)),
                                  n2s::KnotVector(2, clamped_single_span(2, c, d)),
                                  points,
                                  std::vector<double>(points.size(), 2.5));

    const BezierPatch patch = ada::to_bezier(nurbs);
    for (const double s : sample_parameters()) {
        for (const double t : sample_parameters()) {
            CHECK(near(
                patch.evaluate(s, t), nurbs.evaluate(a + s * (b - a), c + t * (d - c)), kExact));
        }
    }
}

TEST_CASE("surfaces without an exact single Bezier form are rejected", "[geom][convert]") {
    std::mt19937 rng(kSeed);

    SECTION("more than one span") {
        const n2s::NurbsSurface multi =
            n2s::NurbsSurface::bspline(n2s::KnotVector::uniform_clamped(3, 6),
                                       n2s::KnotVector(2, clamped_single_span(2, 0.0, 1.0)),
                                       random_points(6 * 3, rng));
        CHECK_THROWS_AS(ada::to_bezier(multi), std::invalid_argument);
    }

    SECTION("varying weights") {
        const std::vector<Vec3> points = random_points(3 * 3, rng);
        std::vector<double> weights(points.size(), 1.0);
        weights[4] = 0.7;
        const n2s::NurbsSurface rational(n2s::KnotVector(2, clamped_single_span(2, 0.0, 1.0)),
                                         n2s::KnotVector(2, clamped_single_span(2, 0.0, 1.0)),
                                         points,
                                         weights);
        CHECK_THROWS_AS(ada::to_bezier(rational), std::invalid_argument);
    }

    SECTION("a degree-0 direction cannot become a B-spline") {
        CHECK_THROWS_AS(ada::to_nurbs(random_patch(0, 2, rng)), std::invalid_argument);
    }
}
