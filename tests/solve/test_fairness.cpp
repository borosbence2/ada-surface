#include "ada/geom/patch_evaluator.hpp"
#include "ada/solve/fairness.hpp"

#include "n2s/nurbs/detail/binomial.hpp"

#include "support/polynomial.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <vector>

using ada::BezierPatch;
using ada::FairnessOrder;
using ada::PatchEvaluator;
using ada::Vec3;
using Catch::Approx;
using namespace ada::test;

namespace {

constexpr unsigned kSeed = 20261023;
constexpr double kQuadratureExact = 1e-14;
constexpr double kEnergyExact = 1e-10;

/// sum over x, y, z of x^T F x, with F applied to each coordinate.
double quadratic_energy(const BezierPatch& patch, const Eigen::MatrixXd& f) {
    double total = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        Eigen::VectorXd x(static_cast<Eigen::Index>(patch.control_points().size()));
        for (std::size_t k = 0; k < patch.control_points().size(); ++k) {
            x(static_cast<Eigen::Index>(k)) = patch.control_points()[k](axis);
        }
        total += x.dot(f * x);
    }
    return total;
}

/// The same energy integrated directly from the patch's derivatives, through
/// the hodograph evaluator rather than Bernstein Gram matrices.
double integrated_energy(const BezierPatch& patch, FairnessOrder order) {
    const PatchEvaluator e(patch);
    const ada::QuadratureRule rule = ada::gauss_legendre(12);
    double total = 0.0;
    for (std::size_t a = 0; a < rule.nodes.size(); ++a) {
        for (std::size_t b = 0; b < rule.nodes.size(); ++b) {
            const ada::PatchDerivatives d = e.derivatives(rule.nodes[a], rule.nodes[b]);
            const double integrand =
                order == FairnessOrder::E2
                    ? d(2, 0).squaredNorm() + 2.0 * d(1, 1).squaredNorm() + d(0, 2).squaredNorm()
                    : d(3, 0).squaredNorm() + 3.0 * d(2, 1).squaredNorm() +
                          3.0 * d(1, 2).squaredNorm() + d(0, 3).squaredNorm();
            total += rule.weights[a] * rule.weights[b] * integrand;
        }
    }
    return total;
}

} // namespace

TEST_CASE("Gauss-Legendre integrates polynomials up to degree 2n-1 exactly", "[solve][fairness]") {
    for (int n = 1; n <= 10; ++n) {
        const ada::QuadratureRule rule = ada::gauss_legendre(n);
        for (int k = 0; k <= 2 * n - 1; ++k) {
            double sum = 0.0;
            for (std::size_t q = 0; q < rule.nodes.size(); ++q) {
                sum += rule.weights[q] * std::pow(rule.nodes[q], k);
            }
            INFO("n " << n << ", k " << k);
            CHECK(std::abs(sum - 1.0 / (k + 1)) < kQuadratureExact);
        }
    }
}

TEST_CASE("the Bernstein Gram matrix matches its closed form", "[solve][fairness]") {
    // int B_i^n B_j^n = C(n,i) C(n,j) / ((2n+1) C(2n, i+j)).
    using n2s::detail::binomial;
    for (int n = 0; n <= 7; ++n) {
        const Eigen::MatrixXd g = ada::bernstein_gram(n, 0);
        for (int i = 0; i <= n; ++i) {
            for (int j = 0; j <= n; ++j) {
                const double expected =
                    binomial(n, i) * binomial(n, j) / ((2 * n + 1) * binomial(2 * n, i + j));
                CHECK(std::abs(g(i, j) - expected) < kQuadratureExact);
            }
        }
    }
}

TEST_CASE("the fairness matrices integrate the energies exactly", "[solve][fairness]") {
    std::mt19937 rng(kSeed);
    for (const auto& [du, dv] : std::vector<std::pair<int, int>>{{3, 3}, {5, 4}, {7, 7}}) {
        const BezierPatch patch = random_patch(du, dv, rng, 500.0);
        for (const FairnessOrder order : {FairnessOrder::E2, FairnessOrder::E3}) {
            const double matrix = quadratic_energy(patch, ada::fairness_matrix(du, dv, order));
            CHECK(matrix == Approx(integrated_energy(patch, order)).epsilon(kEnergyExact));
        }
    }
}

TEST_CASE("E2 vanishes on affine patches and E3 on total degree 2", "[solve][fairness]") {
    // A parallelogram is affine; elevated to bicubic it still is.
    const BezierPatch affine =
        BezierPatch(1, 1, {Vec3(0, 0, 0), Vec3(100, 500, 20), Vec3(800, 0, 40), Vec3(900, 500, 60)})
            .elevated_u(2)
            .elevated_v(2);
    const double scale = 1e6; // the energies of a generic patch of this size
    CHECK(std::abs(quadratic_energy(affine, ada::fairness_matrix(3, 3, FairnessOrder::E2))) <
          kEnergyExact * scale);

    // z = (x^2 + y^2) / 2R has total degree 2: E3 vanishes, E2 does not.
    PowerPatch power{2, 2, std::vector<Vec3>(9, Vec3::Zero())};
    power.coefficients[0] = Vec3(0, 0, 0);
    power.coefficients[1 * 3 + 0] = Vec3(800, 0, 0);
    power.coefficients[0 * 3 + 1] = Vec3(0, 600, 0);
    power.coefficients[2 * 3 + 0] = Vec3(0, 0, 800.0 * 800.0 / 4000.0);
    power.coefficients[0 * 3 + 2] = Vec3(0, 0, 600.0 * 600.0 / 4000.0);
    const BezierPatch dome = power.to_bezier().elevated_u(1).elevated_v(1);
    CHECK(std::abs(quadratic_energy(dome, ada::fairness_matrix(3, 3, FairnessOrder::E3))) <
          kEnergyExact * scale);
    CHECK(quadratic_energy(dome, ada::fairness_matrix(3, 3, FairnessOrder::E2)) > 1.0);
}
