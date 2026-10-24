#include "ada/geom/curve_fit.hpp"

#include "n2s/nurbs/detail/binomial.hpp"
#include "n2s/tolerances.hpp"

#include <Eigen/LU>
#include <Eigen/QR>
#include <fmt/format.h>

#include <cmath>
#include <stdexcept>

namespace ada {

std::vector<double> bernstein_values(int degree, double t) {
    std::vector<double> values(static_cast<std::size_t>(degree) + 1);
    for (int i = 0; i <= degree; ++i) {
        values[static_cast<std::size_t>(i)] =
            n2s::detail::binomial(degree, i) * std::pow(t, i) * std::pow(1.0 - t, degree - i);
    }
    return values;
}

std::vector<double> chord_length_parameters(const std::vector<Vec3>& points) {
    if (points.size() < 2) {
        throw std::invalid_argument("a curve needs at least two points");
    }
    std::vector<double> t(points.size(), 0.0);
    for (std::size_t k = 1; k < points.size(); ++k) {
        const double chord = (points[k] - points[k - 1]).norm();
        if (chord < n2s::tol::kDegenerateDerivative) {
            throw std::invalid_argument(fmt::format(
                "points {} and {} coincide; a curve cannot pass through both in order", k - 1, k));
        }
        t[k] = t[k - 1] + chord;
    }
    for (double& value : t) {
        value /= t.back();
    }
    return t;
}

BezierCurve curve_through_points(const std::vector<Vec3>& points, int degree) {
    if (degree < 1) {
        throw std::invalid_argument(fmt::format("curve degree must be at least 1, got {}", degree));
    }
    const std::vector<double> t = chord_length_parameters(points);
    const auto count = static_cast<Eigen::Index>(points.size());

    if (static_cast<int>(points.size()) <= degree + 1) {
        // Interpolate at the lowest degree that can, then elevate: elevation
        // keeps the curve, so the points stay on it.
        const auto low = static_cast<int>(points.size()) - 1;
        Eigen::MatrixXd basis(count, count);
        for (Eigen::Index k = 0; k < count; ++k) {
            const std::vector<double> b = bernstein_values(low, t[static_cast<std::size_t>(k)]);
            for (Eigen::Index j = 0; j < count; ++j) {
                basis(k, j) = b[static_cast<std::size_t>(j)];
            }
        }
        const Eigen::PartialPivLU<Eigen::MatrixXd> lu(basis);
        std::vector<Vec3> control(points.size());
        for (int axis = 0; axis < 3; ++axis) {
            Eigen::VectorXd q(count);
            for (Eigen::Index k = 0; k < count; ++k) {
                q(k) = points[static_cast<std::size_t>(k)](axis);
            }
            const Eigen::VectorXd p = lu.solve(q);
            for (Eigen::Index j = 0; j < count; ++j) {
                control[static_cast<std::size_t>(j)](axis) = p(j);
            }
        }
        return BezierCurve(std::move(control)).elevated(degree - low);
    }

    // Least squares for the inner control points, with the end points held
    // on the first and last data point.
    const auto inner = static_cast<Eigen::Index>(degree) - 1;
    Eigen::MatrixXd a(count, inner);
    std::vector<std::vector<double>> b(points.size());
    for (Eigen::Index k = 0; k < count; ++k) {
        b[static_cast<std::size_t>(k)] = bernstein_values(degree, t[static_cast<std::size_t>(k)]);
        for (Eigen::Index j = 0; j < inner; ++j) {
            a(k, j) = b[static_cast<std::size_t>(k)][static_cast<std::size_t>(j + 1)];
        }
    }
    const Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(a);
    std::vector<Vec3> control(static_cast<std::size_t>(degree) + 1);
    control.front() = points.front();
    control.back() = points.back();
    for (int axis = 0; axis < 3; ++axis) {
        Eigen::VectorXd rhs(count);
        for (Eigen::Index k = 0; k < count; ++k) {
            const auto& bk = b[static_cast<std::size_t>(k)];
            rhs(k) = points[static_cast<std::size_t>(k)](axis) - bk.front() * points.front()(axis) -
                     bk.back() * points.back()(axis);
        }
        const Eigen::VectorXd p = qr.solve(rhs);
        for (Eigen::Index j = 0; j < inner; ++j) {
            control[static_cast<std::size_t>(j + 1)](axis) = p(j);
        }
    }
    return BezierCurve(std::move(control));
}

} // namespace ada
