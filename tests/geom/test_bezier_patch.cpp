#include "ada/geom/bezier_patch.hpp"

#include "support/polynomial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

using ada::BezierCurve;
using ada::BezierPatch;
using ada::Edge;
using ada::PatchDerivatives;
using ada::Vec3;
using namespace ada::test;

namespace {

constexpr int kMaxTestedDegree = 7;
constexpr unsigned kSeed = 20261023;

const std::vector<Edge>& all_edges() {
    static const std::vector<Edge> edges{Edge::UMin, Edge::UMax, Edge::VMin, Edge::VMax};
    return edges;
}

/// `(u, v)` of the point at parameter `s` along `edge`.
std::pair<double, double> on_edge(Edge edge, double s) {
    switch (edge) {
    case Edge::UMin:
        return {0.0, s};
    case Edge::UMax:
        return {1.0, s};
    case Edge::VMin:
        return {s, 0.0};
    case Edge::VMax:
        return {s, 1.0};
    }
    return {0.0, 0.0};
}

/// First derivative across `edge`, pointing into the patch.
Vec3 inward_derivative(const BezierPatch& patch, Edge edge, double s) {
    const auto [u, v] = on_edge(edge, s);
    switch (edge) {
    case Edge::UMin:
        return patch.derivative(u, v, 1, 0);
    case Edge::UMax:
        return -patch.derivative(u, v, 1, 0);
    case Edge::VMin:
        return patch.derivative(u, v, 0, 1);
    case Edge::VMax:
        return -patch.derivative(u, v, 0, 1);
    }
    return Vec3::Zero();
}

} // namespace

TEST_CASE("tensor de Casteljau agrees with the closed-form Bernstein sum", "[geom][patch]") {
    std::mt19937 rng(kSeed);
    for (int du = 0; du <= kMaxTestedDegree; ++du) {
        for (int dv = 0; dv <= kMaxTestedDegree; ++dv) {
            const BezierPatch patch = random_patch(du, dv, rng, 1500.0);
            for (const double u : sample_parameters()) {
                for (const double v : sample_parameters()) {
                    INFO("degree (" << du << ", " << dv << "), (u, v) (" << u << ", " << v << ")");
                    CHECK(near(patch.evaluate(u, v), bernstein_sum(patch, u, v), kExact));
                }
            }
        }
    }
}

TEST_CASE("mixed partials up to third order match the power-basis oracle", "[geom][patch]") {
    std::mt19937 rng(kSeed);
    const std::vector<std::pair<int, int>> degrees{{1, 1}, {2, 3}, {3, 3}, {5, 4}, {7, 6}};
    for (const auto& [du, dv] : degrees) {
        const PowerPatch power = random_power_patch(du, dv, rng);
        const BezierPatch patch = power.to_bezier();
        for (const double u : sample_parameters()) {
            for (const double v : sample_parameters()) {
                const PatchDerivatives table = patch.derivatives(u, v);
                for (int k = 0; k <= PatchDerivatives::kMaxOrder; ++k) {
                    for (int l = 0; k + l <= PatchDerivatives::kMaxOrder; ++l) {
                        INFO("degree (" << du << ", " << dv << "), d^" << k << "u d^" << l
                                        << "v at (" << u << ", " << v << ")");
                        const Vec3 expected = power.derivative(u, v, k, l);
                        CHECK(near(table(k, l), expected, kExact));
                        CHECK(near(patch.derivative(u, v, k, l), expected, kExact));
                    }
                }
            }
        }
    }
}

TEST_CASE("orders past the degree give exactly zero", "[geom][patch]") {
    std::mt19937 rng(kSeed);
    const BezierPatch patch = random_patch(2, 3, rng);
    CHECK(patch.derivative(0.4, 0.6, 3, 0) == Vec3::Zero());
    CHECK(patch.derivative(0.4, 0.6, 0, 4) == Vec3::Zero());
    CHECK(patch.derivative(0.4, 0.6, 3, 4) == Vec3::Zero());

    const BezierPatch flat = patch.hodograph(3, 1);
    CHECK(flat.degree_u() == 0);
    CHECK(flat.degree_v() == 2);
}

TEST_CASE("first partials agree with central finite differences", "[geom][patch]") {
    std::mt19937 rng(kSeed);
    const BezierPatch patch = random_patch(5, 4, rng);
    const double h = kFiniteDifferenceStep;
    for (const double u : {0.2, 0.5, 0.77}) {
        for (const double v : {0.15, 0.6, 0.83}) {
            const Vec3 fd_u = (patch.evaluate(u + h, v) - patch.evaluate(u - h, v)) / (2.0 * h);
            const Vec3 fd_v = (patch.evaluate(u, v + h) - patch.evaluate(u, v - h)) / (2.0 * h);
            CHECK(near(patch.derivative(u, v, 1, 0), fd_u, kFiniteDifference));
            CHECK(near(patch.derivative(u, v, 0, 1), fd_v, kFiniteDifference));
        }
    }
}

