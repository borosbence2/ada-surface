#include "ada/solve/solver.hpp"

#include "ada/geom/curve_fit.hpp"
#include "ada/tolerances.hpp"

#include <Eigen/Cholesky>
#include <Eigen/QR>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace ada {

namespace {

/// Shown in an inconsistency error: every constraint carrying at least
/// `kConflictShare` of the worst residual, at most `kReportedConflicts` of
/// them. Enough to name every side of a contradiction, few enough to read.
constexpr std::size_t kReportedConflicts = 6;
constexpr double kConflictShare = 0.1;

struct Layout {
    std::vector<std::size_t> offset; // first unknown of each patch
    std::size_t size = 0;

    explicit Layout(const std::vector<BezierPatch>& patches) {
        for (const BezierPatch& p : patches) {
            offset.push_back(size);
            size += p.control_points().size();
        }
    }

    Eigen::Index index(const std::vector<BezierPatch>& patches, const CvRef& cv) const {
        const BezierPatch& p = patches.at(cv.patch);
        if (cv.i >= p.num_u() || cv.j >= p.num_v()) {
            throw SolveError(
                fmt::format("control point ({}, {}) is outside patch {}", cv.i, cv.j, cv.patch));
        }
        return static_cast<Eigen::Index>(offset[cv.patch] + cv.i * p.num_v() + cv.j);
    }
};

/// The rows of one axis: each constraint whose `axes` include it, normalised to
/// a unit coefficient vector so that the rank decision and the residuals are
/// in consistent units (length).
struct AxisSystem {
    Eigen::MatrixXd c;
    Eigen::VectorXd d;
    std::vector<std::size_t> source; // constraint index of each row
};

AxisSystem axis_system(const SolveProblem& problem, const Layout& layout, int axis) {
    std::vector<std::size_t> rows;
    for (std::size_t k = 0; k < problem.constraints.size(); ++k) {
        if (problem.constraints[k].axes[static_cast<std::size_t>(axis)]) {
            rows.push_back(k);
        }
    }
    AxisSystem system{Eigen::MatrixXd::Zero(static_cast<Eigen::Index>(rows.size()),
                                            static_cast<Eigen::Index>(layout.size)),
                      Eigen::VectorXd::Zero(static_cast<Eigen::Index>(rows.size())),
                      rows};
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const LinearConstraint& constraint = problem.constraints[rows[r]];
        const auto row = static_cast<Eigen::Index>(r);
        for (const auto& [cv, coefficient] : constraint.terms) {
            system.c(row, layout.index(problem.patches, cv)) += coefficient;
        }
        system.d(row) = constraint.rhs(axis);
        const double norm = system.c.row(row).norm();
        if (norm > 0.0) {
            system.c.row(row) /= norm;
            system.d(row) /= norm;
        }
    }
    return system;
}

/// Block-diagonal fairness matrix over all patches, normalised by its mean
/// diagonal (see SolveWeights).
Eigen::MatrixXd
fairness(const std::vector<BezierPatch>& patches, const Layout& layout, FairnessOrder order) {
    const auto n = static_cast<Eigen::Index>(layout.size);
    Eigen::MatrixXd f = Eigen::MatrixXd::Zero(n, n);
    for (std::size_t p = 0; p < patches.size(); ++p) {
        const Eigen::MatrixXd block =
            fairness_matrix(patches[p].degree_u(), patches[p].degree_v(), order);
        const auto at = static_cast<Eigen::Index>(layout.offset[p]);
        f.block(at, at, block.rows(), block.cols()) = block;
    }
    const double mean_diagonal = n > 0 ? f.diagonal().mean() : 0.0;
    if (mean_diagonal > 0.0) {
        f /= mean_diagonal;
    }
    return f;
}

std::vector<ConstraintResidual> conflicts(const SolveProblem& problem, const AxisSystem& system) {
    // The least-squares residual. The residual of the solution through an
    // independent subset of the equations lands entirely on the equations left
    // out, which names only one side of a contradiction; this one spreads over
    // every constraint taking part.
    Eigen::CompleteOrthogonalDecomposition<Eigen::MatrixXd> cod(system.c);
    cod.setThreshold(tol::kConstraintRank);
    const Eigen::VectorXd residual = system.c * cod.solve(system.d) - system.d;

    std::map<std::string, double> by_label;
    for (Eigen::Index r = 0; r < residual.size(); ++r) {
        const std::string& label =
            problem.constraints[system.source[static_cast<std::size_t>(r)]].label;
        by_label[label] = std::max(by_label[label], std::abs(residual(r)));
    }
    std::vector<std::pair<std::string, double>> sorted(by_label.begin(), by_label.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) {
        return x.second != y.second ? x.second > y.second : x.first < y.first;
    });
    std::vector<ConstraintResidual> worst;
    for (std::size_t k = 0; k < std::min(kReportedConflicts, sorted.size()); ++k) {
        if (sorted[k].second <= 0.0 || sorted[k].second < kConflictShare * sorted[0].second) {
            break;
        }
        worst.push_back(
            {sorted[k].first.empty() ? "unlabelled" : sorted[k].first, sorted[k].second});
    }
    return worst;
}

