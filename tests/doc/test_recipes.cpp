#include "ada/analysis/continuity.hpp"
#include "ada/analysis/quality.hpp"
#include "ada/doc/history.hpp"
#include "ada/doc/recipes.hpp"
#include "ada/doc/scene.hpp"
#include "ada/geom/curve_fit.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "support/panels.hpp"
#include "support/polynomial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using ada::BezierCurve;
using ada::BezierPatch;
using ada::Continuity;
using ada::Edge;
using ada::PatchEvaluator;
using ada::Scene;
using ada::Vec3;
using namespace ada::test;

namespace {

constexpr std::size_t kEdgeSamples = 33;
/// A determined solve reproduces its oracle to rounding on a ~1 m part.
constexpr double kSameMm = 1e-7;
/// Continuity a solve must reach on every joint it was asked for: far inside
/// the Class-A budgets (0.001 mm, 0.01 deg, 1 %).
constexpr double kJoinGapMm = 1e-6;
constexpr double kJoinAngleDeg = 1e-6;
constexpr double kJoinCurvature = 1e-6;

/// z = (x^2 + y^2) / 2R over 800 x 600 mm: total degree 2, as a bicubic.
BezierPatch dome() {
    PowerPatch power{2, 2, std::vector<Vec3>(9, Vec3::Zero())};
    power.coefficients[1 * 3 + 0] = Vec3(800, 0, 0);
    power.coefficients[0 * 3 + 1] = Vec3(0, 600, 0);
    power.coefficients[2 * 3 + 0] = Vec3(0, 0, 800.0 * 800.0 / 4000.0);
    power.coefficients[0 * 3 + 2] = Vec3(0, 0, 600.0 * 600.0 / 4000.0);
    return power.to_bezier().elevated_u(1).elevated_v(1);
}

double max_difference(const BezierPatch& a, const BezierPatch& b) {
    double worst = 0.0;
    for (std::size_t k = 0; k < a.control_points().size(); ++k) {
        worst = std::max(worst, (a.control_points()[k] - b.control_points()[k]).norm());
    }
    return worst;
}

/// A scene with one from_boundaries patch whose four curves are a patch's
/// boundaries, given as control points (exact).
Scene boundary_scene(const BezierPatch& source) {
    Scene scene;
    const std::array<std::pair<const char*, Edge>, 4> sides{
        {{"umin", Edge::UMin}, {"umax", Edge::UMax}, {"vmin", Edge::VMin}, {"vmax", Edge::VMax}}};
    for (const auto& [id, edge] : sides) {
        scene.curves.push_back({id, source.boundary(edge), std::nullopt});
    }
    ada::ScenePatch patch{
        "panel",
        BezierPatch(1, 1, std::vector<Vec3>(4, Vec3::Zero())),
        ada::FromBoundaries{{"umin", "umax", "vmin", "vmax"}, std::nullopt, std::nullopt},
        false};
    patch.patch = ada::initial_patch(scene, patch);
    scene.patches.push_back(std::move(patch));
    return scene;
}

/// Hood-like height field, even in y so it is symmetric about Y = 0.
double hood_z(double x, double y) {
    const double s = x / 1200.0;
    const double t = y / 700.0;
    return 800.0 + 40.0 * s - 45.0 * s * s - 70.0 * t * t;
}

/// Four points on the hood surface along a straight segment in plan view.
std::vector<Vec3> hood_points(double x0, double y0, double x1, double y1) {
    std::vector<Vec3> points;
    for (int k = 0; k <= 3; ++k) {
        const double a = k / 3.0;
        const double x = x0 + a * (x1 - x0);
        const double y = y0 + a * (y1 - y0);
        points.emplace_back(x, y, hood_z(x, y));
    }
    return points;
}

/// A 2 x 2 network over the hood, built only from curves through points: u
/// runs outwards (y), v rearwards (x). Column 0 sits on Y = 0 and is required
/// to be mirror-symmetric there; every internal edge is G2.
Scene hood_network() {
    Scene scene;
    const std::array<double, 3> xs{0.0, 600.0, 1200.0};
    const std::array<double, 3> ys{0.0, 350.0, 700.0};
    ada::NetworkSpec spec{"hood", {}, {}, Continuity::G2};
    for (std::size_t r = 0; r < 3; ++r) {
        std::vector<std::string> line;
        for (std::size_t c = 0; c < 2; ++c) {
            const std::string id = "u_" + std::to_string(r) + "_" + std::to_string(c);
            const auto points = hood_points(xs[r], ys[c], xs[r], ys[c + 1]);
            scene.curves.push_back({id, ada::curve_through_points(points, 3), points});
            line.push_back(id);
        }
        spec.along_u.push_back(line);
    }
    for (std::size_t c = 0; c < 3; ++c) {
        std::vector<std::string> line;
        for (std::size_t r = 0; r < 2; ++r) {
            const std::string id = "v_" + std::to_string(c) + "_" + std::to_string(r);
            const auto points = hood_points(xs[r], ys[c], xs[r + 1], ys[c]);
            scene.curves.push_back({id, ada::curve_through_points(points, 3), points});
            line.push_back(id);
        }
        spec.along_v.push_back(line);
    }
    ada::add_network(scene, spec);
    scene.symmetry.push_back({{"hood_0_0", Edge::UMin}, Continuity::G2});
    scene.symmetry.push_back({{"hood_1_0", Edge::UMin}, Continuity::G2});
    return scene;
}

void require_joins(const Scene& scene) {
    for (const ada::EdgeLink& link : ada::all_edge_links(scene)) {
        const ada::EdgeContinuity m =
            ada::measure_edge(PatchEvaluator(scene.patch(link.a.patch).patch),
                              link.a.edge,
                              PatchEvaluator(scene.patch(link.b.patch).patch),
                              link.b.edge,
                              kEdgeSamples);
        INFO(link.a.patch << "." << ada::to_string(link.a.edge) << " ~ " << link.b.patch << "."
                          << ada::to_string(link.b.edge));
        CHECK(m.max_gap < kJoinGapMm);
        if (link.continuity != Continuity::G0) {
            CHECK(m.max_normal_angle_deg < kJoinAngleDeg);
            CHECK_FALSE(m.fold);
        }
        if (link.continuity == Continuity::G2) {
            CHECK(m.max_curvature_deviation < kJoinCurvature);
        }
    }
}

/// Every failed check on one line, for INFO and substring checks.
std::string failure_text(const ada::QualityReport& report) {
    std::string out;
    for (const ada::FailedCheck& f : ada::failed_checks(report)) {
        out += f.where + ": " + f.check.name + " ";
    }
    return out;
}

} // namespace

