#include "ada/solve/fairness.hpp"

#include "n2s/nurbs/detail/binomial.hpp"

#include <fmt/format.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace ada {

namespace {

/// Newton on the Legendre polynomial converges to machine precision in a
/// handful of steps from the Chebyshev-like start; this only bounds the loop.
constexpr int kMaxNewtonSteps = 100;
constexpr double kNodeTolerance = 1e-15;

/// k-th derivative of every degree-n Bernstein polynomial at t, from
/// `d^k B_i^n = n!/(n-k)! sum_j (-1)^j C(k, j) B_{i-k+j}^{n-k}`.
std::vector<double> bernstein_derivatives(int n, int k, double t) {
    std::vector<double> out(static_cast<std::size_t>(n) + 1, 0.0);
    if (k > n) {
        return out;
    }
    const int m = n - k;
    std::vector<double> lower(static_cast<std::size_t>(m) + 1);
    for (int i = 0; i <= m; ++i) {
        lower[static_cast<std::size_t>(i)] =
            n2s::detail::binomial(m, i) * std::pow(t, i) * std::pow(1.0 - t, m - i);
    }
    double factor = 1.0;
    for (int r = 0; r < k; ++r) {
        factor *= static_cast<double>(n - r);
    }
    for (int i = 0; i <= n; ++i) {
        double sum = 0.0;
        for (int j = 0; j <= k; ++j) {
            const int index = i - k + j;
            if (index >= 0 && index <= m) {
                const double sign = j % 2 == 0 ? 1.0 : -1.0;
                sum += sign * n2s::detail::binomial(k, j) * lower[static_cast<std::size_t>(index)];
            }
        }
        out[static_cast<std::size_t>(i)] = factor * sum;
    }
    return out;
}

/// `(A kron B)((i, j), (k, l)) = A(i, k) B(j, l)` with (i, j) -> i * B.rows() + j,
/// the control-point order of a patch.
Eigen::MatrixXd kronecker(const Eigen::MatrixXd& a, const Eigen::MatrixXd& b) {
    Eigen::MatrixXd out(a.rows() * b.rows(), a.cols() * b.cols());
    for (Eigen::Index i = 0; i < a.rows(); ++i) {
        for (Eigen::Index k = 0; k < a.cols(); ++k) {
            out.block(i * b.rows(), k * b.cols(), b.rows(), b.cols()) = a(i, k) * b;
        }
    }
    return out;
}

} // namespace

QuadratureRule gauss_legendre(int points) {
    if (points < 1) {
        throw std::invalid_argument(
            fmt::format("a quadrature rule needs at least one node, got {}", points));
    }
    QuadratureRule rule;
    const auto count = static_cast<std::size_t>(points);
    rule.nodes.resize(count);
    rule.weights.resize(count);
    const double n = static_cast<double>(points);
    for (int i = 0; i < points; ++i) {
        double x = std::cos(std::numbers::pi * (static_cast<double>(i) + 0.75) / (n + 0.5));
        double derivative = 0.0;
        for (int step = 0; step < kMaxNewtonSteps; ++step) {
            // P_n(x) and P_n'(x) by the three-term recurrence.
            double p0 = 1.0;
            double p1 = x;
            for (int k = 2; k <= points; ++k) {
                const double kd = static_cast<double>(k);
                const double p2 = ((2.0 * kd - 1.0) * x * p1 - (kd - 1.0) * p0) / kd;
                p0 = p1;
                p1 = p2;
            }
            const double pn = points == 1 ? x : p1;
            const double pn_minus_1 = points == 1 ? 1.0 : p0;
            derivative = n * (x * pn - pn_minus_1) / (x * x - 1.0);
            const double dx = pn / derivative;
            x -= dx;
            if (std::abs(dx) < kNodeTolerance) {
                break;
            }
        }
        // Map [-1, 1] to [0, 1].
        const auto slot = static_cast<std::size_t>(i);
        rule.nodes[slot] = 0.5 * (1.0 - x);
        rule.weights[slot] = 1.0 / ((1.0 - x * x) * derivative * derivative);
    }
    return rule;
}

Eigen::MatrixXd bernstein_gram(int degree, int derivative) {
    if (degree < 0 || derivative < 0) {
        throw std::invalid_argument("degree and derivative order must be non-negative");
    }
    const auto size = static_cast<Eigen::Index>(degree) + 1;
    Eigen::MatrixXd gram = Eigen::MatrixXd::Zero(size, size);
    // The integrand has degree 2 (degree - derivative) <= 2 degree, and
    // degree + 1 nodes integrate up to 2 degree + 1 exactly.
    const QuadratureRule rule = gauss_legendre(degree + 1);
    for (std::size_t q = 0; q < rule.nodes.size(); ++q) {
        const std::vector<double> b = bernstein_derivatives(degree, derivative, rule.nodes[q]);
        const Eigen::Map<const Eigen::VectorXd> v(b.data(), size);
        gram += rule.weights[q] * v * v.transpose();
    }
    return gram;
}

Eigen::MatrixXd fairness_matrix(int degree_u, int degree_v, FairnessOrder order) {
    auto gu = [degree_u](int k) { return bernstein_gram(degree_u, k); };
    auto gv = [degree_v](int k) { return bernstein_gram(degree_v, k); };
    switch (order) {
    case FairnessOrder::E2:
        return kronecker(gu(2), gv(0)) + 2.0 * kronecker(gu(1), gv(1)) + kronecker(gu(0), gv(2));
    case FairnessOrder::E3:
        return kronecker(gu(3), gv(0)) + 3.0 * kronecker(gu(2), gv(1)) +
               3.0 * kronecker(gu(1), gv(2)) + kronecker(gu(0), gv(3));
    }
    throw std::invalid_argument("unknown fairness order");
}

} // namespace ada
