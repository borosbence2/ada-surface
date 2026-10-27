#include "ada/analysis/continuity.hpp"
#include "ada/geom/patch_evaluator.hpp"
#include "ada/solve/fairness.hpp"
#include "ada/solve/solver.hpp"

#include "support/panels.hpp"
#include "support/polynomial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using ada::BezierPatch;
using ada::Constraints;
using ada::Continuity;
using ada::CvRef;
using ada::Edge;
using ada::EdgeJoin;
using ada::FairnessOrder;
using ada::PatchEvaluator;
using ada::SolveProblem;
using ada::SolveResult;
using ada::Vec3;
using namespace ada::test;

namespace {

constexpr unsigned kSeed = 20261023;
constexpr std::size_t kEdgeSamples = 33;

/// A determined solve reproduces its oracle to rounding on a ~1 m part.
constexpr double kSameMm = 1e-7;
/// Constraint residuals after a solve: well below any Class-A budget.
constexpr double kResidualMm = 1e-7;

double max_difference(const BezierPatch& a, const BezierPatch& b) {
    double worst = 0.0;
    for (std::size_t k = 0; k < a.control_points().size(); ++k) {
        worst = std::max(worst, (a.control_points()[k] - b.control_points()[k]).norm());
    }
    return worst;
}

double total_movement(const std::vector<BezierPatch>& before,
                      const std::vector<BezierPatch>& after) {
    double sum = 0.0;
    for (std::size_t p = 0; p < before.size(); ++p) {
        for (std::size_t k = 0; k < before[p].control_points().size(); ++k) {
            sum += (before[p].control_points()[k] - after[p].control_points()[k]).squaredNorm();
        }
    }
    return std::sqrt(sum);
}

void fix_all(Constraints& c,
             std::size_t index,
             const BezierPatch& patch,
             const std::string& label) {
    for (std::size_t i = 0; i < patch.num_u(); ++i) {
        for (std::size_t j = 0; j < patch.num_v(); ++j) {
            ada::add_fixed(c, {index, i, j}, patch.control_point(i, j), label);
        }
    }
}

void fix_boundary(Constraints& c, std::size_t index, const BezierPatch& patch) {
    for (const Edge e : {Edge::UMin, Edge::UMax, Edge::VMin, Edge::VMax}) {
        ada::add_fixed_row(c, index, patch, e, 0, "boundary");
    }
}

/// z = (x^2 + y^2) / 2R over 800 x 600 mm, total degree 2, as a bicubic.
BezierPatch dome() {
    PowerPatch power{2, 2, std::vector<Vec3>(9, Vec3::Zero())};
    power.coefficients[1 * 3 + 0] = Vec3(800, 0, 0);
    power.coefficients[0 * 3 + 1] = Vec3(0, 600, 0);
    power.coefficients[2 * 3 + 0] = Vec3(0, 0, 800.0 * 800.0 / 4000.0);
    power.coefficients[0 * 3 + 2] = Vec3(0, 0, 600.0 * 600.0 / 4000.0);
    return power.to_bezier().elevated_u(1).elevated_v(1);
}

} // namespace

TEST_CASE("E3 fairing rebuilds a total-degree-2 surface from its boundary", "[solve][solver]") {
    // The boundary pins the surface; E3 is zero on the original, so the
    // original is the unique minimiser whatever the interior starts as.
    const BezierPatch original = dome();
    BezierPatch scrambled = original;
    std::mt19937 rng(kSeed);
    std::uniform_real_distribution<double> jitter(-30.0, 30.0);
    for (std::size_t i = 1; i <= 2; ++i) {
        for (std::size_t j = 1; j <= 2; ++j) {
            scrambled.control_point(i, j) += Vec3(jitter(rng), jitter(rng), jitter(rng));
        }
    }
    SolveProblem problem{{scrambled}, {}, {}, {}, {}};
    fix_boundary(problem.constraints, 0, scrambled);
    const SolveResult result = ada::solve(problem);
    CHECK(max_difference(result.patches[0], original) < kSameMm);
    CHECK(result.max_residual < kResidualMm);
    CHECK(result.independent_equations < result.equations); // corners appear in two rows
}