TEST_CASE("curves through few points interpolate them exactly", "[doc][curves]") {
    const std::vector<Vec3> four{
        Vec3(0, 0, 0), Vec3(300, 50, 10), Vec3(650, 40, 30), Vec3(1000, 0, 20)};
    const BezierCurve cubic = ada::curve_through_points(four, 3);
    const std::vector<double> t = ada::chord_length_parameters(four);
    for (std::size_t k = 0; k < four.size(); ++k) {
        CHECK((cubic.evaluate(t[k]) - four[k]).norm() < kSameMm);
    }

    // Three points asked for at degree 5: interpolated at degree 2, elevated.
    const std::vector<Vec3> three{Vec3(0, 0, 0), Vec3(500, 80, 0), Vec3(1000, 0, 0)};
    const BezierCurve quintic = ada::curve_through_points(three, 5);
    CHECK(quintic.degree() == 5);
    const std::vector<double> t3 = ada::chord_length_parameters(three);
    for (std::size_t k = 0; k < three.size(); ++k) {
        CHECK((quintic.evaluate(t3[k]) - three[k]).norm() < kSameMm);
    }
}

TEST_CASE("least squares through many collinear points stays on the line", "[doc][curves]") {
    std::vector<Vec3> line;
    for (int k = 0; k <= 8; ++k) {
        line.emplace_back(100.0 * k * k / 8.0, 3.0 * 100.0 * k * k / 8.0, 0.0); // uneven spacing
    }
    const BezierCurve c = ada::curve_through_points(line, 3);
    CHECK((c.control_point(0) - line.front()).norm() < kSameMm);
    CHECK((c.control_point(3) - line.back()).norm() < kSameMm);
    const Vec3 direction = (line.back() - line.front()).normalized();
    for (double s = 0.0; s <= 1.0; s += 0.125) {
        const Vec3 offset = c.evaluate(s) - line.front();
        CHECK((offset - offset.dot(direction) * direction).norm() < kSameMm);
    }
}