TEST_CASE("degree elevation leaves the patch unchanged", "[geom][patch]") {
    std::mt19937 rng(kSeed);
    const BezierPatch patch = random_patch(3, 5, rng);
    const BezierPatch in_u = patch.elevated_u(2);
    const BezierPatch in_v = patch.elevated_v(1);
    REQUIRE(in_u.degree_u() == 5);
    REQUIRE(in_u.degree_v() == 5);
    REQUIRE(in_v.degree_u() == 3);
    REQUIRE(in_v.degree_v() == 6);
    for (const double u : sample_parameters()) {
        for (const double v : sample_parameters()) {
            CHECK(near(in_u.evaluate(u, v), patch.evaluate(u, v), kExact));
            CHECK(near(in_v.evaluate(u, v), patch.evaluate(u, v), kExact));
        }
    }
}

TEST_CASE("split pieces reproduce the original patch", "[geom][patch]") {
    std::mt19937 rng(kSeed);
    const BezierPatch patch = random_patch(4, 3, rng);
    const double at = 0.37;

    const auto [low_u, high_u] = patch.split_u(at);
    const auto [low_v, high_v] = patch.split_v(at);
    for (const double s : sample_parameters()) {
        for (const double t : sample_parameters()) {
            INFO("(s, t) (" << s << ", " << t << ")");
            CHECK(near(low_u.evaluate(s, t), patch.evaluate(s * at, t), kExact));
            CHECK(near(high_u.evaluate(s, t), patch.evaluate(at + s * (1.0 - at), t), kExact));
            CHECK(near(low_v.evaluate(s, t), patch.evaluate(s, t * at), kExact));
            CHECK(near(high_v.evaluate(s, t), patch.evaluate(s, at + t * (1.0 - at)), kExact));
        }
    }
}

TEST_CASE("boundary curves run along their edges", "[geom][patch]") {
    std::mt19937 rng(kSeed);
    const BezierPatch patch = random_patch(4, 3, rng);
    for (const Edge edge : all_edges()) {
        const BezierCurve boundary = patch.boundary(edge);
        CHECK(boundary.degree() == patch.degree_along(edge));
        for (const double s : sample_parameters()) {
            const auto [u, v] = on_edge(edge, s);
            CHECK(near(boundary.evaluate(s), patch.evaluate(u, v), kExact));
        }
    }
}

TEST_CASE("rows 0 and 1 give the cross-boundary derivative", "[geom][patch]") {
    // The fact every G1 edge constraint rests on: the derivative across an edge
    // is degree_across * (row 1 - row 0), as a Bézier curve along the edge.
    std::mt19937 rng(kSeed);
    const BezierPatch patch = random_patch(4, 3, rng);
    for (const Edge edge : all_edges()) {
        const std::vector<Vec3> row0 = patch.edge_row(edge, 0);
        const std::vector<Vec3> row1 = patch.edge_row(edge, 1);
        std::vector<Vec3> difference;
        for (std::size_t i = 0; i < row0.size(); ++i) {
            difference.push_back(patch.degree_across(edge) * (row1[i] - row0[i]));
        }
        const BezierCurve across(difference);
        for (const double s : sample_parameters()) {
            CHECK(near(across.evaluate(s), inward_derivative(patch, edge, s), kExact));
        }
    }
}

TEST_CASE("invalid patch input is rejected", "[geom][patch]") {
    std::mt19937 rng(kSeed);
    CHECK_THROWS_AS(BezierPatch(2, 2, random_points(8, rng)), std::invalid_argument);
    CHECK_THROWS_AS(BezierPatch(-1, 2, random_points(0, rng)), std::invalid_argument);

    const BezierPatch patch = random_patch(3, 2, rng);
    CHECK_THROWS_AS(patch.edge_row(Edge::VMin, 3), std::out_of_range);
    CHECK_THROWS_AS(patch.edge_row(Edge::UMax, -1), std::out_of_range);
    CHECK_THROWS_AS(patch.split_u(0.0), std::invalid_argument);
    CHECK_THROWS_AS(patch.split_v(1.0), std::invalid_argument);
    CHECK_THROWS_AS(patch.derivative(0.5, 0.5, -1, 0), std::invalid_argument);

    PatchDerivatives table;
    CHECK_THROWS_AS(table(2, 2), std::out_of_range);
}