TEST_CASE("E2 fairing finds the lowest-energy interior", "[solve][solver]") {
    const BezierPatch original = dome();
    SolveProblem problem{{original}, {}, {1.0, FairnessOrder::E2, 0.0}, {}, {}};
    fix_boundary(problem.constraints, 0, original);
    const SolveResult result = ada::solve(problem);
    const Eigen::MatrixXd f = ada::fairness_matrix(3, 3, FairnessOrder::E2);
    auto energy = [&f](const BezierPatch& p) {
        double e = 0.0;
        for (int axis = 0; axis < 3; ++axis) {
            Eigen::VectorXd x(16);
            for (std::size_t k = 0; k < 16; ++k) {
                x(static_cast<Eigen::Index>(k)) = p.control_points()[k](axis);
            }
            e += x.dot(f * x);
        }
        return e;
    };
    CHECK(energy(result.patches[0]) <= energy(original));
    CHECK(max_difference(result.patches[0], original) > 1.0); // E2 does not keep the dome
}

TEST_CASE("with fairness off, full constraints rebuild the hand-built G2 neighbour",
          "[solve][solver]") {
    // Rows 0-2 of B follow from A through the G0/G1/G2 equations; row 3 is
    // fixed. Nothing is left to optimise, so the result must be the oracle.
    for (const auto& [alpha, gamma] :
         std::vector<std::pair<double, double>>{{1.0, 0.0}, {1.7, 0.4}}) {
        const BezierPatch a = domed_panel();
        const BezierPatch expected = g2_neighbour(a, alpha, gamma);
        BezierPatch start = expected;
        for (std::size_t i = 0; i < start.num_u(); ++i) {
            for (std::size_t j = 0; j < start.num_v(); ++j) {
                start.control_point(i, j) += Vec3(3.0, -2.0, 7.0);
            }
        }
        SolveProblem problem{{a, start}, {}, {0.0, FairnessOrder::E3, 0.0}, {}, {}};
        fix_all(problem.constraints, 0, a, "A");
        ada::add_fixed_row(problem.constraints, 1, expected, Edge::VMax, 0, "B row 3");
        ada::add_edge_join(problem.constraints,
                           problem.patches,
                           {0, Edge::VMax, 1, Edge::VMin, Continuity::G2, alpha, gamma},
                           "G2 A~B");
        const SolveResult result = ada::solve(problem);
        INFO("alpha " << alpha << ", gamma " << gamma);
        CHECK(max_difference(result.patches[1], expected) < kSameMm);
        CHECK(result.max_residual < kResidualMm);
    }
}

TEST_CASE("least-change repair undoes exactly the broken control point", "[solve][solver]") {
    // hood-g2-broken raised one rear row-2 control point by 4 mm. Projecting
    // back onto the G2 constraint with the front fixed must undo that and
    // leave every other control point where it is.
    const ada::Scene broken = hood_scene_g2_broken();
    const ada::Scene good = hood_scene();
    SolveProblem problem{{broken.patches[0].patch, broken.patches[1].patch},
                         {},
                         {0.0, FairnessOrder::E3, 1.0},
                         {},
                         {}};
    fix_all(problem.constraints, 0, problem.patches[0], "front");
    ada::add_edge_join(problem.constraints,
                       problem.patches,
                       {0, Edge::VMax, 1, Edge::VMin, Continuity::G2},
                       "G2 front~rear");
    const SolveResult result = ada::solve(problem);
    CHECK(max_difference(result.patches[1], good.patches[1].patch) < kSameMm);

    const ada::EdgeContinuity c = ada::measure_edge(PatchEvaluator(result.patches[0]),
                                                    Edge::VMax,
                                                    PatchEvaluator(result.patches[1]),
                                                    Edge::VMin,
                                                    kEdgeSamples);
    CHECK(c.max_curvature_deviation < 1e-7);
}

