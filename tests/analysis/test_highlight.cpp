#include "ada/analysis/cv_hull.hpp"
#include "ada/analysis/highlight.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "support/polynomial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <stdexcept>
#include <vector>

using ada::BezierPatch;
using ada::LightPlane;
using ada::PatchEvaluator;
using ada::Vec3;
using namespace ada::test;

namespace {

constexpr std::size_t kGrid = 49;

/// Highlight quantities are in mm; exact inputs reproduce them to rounding.
constexpr double kExactMm = 1e-9;

// z = y^2 / 2R over x in [0, kLength], y in [-kHalfWidth, kHalfWidth].
constexpr double kLength = 1200.0;
constexpr double kHalfWidth = 500.0;
constexpr double kRadius = 900.0;
// Light plane z = kHeight; below kRadius, so s(y) is monotonic and every
// highlight level appears exactly once.
constexpr double kHeight = 400.0;
constexpr double kSpacing = 60.0;

/// The parabolic cylinder exactly, as a degree (1, 2) patch: x = L u,
/// y = a (2v - 1), z = y^2 / 2R. Its normals do not change along x.
BezierPatch parabolic_cylinder() {
    const double c = kHalfWidth * kHalfWidth / (2.0 * kRadius);
    PowerPatch power{1, 2, std::vector<Vec3>(6, Vec3::Zero())};
    power.coefficients[0] = Vec3(0.0, -kHalfWidth, c);             // (0, 0)
    power.coefficients[1] = Vec3(0.0, 2.0 * kHalfWidth, -4.0 * c); // (0, 1)
    power.coefficients[2] = Vec3(0.0, 0.0, 4.0 * c);               // (0, 2)
    power.coefficients[3] = Vec3(kLength, 0.0, 0.0);               // (1, 0)
    return power.to_bezier();
}

LightPlane overhead_light() {
    return ada::make_light_plane(Vec3(0, 0, kHeight), Vec3::UnitZ(), Vec3::UnitX(), kSpacing);
}

/// Closed form for the cylinder: the normal line from (x, y, y^2/2R) has
/// direction (0, -y/R, 1) and meets z = h at y - (y/R)(h - y^2/2R).
double analytic_s(double y) {
    return y - (y / kRadius) * (kHeight - y * y / (2.0 * kRadius));
}

} // namespace

TEST_CASE("the highlight parameter is where the normal line meets the light plane",
          "[analysis][highlight]") {
    const LightPlane light = overhead_light();
    CHECK(std::abs(ada::highlight_parameter(light, Vec3(0, 0, 0), Vec3::UnitZ())) < kExactMm);
    CHECK(std::abs(ada::highlight_parameter(light, Vec3(7, 50, 0), Vec3::UnitZ()) - 50.0) <
          kExactMm);

    const double theta = 0.3;
    const Vec3 tilted(0.0, std::sin(theta), std::cos(theta));
    CHECK(std::abs(ada::highlight_parameter(light, Vec3::Zero(), tilted) -
                   kHeight * std::tan(theta)) < kExactMm);

    // A normal lying in a plane parallel to the light plane never meets it.
    CHECK(std::isnan(ada::highlight_parameter(light, Vec3::Zero(), Vec3::UnitY())));
}

TEST_CASE("highlights on a parabolic cylinder are straight rulings at the analytic positions",
          "[analysis][highlight]") {
    const PatchEvaluator patch(parabolic_cylinder());
    const LightPlane light = overhead_light();

    const ada::ScalarField field = ada::highlight_field(patch, light);
    for (const double u : sample_parameters()) {
        for (const double v : sample_parameters()) {
            const double y = patch.evaluate(u, v).y();
            CHECK(std::abs(field(u, v) - analytic_s(y)) < kExactMm);
        }
    }

    const auto lines = ada::highlight_lines(patch, light, kGrid);
    std::size_t expected = 0;
    for (double k = std::ceil(analytic_s(-kHalfWidth) / kSpacing);
         k * kSpacing <= analytic_s(kHalfWidth);
         k += 1.0) {
        ++expected;
    }
    REQUIRE(lines.size() == expected);
    for (const ada::ContourLine& line : lines) {
        const double y0 = patch.evaluate(line.uv.front().x(), line.uv.front().y()).y();
        CHECK(std::abs(analytic_s(y0) - line.level) < kExactMm);
        for (const Eigen::Vector2d& p : line.uv) {
            CHECK(std::abs(patch.evaluate(p.x(), p.y()).y() - y0) < kExactMm);
        }
        const ada::HighlightLineShape shape = ada::analyze_highlight_line(patch, line);
        CHECK(shape.inflections == 0);
        CHECK(shape.max_abs_curvature < kExactMm);
    }
}