TEST_CASE("unusable point input is rejected", "[doc][curves]") {
    CHECK_THROWS_AS(ada::curve_through_points({Vec3(1, 2, 3)}, 3), std::invalid_argument);
    CHECK_THROWS_AS(ada::curve_through_points({Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(1, 0, 0)}, 3),
                    std::invalid_argument);
    CHECK_THROWS_AS(ada::curve_through_points({Vec3(0, 0, 0), Vec3(1, 0, 0)}, 0),
                    std::invalid_argument);
}

TEST_CASE("from_boundaries rebuilds a dome from its four edges", "[doc][recipes]") {
    // E3 vanishes on the dome (total degree 2), so the faired interior must
    // be the dome's own.
    Scene scene = boundary_scene(dome());
    CHECK_FALSE(scene.patches[0].solved);
    ada::solve_scene(scene);
    CHECK(scene.patches[0].solved);
    CHECK(max_difference(scene.patches[0].patch, dome()) < kSameMm);
}

TEST_CASE("boundary curves may run either way", "[doc][recipes]") {
    // Every subset of the four curves reversed: each orientation path in
    // resolve_boundaries has to land on the same patch.
    for (unsigned mask = 0; mask < 16; ++mask) {
        Scene scene = boundary_scene(dome());
        for (std::size_t k = 0; k < 4; ++k) {
            if ((mask >> k) & 1U) {
                scene.curves[k].curve = scene.curves[k].curve.reversed();
            }
        }
        ada::solve_scene(scene);
        INFO("reversed curves mask " << mask);
        CHECK(max_difference(scene.patches[0].patch, dome()) < kSameMm);
    }
}

TEST_CASE("curves that do not meet are reported by name", "[doc][recipes]") {
    Scene scene = boundary_scene(dome());
    std::vector<Vec3> moved = scene.curves[3].curve.control_points(); // vmax
    for (Vec3& p : moved)
        p.z() += 3.0;
    scene.curves[3].curve = BezierCurve(moved);
    try {
        ada::solve_scene(scene);
        FAIL("corner gap not detected");
    } catch (const std::invalid_argument& e) {
        const std::string message = e.what();
        INFO(message);
        CHECK(message.find("vmax") != std::string::npos);
        CHECK(message.find("do not meet") != std::string::npos);
    }
}

TEST_CASE("an unsolved patch fails the report until the scene is solved", "[doc][recipes]") {
    Scene scene = boundary_scene(dome());
    const ada::QualityReport before = ada::compute_quality(scene);
    CHECK_FALSE(before.pass);
    CHECK(failure_text(before).find("unsolved") != std::string::npos);
    ada::solve_scene(scene);
    const ada::QualityReport after = ada::compute_quality(scene);
    INFO(failure_text(after));
    CHECK(after.pass);
}

TEST_CASE("a pin pulls the surface through a point", "[doc][recipes]") {
    Scene scene = boundary_scene(dome());
    const Vec3 target = PatchEvaluator(dome()).evaluate(0.4, 0.6) + Vec3(0, 0, 25);
    scene.pins.push_back({"panel", 0.4, 0.6, target});
    ada::solve_scene(scene);
    CHECK((PatchEvaluator(scene.patches[0].patch).evaluate(0.4, 0.6) - target).norm() < kSameMm);
    // The boundary stays on the curves.
    CHECK(max_difference(BezierPatch(3, 0, scene.patches[0].patch.edge_row(Edge::VMin, 0)),
                         BezierPatch(3, 0, dome().edge_row(Edge::VMin, 0))) < kSameMm);
}

TEST_CASE("a blend joins two fixed panels G2 on both sides", "[doc][recipes]") {
    Scene scene;
    const BezierPatch a = domed_panel();
    std::vector<Vec3> shifted = a.control_points();
    for (Vec3& p : shifted)
        p += Vec3(0, 900, -20);
    scene.patches.push_back({"a", a, std::nullopt, true});
    scene.patches.push_back({"b", BezierPatch(3, 3, shifted), std::nullopt, true});
    ada::ScenePatch blend{
        "gap",
        BezierPatch(1, 1, std::vector<Vec3>(4, Vec3::Zero())),
        ada::Blend{{"a", Edge::VMax}, {"b", Edge::VMin}, Continuity::G2, std::nullopt},
        false};
    blend.patch = ada::initial_patch(scene, blend);
    scene.patches.push_back(std::move(blend));

    ada::solve_scene(scene);
    CHECK(scene.patch("gap").patch.degree_v() == 5);
    CHECK(ada::all_edge_links(scene).size() == 2);
    require_joins(scene);
    CHECK(max_difference(scene.patch("a").patch, a) == 0.0); // sources never move

    // A G1 blend needs only degree 3 across.
    std::get<ada::Blend>(*scene.patches[2].recipe).continuity = Continuity::G1;
    scene.patches[2].solved = false;
    ada::solve_scene(scene);
    CHECK(scene.patch("gap").patch.degree_v() == 3);
    require_joins(scene);
}