TEST_CASE("the move weight trades fairness against staying put", "[solve][solver]") {
    // The G1-only hood: rear row 2 sits 15 mm high. The rear is elevated to
    // degree 5 across the joint (same surface) so that, with G2 pinning rows
    // 0-2 and the far row held, rows 3 and 4 are free. A larger move weight
    // must keep them closer to where they were; both runs must satisfy the
    // constraints.
    const ada::Scene scene = hood_scene_g1_only();
    const BezierPatch rear = scene.patches[1].patch.elevated_v(2);
    auto run = [&](double move) {
        SolveProblem problem{
            {scene.patches[0].patch, rear}, {}, {1.0, FairnessOrder::E3, move}, {}, {}};
        fix_all(problem.constraints, 0, problem.patches[0], "front");
        ada::add_fixed_row(problem.constraints, 1, rear, Edge::VMax, 0, "rear far row");
        ada::add_edge_join(problem.constraints,
                           problem.patches,
                           {0, Edge::VMax, 1, Edge::VMin, Continuity::G2},
                           "G2");
        ada::add_symmetry(problem.constraints, 1, rear, Edge::UMin, Continuity::G1, "Y0 rear");
        const SolveResult result = ada::solve(problem);
        CHECK(result.max_residual < kResidualMm);
        return total_movement(problem.patches, result.patches);
    };
    const double stiff = run(10.0);
    const double loose = run(0.01);
    INFO("movement with move weight 10: " << stiff << " mm, with 0.01: " << loose << " mm");
    CHECK(stiff < loose);
}

TEST_CASE("the symmetry constraint makes an edge mirror-G1", "[solve][solver]") {
    BezierPatch tilted = symmetric_panel();
    tilted.control_point(2, 1).z() += 4.0;
    tilted.control_point(1, 0).y() += 1.5;
    SolveProblem problem{{tilted}, {}, {0.0, FairnessOrder::E3, 1.0}, {}, {}};
    ada::add_symmetry(problem.constraints, 0, tilted, Edge::VMin, Continuity::G2, "Y0");
    const SolveResult result = ada::solve(problem);
    const ada::SymmetryContinuity s =
        ada::measure_symmetry(PatchEvaluator(result.patches[0]), Edge::VMin, kEdgeSamples);
    CHECK(s.max_plane_distance < kResidualMm);
    CHECK(s.max_normal_tilt_deg < 1e-7);
}

TEST_CASE("contradicting constraints are reported by label", "[solve][solver]") {
    const BezierPatch a = domed_panel();
    BezierPatch b = g2_neighbour(a, 1.0, 0.0);
    SolveProblem problem{{a, b}, {}, {}, {}, {}};
    fix_all(problem.constraints, 0, a, "front fixed");
    // Hold B's boundary row 2 mm away from A's, then ask for G0 between them.
    for (std::size_t i = 0; i <= 3; ++i) {
        ada::add_fixed(problem.constraints,
                       {1, i, 0},
                       b.control_point(i, 0) + Vec3(0, 0, 2.0),
                       "rear row held");
    }
    ada::add_edge_join(problem.constraints,
                       problem.patches,
                       {0, Edge::VMax, 1, Edge::VMin, Continuity::G0},
                       "G0 front~rear");
    try {
        static_cast<void>(ada::solve(problem));
        FAIL("contradiction not detected");
    } catch (const ada::SolveError& e) {
        const std::string message = e.what();
        INFO(message);
        CHECK(message.find("contradict") != std::string::npos);
        CHECK((message.find("rear row held") != std::string::npos ||
               message.find("G0 front~rear") != std::string::npos));

        // The same, as data a caller can act on: worst first, all nonzero.
        const std::vector<ada::ConstraintResidual>& conflicts = e.conflicts();
        REQUIRE_FALSE(conflicts.empty());
        bool named = false;
        for (std::size_t k = 0; k < conflicts.size(); ++k) {
            CHECK(conflicts[k].max_residual > 0.0);
            if (k > 0) {
                CHECK(conflicts[k].max_residual <= conflicts[k - 1].max_residual);
            }
            named = named || conflicts[k].label == "rear row held" ||
                    conflicts[k].label == "G0 front~rear";
        }
        CHECK(named);
    }
}

TEST_CASE("edges of different degree cannot be joined yet", "[solve][solver]") {
    const BezierPatch a = domed_panel();
    const BezierPatch b = g2_neighbour(a, 1.0, 0.0).elevated_u(1);
    Constraints c;
    CHECK_THROWS_AS(
        ada::add_edge_join(c, {a, b}, {0, Edge::VMax, 1, Edge::VMin, Continuity::G1}, "x"),
        std::invalid_argument);
}

