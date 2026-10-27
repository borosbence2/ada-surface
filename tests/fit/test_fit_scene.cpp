// Fitting a scene to a target: known surfaces sampled, with and without
// noise, recovered through the scene solve, and reported.

#include "ada/analysis/quality.hpp"
#include "ada/doc/recipes.hpp"
#include "ada/doc/scene.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "support/panels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

using ada::BezierPatch;
using ada::Edge;
using ada::Scene;
using ada::Vec3;

namespace {

constexpr unsigned kSeed = 20261027;
constexpr double kNoiseMm = 0.05;
constexpr std::size_t kGrid = 25;

/// The domed panel raised to degree 4 with its middle control point lifted
/// 8 mm: its boundaries are still the panel's cubic ones, but the bump in
/// between needs degree 4 to be represented.
BezierPatch bumped_panel() {
    BezierPatch p = ada::test::domed_panel().elevated_u(1).elevated_v(1);
    p.control_point(2, 2).z() += 8.0;
    return p;
}

/// Points of `source` on a (u, v) grid, displaced along z by Gaussian noise.
std::vector<Vec3> samples(const BezierPatch& source, double noise) {
    std::mt19937 rng(kSeed);
    std::normal_distribution<double> gauss(0.0, noise);
    const ada::PatchEvaluator evaluator(source);
    std::vector<Vec3> points;
    for (std::size_t i = 0; i < kGrid; ++i) {
        for (std::size_t j = 0; j < kGrid; ++j) {
            Vec3 p = evaluator.evaluate(static_cast<double>(i) / (kGrid - 1),
                                        static_cast<double>(j) / (kGrid - 1));
            if (noise > 0.0) {
                p.z() += gauss(rng);
            }
            points.push_back(p);
        }
    }
    return points;
}

/// One from_boundaries patch on the domed panel's (cubic) boundaries, fitted
/// to `points` at degree `degree` in both directions.
Scene fit_scene(std::vector<Vec3> points, int degree) {
    const BezierPatch panel = ada::test::domed_panel();
    Scene scene;
    const std::array<std::pair<const char*, Edge>, 4> sides{
        {{"umin", Edge::UMin}, {"umax", Edge::UMax}, {"vmin", Edge::VMin}, {"vmax", Edge::VMax}}};
    for (const auto& [id, edge] : sides) {
        scene.curves.push_back({id, panel.boundary(edge), std::nullopt});
    }
    ada::ScenePatch patch{"panel",
                          BezierPatch(1, 1, std::vector<Vec3>(4, Vec3::Zero())),
                          ada::FromBoundaries{{"umin", "umax", "vmin", "vmax"}, degree, degree},
                          false};
    patch.patch = ada::initial_patch(scene, patch);
    scene.patches.push_back(std::move(patch));
    ada::SceneTarget target;
    target.id = "scan";
    target.file = "scan.ply";
    target.points = std::make_shared<const std::vector<Vec3>>(std::move(points));
    scene.targets.push_back(target);
    scene.fit = ada::FitSettings{"scan", ada::kDefaultFitWeight};
    return scene;
}

/// How far the fitted surface is from the true one: the largest distance
/// from points of the truth to the fit. Geometric, not at equal (u, v): a fit
/// fixes the shape, and control points may slide along it.
double distance_from_truth(const BezierPatch& fitted, const BezierPatch& truth) {
    return ada::fit_stats(ada::assign_points({fitted}, samples(truth, 0.0))).max_mm;
}

} // namespace

TEST_CASE("a fit recovers exact data at the data's degree", "[fit][scene]") {
    Scene scene = fit_scene(samples(bumped_panel(), 0.0), 4);
    scene.solve.fairness = 0.0; // nothing but the data
    const ada::SceneSolveSummary summary = ada::solve_scene(scene);
    REQUIRE(summary.fit.active);
    CHECK(summary.fit.iterations > 1); // the first footpoints were on the unbumped start
    CHECK(summary.fit.stats.points == kGrid * kGrid);
    // Parameter correction by point distance converges linearly: it stops
    // with the footpoints settled and a few micrometres left of a 1 mm bump.
    CHECK(summary.fit.stats.max_mm < 0.005);
    CHECK(distance_from_truth(scene.patches[0].patch, bumped_panel()) < 0.005);
}

TEST_CASE("with noise, a fit lands within the noise of the true surface", "[fit][scene]") {
    Scene scene = fit_scene(samples(bumped_panel(), kNoiseMm), 4);
    const ada::SceneSolveSummary summary = ada::solve_scene(scene);
    // The residual is the noise itself, and the surface sits well inside it:
    // 625 points average the noise over the 9 free control points.
    CHECK(summary.fit.stats.rms_mm > 0.8 * kNoiseMm);
    CHECK(summary.fit.stats.rms_mm < 1.2 * kNoiseMm);
    CHECK(distance_from_truth(scene.patches[0].patch, bumped_panel()) < 0.5 * kNoiseMm);

    // At degree 3 the bump does not fit, and the residual says so.
    Scene cubic = fit_scene(samples(bumped_panel(), kNoiseMm), 3);
    CHECK(ada::solve_scene(cubic).fit.stats.rms_mm > 2.0 * kNoiseMm);
}

