// n2s::project_to_surface (copied from nurbs2subd for M11) on ADA patches.
// Points are built as a footpoint plus an offset along the normal there, so
// the right answer is known: the footpoint's parameters and the offset.

#include "ada/analysis/differential.hpp"
#include "ada/geom/convert.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "n2s/nurbs/projection.hpp"

#include "support/panels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

using ada::Vec3;
using n2s::ProjectionStatus;

namespace {

/// Well inside the radius of curvature of the domed panel (metres), so the
/// footpoint stays the closest point.
constexpr double kOffsetMm = 5.0;
constexpr double kSameParameter = 1e-9;
constexpr double kSameMm = 1e-9;

} // namespace

TEST_CASE("projection finds the footpoint of a point off the surface", "[geom][projection]") {
    const ada::BezierPatch patch = ada::test::domed_panel();
    const ada::PatchEvaluator evaluator(patch);
    const n2s::NurbsSurface surface = ada::to_nurbs(patch);
    for (const double u : {0.1, 0.35, 0.5, 0.8}) {
        for (const double v : {0.15, 0.5, 0.9}) {
            const ada::PatchDerivatives d = evaluator.derivatives(u, v);
            const Vec3 target = d.position() + kOffsetMm * *ada::unit_normal(d);

            const n2s::SurfaceProjection p = n2s::project_to_surface(surface, target);
            CHECK(p.status == ProjectionStatus::Perpendicular);
            CHECK(std::abs(p.u - u) < kSameParameter);
            CHECK(std::abs(p.v - v) < kSameParameter);
            CHECK(std::abs(p.distance - kOffsetMm) < kSameMm);
            CHECK_FALSE(p.on_boundary);
        }
    }
}

TEST_CASE("a warm start reaches the same footpoint without the grid search", "[geom][projection]") {
    const ada::BezierPatch patch = ada::test::domed_panel();
    const ada::PatchEvaluator evaluator(patch);
    const n2s::NurbsSurface surface = ada::to_nurbs(patch);
    const ada::PatchDerivatives d = evaluator.derivatives(0.42, 0.61);
    const Vec3 target = d.position() - kOffsetMm * *ada::unit_normal(d);

    n2s::ProjectionOptions warm;
    warm.start = std::array<double, 2>{0.40, 0.63};
    const n2s::SurfaceProjection p = n2s::project_to_surface(surface, target, warm);
    CHECK(p.status == ProjectionStatus::Perpendicular);
    CHECK(std::abs(p.u - 0.42) < kSameParameter);
    CHECK(std::abs(p.v - 0.61) < kSameParameter);

    // A start outside the domain is clamped into it, not trusted.
    warm.start = std::array<double, 2>{-3.0, 7.0};
    const n2s::SurfaceProjection clamped = n2s::project_to_surface(surface, target, warm);
    CHECK(clamped.converged());
}

TEST_CASE("a point beyond an edge projects onto that edge", "[geom][projection]") {
    const ada::BezierPatch patch = ada::test::domed_panel();
    const ada::PatchEvaluator evaluator(patch);
    const n2s::NurbsSurface surface = ada::to_nurbs(patch);
    // Continue the surface past u = 1 along its tangent: the closest point is
    // on the UMax edge, where the clamp holds the iterate.
    const ada::PatchDerivatives d = evaluator.derivatives(1.0, 0.5);
    const Vec3 target = d.position() + 50.0 * d.du().normalized();

    const n2s::SurfaceProjection p = n2s::project_to_surface(surface, target);
    CHECK(p.status == ProjectionStatus::ClampedToBoundary);
    CHECK(p.on_boundary);
    CHECK(p.u == 1.0);
    CHECK(std::abs(p.v - 0.5) < 1e-6);
}