TEST_CASE("a 3x3 network comes out G2 on every internal edge, fast", "[solve][solver]") {
    // A bicubic over 2400 x 1800 mm split into 3 x 3 patches (C-infinity
    // joins), interior control points scrambled, outer boundary held. E3 with
    // C2 joins on all 12 internal edges must give clean joins again.
    std::mt19937 rng(kSeed);
    PowerPatch power{3, 3, std::vector<Vec3>(16, Vec3::Zero())};
    power.coefficients[1 * 4 + 0] = Vec3(2400, 0, 0);
    power.coefficients[0 * 4 + 1] = Vec3(0, 1800, 0);
    power.coefficients[2 * 4 + 0] = Vec3(0, 0, -300);
    power.coefficients[0 * 4 + 2] = Vec3(0, 0, -200);
    power.coefficients[3 * 4 + 0] = Vec3(0, 0, 60);
    power.coefficients[1 * 4 + 1] = Vec3(0, 0, 40);
    const BezierPatch whole = power.to_bezier();

    std::vector<BezierPatch> columns;
    {
        auto [first, rest] = whole.split_u(1.0 / 3.0);
        auto [second, third] = rest.split_u(0.5);
        columns = {first, second, third};
    }
    std::vector<BezierPatch> grid; // index c * 3 + r
    for (const BezierPatch& column : columns) {
        auto [first, rest] = column.split_v(1.0 / 3.0);
        auto [second, third] = rest.split_v(0.5);
        grid.insert(grid.end(), {first, second, third});
    }

    std::uniform_real_distribution<double> jitter(-5.0, 5.0);
    std::vector<BezierPatch> start = grid;
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t r = 0; r < 3; ++r) {
            BezierPatch& p = start[c * 3 + r];
            for (std::size_t i = 0; i <= 3; ++i) {
                for (std::size_t j = 0; j <= 3; ++j) {
                    const bool outer = (c == 0 && i == 0) || (c == 2 && i == 3) ||
                                       (r == 0 && j == 0) || (r == 2 && j == 3);
                    if (!outer) {
                        p.control_point(i, j).z() += jitter(rng);
                    }
                }
            }
        }
    }

    SolveProblem problem{start, {}, {}, {}, {}};
    for (std::size_t c = 0; c < 3; ++c) {
        ada::add_fixed_row(problem.constraints, c * 3 + 0, grid[c * 3 + 0], Edge::VMin, 0, "outer");
        ada::add_fixed_row(problem.constraints, c * 3 + 2, grid[c * 3 + 2], Edge::VMax, 0, "outer");
        ada::add_fixed_row(problem.constraints, 0 * 3 + c, grid[0 * 3 + c], Edge::UMin, 0, "outer");
        ada::add_fixed_row(problem.constraints, 2 * 3 + c, grid[2 * 3 + c], Edge::UMax, 0, "outer");
    }
    std::vector<EdgeJoin> joins;
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t r = 0; r < 3; ++r) {
            if (c + 1 < 3)
                joins.push_back(
                    {c * 3 + r, Edge::UMax, (c + 1) * 3 + r, Edge::UMin, Continuity::G2});
            if (r + 1 < 3)
                joins.push_back({c * 3 + r, Edge::VMax, c * 3 + r + 1, Edge::VMin, Continuity::G2});
        }
    }
    REQUIRE(joins.size() == 12);
    for (const EdgeJoin& j : joins) {
        ada::add_edge_join(problem.constraints, problem.patches, j, "G2 internal");
    }

    const auto t0 = std::chrono::steady_clock::now();
    const SolveResult result = ada::solve(problem);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    INFO("solve took " << ms << " ms for " << result.unknowns << " unknowns, " << result.equations
                       << " equations");
    CHECK(result.max_residual < kResidualMm);

    for (const EdgeJoin& j : joins) {
        const ada::EdgeContinuity m = ada::measure_edge(PatchEvaluator(result.patches[j.a]),
                                                        j.edge_a,
                                                        PatchEvaluator(result.patches[j.b]),
                                                        j.edge_b,
                                                        kEdgeSamples);
        CHECK(m.max_gap < kResidualMm);
        CHECK(m.max_normal_angle_deg < 1e-6);
        CHECK(m.max_curvature_deviation < 1e-6);
        CHECK_FALSE(m.fold);
    }