TEST_CASE("fit deviations are in the report and checked against the budgets", "[fit][scene]") {
    Scene scene = fit_scene(samples(bumped_panel(), kNoiseMm), 4);
    ada::solve_scene(scene);
    scene.budgets.fit_rms_mm = 0.06;
    ada::QualityReport report = ada::compute_quality(scene);
    REQUIRE(report.fit.has_value());
    CHECK(report.fit->target == "scan");
    CHECK(report.fit->stats.points == kGrid * kGrid);
    CHECK(report.fit->pass);

    scene.budgets.fit_rms_mm = 0.01;
    report = ada::compute_quality(scene);
    CHECK_FALSE(report.pass);
    const auto failed = ada::failed_checks(report);
    REQUIRE(failed.size() == 1);
    CHECK(failed[0].check.name == "fit_rms_mm");
    CHECK(ada::to_json(report).at("fit").at("rms_mm").get<double>() > 0.0);
}

TEST_CASE("a fit needs its target loaded", "[fit][scene]") {
    Scene scene = fit_scene(samples(bumped_panel(), 0.0), 4);
    scene.targets[0].points.reset();
    CHECK_THROWS_AS(ada::solve_scene(scene), std::invalid_argument);
    scene.fit->target = "nope";
    CHECK_THROWS_AS(ada::solve_scene(scene), std::invalid_argument);
}

TEST_CASE("targets load from files next to the scene, and follow it when written elsewhere",
          "[fit][scene]") {
    const auto root = std::filesystem::temp_directory_path() / "ada-fit-scene";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "data");
    std::filesystem::create_directories(root / "out" / "deeper");
    {
        std::ofstream obj(root / "data" / "scan.obj");
        for (const Vec3& p : samples(bumped_panel(), 0.0)) {
            obj << "v " << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
        }
    }
    Scene scene = fit_scene({}, 4);
    scene.targets[0].file = "scan.obj";
    scene.targets[0].points.reset();
    nlohmann::json json = ada::scene_to_json(scene);
    {
        std::ofstream(root / "data" / "fit.ada.json") << json.dump(2);
    }

    Scene loaded = ada::read_scene(root / "data" / "fit.ada.json");
    REQUIRE(loaded.targets[0].points != nullptr);
    CHECK(loaded.targets[0].points->size() == kGrid * kGrid); // a point cloud: vertices as they are
    ada::solve_scene(loaded);

    ada::write_scene(loaded, root / "out" / "deeper" / "fit.ada.json");
    std::ifstream written(root / "out" / "deeper" / "fit.ada.json");
    const nlohmann::json back = nlohmann::json::parse(written);
    CHECK(back.at("targets")[0].at("file") == "../../data/scan.obj");
    CHECK(ada::read_scene(root / "out" / "deeper" / "fit.ada.json").targets[0].points->size() ==
          kGrid * kGrid);
    written.close();
    std::filesystem::remove_all(root);
}

TEST_CASE("M11 exit: a known Bezier plus noise is recovered within the noise, at its own degree",
          "[fit][scene][economy]") {
    // The bumped panel is degree 4 on cubic boundaries. CV economy starts at
    // the curves' degree 3 (any override is dropped), finds the residual
    // above the budget, raises the patch to 4, and stops there.
    Scene scene = fit_scene(samples(bumped_panel(), kNoiseMm), 9);
    scene.budgets.fit_rms_mm = 1.2 * kNoiseMm;
    const ada::EconomyResult result = ada::fit_economically(scene);
    INFO(result.steps.size() << " steps");
    CHECK(result.met);
    REQUIRE(result.steps.size() == 1);
    CHECK(scene.patches[0].patch.degree_u() == 4);
    CHECK(scene.patches[0].patch.degree_v() == 4);
    CHECK(result.last.fit.stats.rms_mm < 1.2 * kNoiseMm);
    CHECK(distance_from_truth(scene.patches[0].patch, bumped_panel()) < 0.5 * kNoiseMm);
}

TEST_CASE("CV economy stops at the degree cap when the budget is out of reach",
          "[fit][scene][economy]") {
    // Asking for a tenth of the noise cannot be met at any degree; the
    // budget's max_degree caps the climb.
    Scene scene = fit_scene(samples(bumped_panel(), kNoiseMm), 3);
    scene.budgets.fit_rms_mm = 0.1 * kNoiseMm;
    scene.budgets.max_degree = 5;
    const ada::EconomyResult result = ada::fit_economically(scene);
    CHECK_FALSE(result.met);
    CHECK(result.steps.size() == 2);
    CHECK(scene.patches[0].patch.degree_u() == 5);

    Scene no_budget = fit_scene(samples(bumped_panel(), 0.0), 3);
    CHECK_THROWS_AS(ada::fit_economically(no_budget), std::invalid_argument);
    no_budget.fit.reset();
    no_budget.budgets.fit_rms_mm = 0.1;
    CHECK_THROWS_AS(ada::fit_economically(no_budget), std::invalid_argument);
}