TEST_CASE("a symmetric 2x2 network from curves through points comes out G2", "[doc][recipes]") {
    Scene scene = hood_network();
    REQUIRE(scene.patches.size() == 4);
    REQUIRE(scene.edges.size() == 4);
    const ada::SceneSolveSummary summary = ada::solve_scene(scene);
    require_joins(scene);
    for (const ada::SymmetryLink& link : scene.symmetry) {
        const ada::SymmetryContinuity s = ada::measure_symmetry(
            PatchEvaluator(scene.patch(link.edge.patch).patch), link.edge.edge, kEdgeSamples);
        CHECK(s.max_plane_distance < kJoinGapMm);
        CHECK(s.max_normal_tilt_deg < kJoinAngleDeg);
    }

    // The curves had to give a little at the nodes (they were fitted one by
    // one), and only a little: they are samples of one smooth surface.
    double worst = 0.0;
    for (const ada::CurveDeviation& c : summary.curves) {
        worst = std::max(worst, c.max_mm);
    }
    INFO("largest curve adjustment " << worst << " mm");
    CHECK(summary.curves.size() == scene.curves.size());
    CHECK(worst > 0.0);
    CHECK(worst < 5.0);

    const ada::QualityReport report = ada::compute_quality(scene);
    INFO(failure_text(report));
    CHECK(report.pass);
}

TEST_CASE("scenes round-trip through JSON, solved or not", "[doc][scene]") {
    Scene authored = hood_network();
    authored.pins.push_back({"hood_1_1", 0.5, 0.5, Vec3(900, 520, 790)});
    authored.solve.order = ada::FairnessOrder::E2;
    authored.solve.move = 0.5;
    const nlohmann::json unsolved = ada::scene_to_json(authored);
    CHECK_FALSE(unsolved.at("patches")[0].contains("control_points")); // recipes only
    CHECK(unsolved.at("curves")[0].contains("through"));
    CHECK(ada::scene_to_json(ada::scene_from_json(unsolved)) == unsolved);

    Scene solved = ada::scene_from_json(unsolved);
    ada::solve_scene(solved);
    const nlohmann::json json = ada::scene_to_json(solved);
    CHECK(json.at("patches")[0].contains("control_points"));
    CHECK(ada::scene_to_json(ada::scene_from_json(json)) == json);
}

TEST_CASE("version 0 scenes still load, as fixed patches", "[doc][scene]") {
    nlohmann::json v0 = ada::scene_to_json(hood_scene());
    v0["version"] = 0;
    const Scene scene = ada::scene_from_json(v0);
    CHECK(scene.patches.size() == 2);
    CHECK_FALSE(scene.patches[0].recipe.has_value());
    CHECK(scene.patches[0].solved);
}

TEST_CASE("undo and redo restore whole scenes", "[doc][history]") {
    ada::History history;
    Scene scene = boundary_scene(dome());
    CHECK_FALSE(history.can_undo());

    history.record(scene);
    Scene edited = scene;
    ada::solve_scene(edited);
    REQUIRE(edited.patches[0].solved);

    const Scene back = history.undo(edited);
    CHECK_FALSE(back.patches[0].solved);
    CHECK(history.can_redo());
    const Scene forward = history.redo(back);
    CHECK(forward.patches[0].solved);
    CHECK(max_difference(forward.patches[0].patch, edited.patches[0].patch) == 0.0);

    history.record(forward); // a new edit clears what could be redone
    CHECK_FALSE(history.can_redo());
    CHECK_THROWS_AS(ada::History().undo(scene), std::logic_error);
}

TEST_CASE("undo and redo say which edit they move over", "[doc][history]") {
    ada::History history;
    const Scene scene = boundary_scene(dome());
    CHECK(history.undo_label().empty());
    history.record(scene, "first");
    history.record(scene, "second");
    CHECK(history.undo_label() == "second");

    const Scene back = history.undo(scene);
    CHECK(history.undo_label() == "first");
    CHECK(history.redo_label() == "second");
    static_cast<void>(history.redo(back));
    CHECK(history.undo_label() == "second");
    CHECK(history.redo_label().empty());
}

