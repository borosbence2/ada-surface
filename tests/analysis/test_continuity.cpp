#include "ada/analysis/continuity.hpp"
#include "ada/analysis/differential.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "support/panels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using ada::BezierPatch;
using ada::Edge;
using ada::EdgeContinuity;
using ada::PatchEvaluator;
using ada::SymmetryContinuity;
using ada::Vec3;
using namespace ada::test;

namespace {

constexpr std::size_t kSamples = 33;

// What "measures as zero" means for each quantity on a ~1 m panel. Each sits
// far below the corresponding Class-A budget (0.001 mm, 0.01 deg, 1 %) and far
// above the rounding noise of the measurement.
constexpr double kZeroGapMm = 1e-8;
constexpr double kZeroAngleDeg = 1e-8;
constexpr double kZeroCurvatureDeviation = 1e-7;

// The default budgets the perturbation tests must clearly break.
constexpr double kG0BudgetMm = 0.001;
constexpr double kG1BudgetDeg = 0.01;
constexpr double kG2Budget = 0.01;

EdgeContinuity across(const BezierPatch& a, const BezierPatch& b, Edge eb = Edge::VMin) {
    return ada::measure_edge(PatchEvaluator(a), Edge::VMax, PatchEvaluator(b), eb, kSamples);
}

void require_g2(const EdgeContinuity& c) {
    CHECK(c.undefined_samples == 0);
    CHECK(c.max_gap < kZeroGapMm);
    CHECK(c.max_normal_angle_deg < kZeroAngleDeg);
    CHECK(c.max_curvature_deviation < kZeroCurvatureDeviation);
    CHECK_FALSE(c.fold);
}

} // namespace

TEST_CASE("a patch split in two is G2 across the cut", "[analysis][continuity]") {
    const auto [low, high] = domed_panel().split_v(0.4);
    require_g2(ada::measure_edge(
        PatchEvaluator(low), Edge::VMax, PatchEvaluator(high), Edge::VMin, kSamples));
}

TEST_CASE("G2 is measured geometrically, not parametrically", "[analysis][continuity]") {
    // alpha = 1.7, gamma = 0.4: the derivatives across the edge differ by a
    // reparameterisation, so a C2 check would fail, but the surface is G2.
    const BezierPatch a = domed_panel();
    require_g2(across(a, g2_neighbour(a, 1.7, 0.4)));
}

TEST_CASE("edges running in opposite directions are matched by position",
          "[analysis][continuity]") {
    const BezierPatch a = domed_panel();
    const EdgeContinuity c = across(a, flip_u(g2_neighbour(a, 1.0, 0.0)));
    require_g2(c);
}

TEST_CASE("moving a row-2 control point breaks G2 but not G1", "[analysis][continuity]") {
    const BezierPatch a = domed_panel();
    const EdgeContinuity c = across(a, nudge(g2_neighbour(a, 1.0, 0.0), 1, 2, 5.0));
    CHECK(c.max_gap < kZeroGapMm);
    CHECK(c.max_normal_angle_deg < kZeroAngleDeg);
    CHECK(c.max_curvature_deviation > kG2Budget);
    CHECK_FALSE(c.fold);
}

TEST_CASE("moving a row-1 control point breaks G1 but not G0", "[analysis][continuity]") {
    const BezierPatch a = domed_panel();
    const EdgeContinuity c = across(a, nudge(g2_neighbour(a, 1.0, 0.0), 2, 1, 5.0));
    CHECK(c.max_gap < kZeroGapMm);
    CHECK(c.max_normal_angle_deg > kG1BudgetDeg);
}

TEST_CASE("moving a boundary control point opens a gap", "[analysis][continuity]") {
    const BezierPatch a = domed_panel();
    const EdgeContinuity c = across(a, nudge(g2_neighbour(a, 1.0, 0.0), 1, 0, 2.0));
    CHECK(c.max_gap > kG0BudgetMm);
    CHECK(c.max_gap < 2.0);
}

TEST_CASE("a patch that doubles back is a fold, even with parallel normals",
          "[analysis][continuity]") {
    const BezierPatch a = domed_panel();
    const EdgeContinuity c = across(a, g2_neighbour(a, 1.0, 0.0, -1.0));
    CHECK(c.max_normal_angle_deg < kZeroAngleDeg); // the angle alone would pass it
    CHECK(c.orientation_flipped);
    CHECK(c.fold);
}

TEST_CASE("a mirror-G1 patch measures clean against the centre plane", "[analysis][symmetry]") {
    const SymmetryContinuity s =
        ada::measure_symmetry(PatchEvaluator(symmetric_panel()), Edge::VMin, kSamples);
    CHECK(s.undefined_samples == 0);
    CHECK(s.max_plane_distance < kZeroGapMm);
    CHECK(s.max_normal_tilt_deg < kZeroAngleDeg);
}

TEST_CASE("tilting row 1 or lifting row 0 off the plane breaks symmetry", "[analysis][symmetry]") {
    BezierPatch tilted = symmetric_panel();
    tilted.control_point(2, 1).z() += 4.0;
    CHECK(ada::measure_symmetry(PatchEvaluator(tilted), Edge::VMin, kSamples).max_normal_tilt_deg >
          kG1BudgetDeg);

    BezierPatch lifted = symmetric_panel();
    lifted.control_point(1, 0).y() += 2.0;
    const SymmetryContinuity s =
        ada::measure_symmetry(PatchEvaluator(lifted), Edge::VMin, kSamples);
    CHECK(s.max_plane_distance > kG0BudgetMm);
}

TEST_CASE("mirror G1 implies mirror G2", "[analysis][symmetry]") {
    // The argument in continuity.hpp, checked numerically: the patch and its
    // mirror image are measured as an ordinary edge pair, and G2 holds although
    // nothing constrains row 2.
    const BezierPatch a = symmetric_panel();
    const EdgeContinuity c = ada::measure_edge(
        PatchEvaluator(a), Edge::VMin, PatchEvaluator(mirror_y(a)), Edge::VMin, kSamples);
    require_g2(c);

    // Without mirror G1 the pair is not even G1.
    BezierPatch tilted = a;
    tilted.control_point(2, 1).z() += 4.0;
    const EdgeContinuity broken = ada::measure_edge(
        PatchEvaluator(tilted), Edge::VMin, PatchEvaluator(mirror_y(tilted)), Edge::VMin, kSamples);
    CHECK(broken.max_normal_angle_deg > kG1BudgetDeg);
}
