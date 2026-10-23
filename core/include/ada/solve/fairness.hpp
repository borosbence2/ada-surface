#pragma once

#include <Eigen/Core>

#include <vector>

namespace ada {

/// Gauss-Legendre rule on [0, 1] with `points` nodes: exact for polynomials
/// of degree up to `2 * points - 1`.
struct QuadratureRule {
    std::vector<double> nodes;
    std::vector<double> weights;
};

/// Throws `std::invalid_argument` for `points < 1`.
QuadratureRule gauss_legendre(int points);

/// The Gram matrix of the `derivative`-th derivatives of the degree-`degree`
/// Bernstein basis on [0, 1]: `G(i, k) = int B_i^(d)(t) B_k^(d)(t) dt`. Exact
/// (integrated by Gauss-Legendre with enough nodes for the polynomial degree).
Eigen::MatrixXd bernstein_gram(int degree, int derivative);

enum class FairnessOrder {
    /// Thin-plate energy `int |S_uu|^2 + 2|S_uv|^2 + |S_vv|^2`: penalises
    /// bending. Zero exactly on affine patches (polynomials of total degree <= 1).
    E2,
    /// Linearised curvature variation `int |S_uuu|^2 + 3|S_uuv|^2 + 3|S_uvv|^2
    /// + |S_vvv|^2`: penalises changes of bending, which is what highlight
    /// lines show. Zero exactly on polynomials of total degree <= 2.
    E3,
};

/// The fairness energy of a degree (du, dv) patch as a quadratic form on its
/// control points, per coordinate: `E = x^T F x` for each of x, y, z, with
/// the control points ordered like `BezierPatch` (i * (dv + 1) + j).
Eigen::MatrixXd fairness_matrix(int degree_u, int degree_v, FairnessOrder order);

} // namespace ada