TEST_CASE("a degree override raises a from_boundaries patch exactly", "[doc][recipes]") {
    Scene scene = boundary_scene(dome());
    std::get<ada::FromBoundaries>(*scene.patches[0].recipe).degree_u = 5;
    ada::solve_scene(scene);
    const BezierPatch& p = scene.patches[0].patch;
    CHECK(p.degree_u() == 5);
    CHECK(p.degree_v() == 3);

    // Still on its curves: they were degree-elevated, not refitted.
    const std::array<std::pair<const char*, Edge>, 4> sides{
        {{"umin", Edge::UMin}, {"umax", Edge::UMax}, {"vmin", Edge::VMin}, {"vmax", Edge::VMax}}};
    for (const auto& [id, edge] : sides) {
        const BezierCurve curve = scene.curve(id).curve;
        const BezierCurve side = p.boundary(edge);
        for (std::size_t k = 0; k < kEdgeSamples; ++k) {
            const double t = static_cast<double>(k) / (kEdgeSamples - 1);
            CHECK((side.evaluate(t) - curve.evaluate(t)).norm() < kSameMm);
        }
    }

    const Scene back = ada::scene_from_json(ada::scene_to_json(scene));
    const auto& recipe = std::get<ada::FromBoundaries>(*back.patches[0].recipe);
    CHECK(recipe.degree_u == 5);
    CHECK_FALSE(recipe.degree_v.has_value());
}

TEST_CASE("a degree override below the curves' degree is refused", "[doc][recipes]") {
    Scene scene = boundary_scene(dome());
    std::get<ada::FromBoundaries>(*scene.patches[0].recipe).degree_v = 2;
    CHECK_THROWS_AS(ada::solve_scene(scene), std::invalid_argument);
}

TEST_CASE("raising a degree starts the solve from the same surface", "[doc][recipes]") {
    // A least-change solve returns its start whenever the start already meets
    // the constraints, so it shows what the solve started from. Store a
    // surface that is not the recipe's Coons start, then raise the degrees.
    const BezierPatch stored = nudge(dome(), 1, 2, 6.0);
    Scene scene = boundary_scene(dome());
    scene.patches[0].patch = stored;
    scene.patches[0].solved = true;
    scene.solve.fairness = 0.0;
    scene.solve.move = 1.0;
    auto& recipe = std::get<ada::FromBoundaries>(*scene.patches[0].recipe);
    recipe.degree_u = 4;
    recipe.degree_v = 5;
    ada::solve_scene(scene);

    REQUIRE(scene.patches[0].patch.degree_u() == 4);
    REQUIRE(scene.patches[0].patch.degree_v() == 5);
    const PatchEvaluator raised(scene.patches[0].patch);
    const PatchEvaluator before(stored);
    double worst = 0.0;
    for (std::size_t i = 0; i < kEdgeSamples; ++i) {
        for (std::size_t j = 0; j < kEdgeSamples; ++j) {
            const double u = static_cast<double>(i) / (kEdgeSamples - 1);
            const double v = static_cast<double>(j) / (kEdgeSamples - 1);
            worst = std::max(worst, (raised.evaluate(u, v) - before.evaluate(u, v)).norm());
        }
    }
    CHECK(worst < kSameMm);
}

TEST_CASE("linked patches are raised to a common degree along their joint", "[doc][recipes]") {
    // The outer side curve of hood_0_1 goes to degree 5 (elevated: the same
    // curve), so that patch is degree 5 in v while hood_0_0, joined to it along
    // v, is degree 3. Matched edges need equal degrees; the lower one rises.
    Scene scene = hood_network();
    for (ada::SceneCurve& c : scene.curves) {
        if (c.id == "v_2_0") {
            c.curve = c.curve.elevated(2);
            c.through.reset();
        }
    }
    ada::solve_scene(scene);
    CHECK(scene.patch("hood_0_1").patch.degree_v() == 5);
    CHECK(scene.patch("hood_0_0").patch.degree_v() == 5);
    CHECK(scene.patch("hood_1_0").patch.degree_v() == 3); // tied to neither
    CHECK(scene.patch("hood_0_0").patch.degree_u() == 3);
    require_joins(scene);
}