TEST_CASE("a bump puts wiggles into the highlights", "[analysis][highlight]") {
    const BezierPatch clean = parabolic_cylinder().elevated_u(3).elevated_v(2);
    BezierPatch bumped = clean;
    bumped.control_point(2, 2).z() += 15.0;

    const LightPlane light = overhead_light();
    const ada::HighlightSummary before =
        ada::analyze_highlights(PatchEvaluator(clean), light, kGrid);
    const ada::HighlightSummary after =
        ada::analyze_highlights(PatchEvaluator(bumped), light, kGrid);
    CHECK(before.max_inflections == 0);
    CHECK(after.max_inflections >= 2);
    CHECK(after.max_curvature_rate > 100.0 * before.max_curvature_rate);
}

TEST_CASE("degenerate light planes are rejected", "[analysis][highlight]") {
    CHECK_THROWS_AS(ada::make_light_plane(Vec3::Zero(), Vec3::UnitZ(), Vec3::UnitZ(), 10.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(ada::make_light_plane(Vec3::Zero(), Vec3::UnitZ(), Vec3::UnitX(), 0.0),
                    std::invalid_argument);
}

TEST_CASE("the isophote field is N . L", "[analysis][highlight]") {
    const PatchEvaluator patch(parabolic_cylinder());
    const ada::ScalarField field = ada::isophote_field(patch, Vec3(0, 0, 2));
    // On the ridge line y = 0 the normal is +z; at y the normal is
    // (0, -y/R, 1) normalised.
    const double y = 0.6 * kHalfWidth - kHalfWidth * 0.0;
    CHECK(std::abs(field(0.3, 0.5) - 1.0) < kExactMm);
    const double v = 0.5 + y / (2.0 * kHalfWidth);
    CHECK(std::abs(field(0.3, v) - 1.0 / std::hypot(1.0, y / kRadius)) < kExactMm);
}

TEST_CASE("CV rows of a clean cylinder neither turn back nor zig-zag", "[analysis][hull]") {
    const ada::CvHullSummary clean =
        ada::analyze_cv_hull(PatchEvaluator(parabolic_cylinder().elevated_u(3).elevated_v(2)));
    CHECK(clean.rows == 10);
    CHECK(clean.zigzag_rows == 0);
    CHECK(clean.max_row_inflections == 0);
}

TEST_CASE("alternating control points make a zig-zag row", "[analysis][hull]") {
    BezierPatch patch = parabolic_cylinder().elevated_u(3).elevated_v(2);
    patch.control_point(2, 1).z() += 6.0;
    patch.control_point(2, 2).z() -= 6.0;
    patch.control_point(2, 3).z() += 6.0;
    const ada::CvHullSummary hull = ada::analyze_cv_hull(PatchEvaluator(patch));
    CHECK(hull.zigzag_rows >= 1);
    CHECK(hull.max_row_inflections >= 2);
}

TEST_CASE("zig-zags out of the surface and within it are both caught", "[analysis][hull]") {
    // On a flat panel the normal is +z everywhere, so z offsets bend rows only
    // out of the surface and y offsets only within it: each component has to
    // catch its own zig-zag.
    const BezierPatch flat =
        BezierPatch(1, 1, {Vec3(0, 0, 0), Vec3(0, 500, 0), Vec3(800, 0, 0), Vec3(800, 500, 0)})
            .elevated_u(4)
            .elevated_v(2);
    REQUIRE(ada::analyze_cv_hull(PatchEvaluator(flat)).zigzag_rows == 0);

    BezierPatch vertical = flat;
    BezierPatch lateral = flat;
    for (std::size_t i = 1; i <= 4; ++i) {
        const double offset = i % 2 == 0 ? 4.0 : -4.0;
        vertical.control_point(i, 1).z() += offset;
        lateral.control_point(i, 1).y() += offset;
    }
    CHECK(ada::analyze_cv_hull(PatchEvaluator(vertical)).zigzag_rows >= 1);
    CHECK(ada::analyze_cv_hull(PatchEvaluator(lateral)).zigzag_rows >= 1);
}

TEST_CASE("a straight CV row does not turn", "[analysis][hull]") {
    // A bilinear plane elevated to bicubic: every row is a straight line.
    const BezierPatch plane(
        1, 1, {Vec3(0, 0, 0), Vec3(0, 500, 0), Vec3(800, 0, 0), Vec3(800, 500, 40)});
    const ada::CvHullSummary hull =
        ada::analyze_cv_hull(PatchEvaluator(plane.elevated_u(2).elevated_v(2)));
    CHECK(hull.max_turning_angle_deg < 1e-9);
    CHECK(hull.zigzag_rows == 0);
}