#ifdef NDEBUG
    // MILESTONES M4 exit: a 3x3 network solves in under 50 ms in release.
    CHECK(ms < 50.0);
#endif
}

namespace {

/// Points of `source` on a uniform (u, v) grid, as fit points for patch 0.
std::vector<ada::FitPoint> fit_grid(const BezierPatch& source, std::size_t n) {
    const PatchEvaluator evaluator(source);
    std::vector<ada::FitPoint> points;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            const double u = static_cast<double>(i) / static_cast<double>(n - 1);
            const double v = static_cast<double>(j) / static_cast<double>(n - 1);
            points.push_back({0, u, v, evaluator.evaluate(u, v)});
        }
    }
    return points;
}

double fit_rms(const BezierPatch& patch, const std::vector<ada::FitPoint>& points) {
    const PatchEvaluator evaluator(patch);
    double sum = 0.0;
    for (const ada::FitPoint& p : points) {
        sum += (evaluator.evaluate(p.u, p.v) - p.target).squaredNorm();
    }
    return std::sqrt(sum / static_cast<double>(points.size()));
}

} // namespace

TEST_CASE("fit points pull a patch onto the surface they were sampled from",
          "[solve][solver][fit]") {
    // Data from the domed panel, a flat start, and no fairness: least squares
    // on exact data of the same degree recovers the panel.
    const BezierPatch source = domed_panel();
    std::vector<Vec3> flat = source.control_points();
    for (Vec3& p : flat) {
        p.z() = 0.0;
    }
    SolveProblem problem{{BezierPatch(3, 3, flat)}, {}, {0.0, FairnessOrder::E3, 0.0}, {}, {}};
    problem.fit = {fit_grid(source, 9), 1.0};
    const SolveResult result = ada::solve(problem);
    CHECK(max_difference(result.patches[0], source) < 1e-6);
}

TEST_CASE("the fit weight trades closeness to the data against fairness", "[solve][solver][fit]") {
    // A bump the fairness energy dislikes: the stronger the fit, the closer
    // the result follows it.
    const BezierPatch bumped = nudge(domed_panel(), 1, 2, 30.0);
    const std::vector<ada::FitPoint> data = fit_grid(bumped, 9);
    auto rms = [&](double weight) {
        SolveProblem problem{{domed_panel()}, {}, {1.0, FairnessOrder::E3, 0.0}, {}, {}};
        problem.fit = {data, weight};
        return fit_rms(ada::solve(problem).patches[0], data);
    };
    const double loose = rms(1e-3);
    const double medium = rms(1.0);
    const double tight = rms(1e3);
    CHECK(loose > medium);
    CHECK(medium > tight);
    CHECK(tight < 0.05);
}

TEST_CASE("constraints hold exactly against the fit", "[solve][solver][fit]") {
    // The data wants the VMin row 10 mm higher than the constraint allows;
    // the constraint wins exactly, the fit takes the rest.
    const BezierPatch source = domed_panel();
    std::vector<Vec3> lifted = source.control_points();
    for (Vec3& p : lifted) {
        p.z() += 10.0;
    }
    SolveProblem problem{{source}, {}, {0.0, FairnessOrder::E3, 0.0}, {}, {}};
    problem.fit = {fit_grid(BezierPatch(3, 3, lifted), 9), 1.0};
    ada::add_fixed_row(problem.constraints, 0, source, Edge::VMin, 0, "held row");
    const SolveResult result = ada::solve(problem);
    CHECK(result.max_residual < kResidualMm);
    for (std::size_t i = 0; i <= 3; ++i) {
        CHECK((result.patches[0].control_point(i, 0) - source.control_point(i, 0)).norm() <
              kResidualMm);
        CHECK(result.patches[0].control_point(i, 3).z() > source.control_point(i, 3).z() + 5.0);
    }

    SolveProblem bad{{source}, {}, {}, {}, {}};
    bad.fit = {{{3, 0.5, 0.5, Vec3::Zero()}}, 1.0};
    CHECK_THROWS_AS(ada::solve(bad), ada::SolveError);
    bad.fit = {{{0, 1.5, 0.5, Vec3::Zero()}}, 1.0};
    CHECK_THROWS_AS(ada::solve(bad), ada::SolveError);
}