std::string describe(const std::vector<ConstraintResidual>& conflicts) {
    std::string text;
    for (const ConstraintResidual& c : conflicts) {
        text += fmt::format("{}{} ({:.3g} mm)", text.empty() ? "" : ", ", c.label, c.max_residual);
    }
    return text;
}

} // namespace

SolveResult solve(const SolveProblem& problem) {
    const Layout layout(problem.patches);
    const auto n = static_cast<Eigen::Index>(layout.size);
    const SolveWeights& w = problem.weights;
    if (w.fairness < 0.0 || w.move < 0.0) {
        throw SolveError("solver weights must be non-negative");
    }

    // Lengths in this problem: used to scale the consistency tolerance, so
    // that a millimetre-scale body panel and a unit test patch are judged alike.
    double scale = 1.0;
    for (const BezierPatch& p : problem.patches) {
        for (const Vec3& point : p.control_points()) {
            scale = std::max(scale, point.cwiseAbs().maxCoeff());
        }
    }

    Eigen::MatrixXd energy = w.fairness * fairness(problem.patches, layout, w.order) +
                             (w.move + tol::kSolverTieBreak) * Eigen::MatrixXd::Identity(n, n);

    // Fit: each point's |S(u, v) - target|^2 is a quadratic in its patch's
    // control points, with the Bernstein products at (u, v) as coefficients.
    Eigen::Matrix3Xd fit_pull = Eigen::Matrix3Xd::Zero(3, n);
    if (!problem.fit.points.empty()) {
        if (problem.fit.weight < 0.0) {
            throw SolveError("the fit weight must be non-negative");
        }
        Eigen::MatrixXd fit = Eigen::MatrixXd::Zero(n, n);
        for (const FitPoint& point : problem.fit.points) {
            if (point.patch >= problem.patches.size() || !(point.u >= 0.0 && point.u <= 1.0) ||
                !(point.v >= 0.0 && point.v <= 1.0)) {
                throw SolveError(
                    fmt::format("fit point at ({}, {}) on patch {} is outside the problem",
                                point.u,
                                point.v,
                                point.patch));
            }
            const BezierPatch& patch = problem.patches[point.patch];
            const std::vector<double> bu = bernstein_values(patch.degree_u(), point.u);
            const std::vector<double> bv = bernstein_values(patch.degree_v(), point.v);
            std::vector<std::pair<Eigen::Index, double>> terms;
            for (std::size_t i = 0; i < bu.size(); ++i) {
                for (std::size_t j = 0; j < bv.size(); ++j) {
                    terms.emplace_back(layout.index(problem.patches, {point.patch, i, j}),
                                       bu[i] * bv[j]);
                }
            }
            for (const auto& [k, bk] : terms) {
                for (const auto& [l, bl] : terms) {
                    fit(k, l) += bk * bl;
                }
                fit_pull.col(k) += bk * point.target;
            }
        }
        const double mean_diagonal = fit.diagonal().sum() / static_cast<double>(n);
        if (mean_diagonal > 0.0) {
            energy += (problem.fit.weight / mean_diagonal) * fit;
            fit_pull *= problem.fit.weight / mean_diagonal;
        }
    }
    for (const SoftTarget& t : problem.targets) {
        if (t.weight < 0.0) {
            throw SolveError("soft target weights must be non-negative");
        }
        const Eigen::Index k = layout.index(problem.patches, t.cv);
        energy(k, k) += t.weight;
    }

    SolveResult result;
    result.patches = problem.patches;
    result.unknowns = 3 * layout.size;

    for (int axis = 0; axis < 3; ++axis) {
        Eigen::VectorXd previous(n);
        for (std::size_t p = 0; p < problem.patches.size(); ++p) {
            const auto& points = problem.patches[p].control_points();
            for (std::size_t k = 0; k < points.size(); ++k) {
                previous(static_cast<Eigen::Index>(layout.offset[p] + k)) = points[k](axis);
            }
        }

        const AxisSystem system = axis_system(problem, layout, axis);
        const Eigen::Index m = system.c.rows();
        result.equations += static_cast<std::size_t>(m);

        // Constraints C x = d. With C^T P = Q R (column-pivoted QR, rank r),
        // the first r columns of Q span the row space of C and the rest its
        // null space N: x = x_p + N z, where x_p is the minimum-norm solution.
        Eigen::VectorXd particular = Eigen::VectorXd::Zero(n);
        Eigen::MatrixXd null_space = Eigen::MatrixXd::Identity(n, n);
        if (m > 0) {
            Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(system.c.transpose());
            qr.setThreshold(tol::kConstraintRank);
            const Eigen::Index r = qr.rank();
            result.independent_equations += static_cast<std::size_t>(r);
            const Eigen::MatrixXd q = qr.householderQ();
            const Eigen::VectorXd permuted = qr.colsPermutation().transpose() * system.d;
            const Eigen::MatrixXd r1 =
                qr.matrixR().topLeftCorner(r, r).triangularView<Eigen::Upper>();
            const Eigen::VectorXd y =
                r1.transpose().triangularView<Eigen::Lower>().solve(permuted.head(r));
            particular = q.leftCols(r) * y;
            null_space = q.rightCols(n - r);

            const Eigen::VectorXd residual = system.c * particular - system.d;
            if (residual.cwiseAbs().maxCoeff() > tol::kConstraintConsistency * scale) {
                std::vector<ConstraintResidual> worst = conflicts(problem, system);
                const std::string message =
                    fmt::format("the constraints contradict each other on "
                                "the {} axis; the largest conflicts are: {}",
                                "xyz"[axis],
                                describe(worst));
                throw SolveError(message, std::move(worst));
            }
        }

        // Minimise over the free directions: N^T H N z = N^T (b - H x_p),
        // with b the pull towards the current control points.
        Eigen::VectorXd solution = particular;
        if (null_space.cols() > 0) {
            Eigen::VectorXd pull = (w.move + tol::kSolverTieBreak) * previous;
            pull += fit_pull.row(axis).transpose();
            for (const SoftTarget& t : problem.targets) {
                pull(layout.index(problem.patches, t.cv)) += t.weight * t.target(axis);
            }
            const Eigen::MatrixXd reduced = null_space.transpose() * energy * null_space;
            const Eigen::VectorXd rhs = null_space.transpose() * (pull - energy * particular);
            const Eigen::LDLT<Eigen::MatrixXd> ldlt(reduced);
            if (ldlt.info() != Eigen::Success) {
                throw SolveError("the reduced energy could not be factorised");
            }
            solution += null_space * ldlt.solve(rhs);
            const Eigen::VectorXd pivots = ldlt.vectorD().cwiseAbs();
            if (pivots.minCoeff() > 0.0) {
                result.condition_estimate =
                    std::max(result.condition_estimate, pivots.maxCoeff() / pivots.minCoeff());
            }
        }

        // Residuals per label, in the normalised (length) units.
        const Eigen::VectorXd residual =
            m > 0 ? Eigen::VectorXd(system.c * solution - system.d) : Eigen::VectorXd();
        for (Eigen::Index r = 0; r < residual.size(); ++r) {
            const std::string& label =
                problem.constraints[system.source[static_cast<std::size_t>(r)]].label;
            auto it = std::find_if(result.residuals.begin(),
                                   result.residuals.end(),
                                   [&](const ConstraintResidual& c) { return c.label == label; });
            if (it == result.residuals.end()) {
                result.residuals.push_back({label, 0.0});
                it = std::prev(result.residuals.end());
            }
            it->max_residual = std::max(it->max_residual, std::abs(residual(r)));
            result.max_residual = std::max(result.max_residual, std::abs(residual(r)));
        }

        for (std::size_t p = 0; p < result.patches.size(); ++p) {
            BezierPatch& patch = result.patches[p];
            for (std::size_t i = 0; i < patch.num_u(); ++i) {
                for (std::size_t j = 0; j < patch.num_v(); ++j) {
                    patch.control_point(i, j)(axis) = solution(
                        static_cast<Eigen::Index>(layout.offset[p] + i * patch.num_v() + j));
                }
            }
        }
    }
    return result;
}

} // namespace ada
