#include "ada/analysis/quality.hpp"
#include "ada/doc/scene.hpp"

#include "support/panels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using ada::Check;
using ada::QualityReport;
using ada::Scene;
using namespace ada::test;

namespace {

const Check* find_check(const std::vector<Check>& checks, const std::string& name) {
    const auto it =
        std::find_if(checks.begin(), checks.end(), [&](const Check& c) { return c.name == name; });
    return it == checks.end() ? nullptr : &*it;
}

std::filesystem::path examples_dir() {
    return std::filesystem::path(ADA_SOURCE_DIR) / "cases" / "examples";
}

} // namespace

TEST_CASE("the hood example passes every default budget", "[analysis][quality]") {
    const QualityReport report = ada::compute_quality(hood_scene());
    CHECK(report.pass);
    REQUIRE(report.edges.size() == 1);
    REQUIRE(report.symmetry.size() == 2);

    // A G2 edge is checked for gap, angle, fold and curvature.
    const auto& checks = report.edges[0].checks;
    for (const char* name : {"g0_gap_mm", "g1_angle_deg", "fold", "g2_relative"}) {
        INFO(name);
        REQUIRE(find_check(checks, name) != nullptr);
        CHECK(find_check(checks, name)->pass);
    }
    for (const auto& s : report.symmetry) {
        CHECK(s.pass);
        CHECK(find_check(s.checks, "g1_normal_tilt_deg") != nullptr);
    }
}

TEST_CASE("raising a row-2 control point fails exactly the G2 check", "[analysis][quality]") {
    const QualityReport report = ada::compute_quality(hood_scene_g2_broken());
    CHECK_FALSE(report.pass);
    const auto& edge = report.edges[0];
    CHECK_FALSE(edge.pass);
    CHECK_FALSE(find_check(edge.checks, "g2_relative")->pass);
    CHECK(find_check(edge.checks, "g1_angle_deg")->pass);
    CHECK(find_check(edge.checks, "g0_gap_mm")->pass);
    for (const auto& s : report.symmetry) {
        CHECK(s.pass); // row 2 does not touch the centre line
    }

    const std::string text = ada::format_report(report);
    CHECK(text.find("FAIL") != std::string::npos);
    CHECK(text.find("g2_relative") != std::string::npos);
}

TEST_CASE("budgets decide the verdict", "[analysis][quality]") {
    Scene loose = hood_scene_g2_broken();
    loose.budgets.g2_relative = 10.0;
    CHECK(ada::compute_quality(loose).pass);

    Scene g1_only = hood_scene_g2_broken();
    g1_only.edges[0].continuity = ada::Continuity::G1;
    const QualityReport report = ada::compute_quality(g1_only);
    CHECK(report.pass);
    CHECK(find_check(report.edges[0].checks, "g2_relative") == nullptr);
}

TEST_CASE("per-case budgets are only checked when a scene sets them", "[analysis][quality]") {
    Scene scene = hood_scene();
    CHECK(find_check(ada::compute_quality(scene).patches[0].checks, "degree") == nullptr);

    scene.budgets.max_degree = 2;
    const QualityReport report = ada::compute_quality(scene);
    CHECK_FALSE(report.pass);
    CHECK_FALSE(find_check(report.patches[0].checks, "degree")->pass);
}

TEST_CASE("the JSON report carries every measured field", "[analysis][quality]") {
    const nlohmann::json json = ada::to_json(ada::compute_quality(hood_scene_g2_broken()));
    CHECK(json.at("pass") == false);
    for (const char* key :
         {"g0_gap_mm", "g1_angle_deg", "g2_relative", "g2_absolute", "fold", "checks"}) {
        INFO(key);
        CHECK(json.at("edges")[0].contains(key));
    }
    for (const char* key : {"curvature", "highlights", "cv_hull", "checks", "degree", "cv_count"}) {
        INFO(key);
        CHECK(json.at("patches")[0].contains(key));
    }
    CHECK(json.at("symmetry")[0].at("g2_implied_by_g1") == true);
}

TEST_CASE("scenes survive a JSON round trip", "[doc][scene]") {
    Scene scene = hood_scene();
    scene.budgets.g1_angle_deg = 0.05;
    scene.budgets.max_highlight_inflections = 1;
    scene.light =
        ada::make_light_plane(ada::Vec3(0, 0, 3000), ada::Vec3::UnitZ(), ada::Vec3::UnitX(), 40.0);

    const Scene back = ada::scene_from_json(ada::scene_to_json(scene));
    REQUIRE(back.patches.size() == 2);
    CHECK(back.patches[1].patch.control_points() == scene.patches[1].patch.control_points());
    CHECK(back.edges[0].b.edge == ada::Edge::VMin);
    CHECK(back.symmetry.size() == 2);
    CHECK(back.budgets.g1_angle_deg == 0.05);
    CHECK(back.budgets.max_highlight_inflections == 1);
    CHECK(back.budgets.g0_gap_mm == ada::kDefaultG0GapMm);
    REQUIRE(back.light.has_value());
    CHECK(back.light->spacing == 40.0);
    CHECK(ada::scene_to_json(back) == ada::scene_to_json(scene));
}

TEST_CASE("malformed scenes are rejected with the JSON path", "[doc][scene]") {
    const nlohmann::json good = ada::scene_to_json(hood_scene());
    auto rejects = [](nlohmann::json json, const std::string& where) {
        try {
            static_cast<void>(ada::scene_from_json(json));
        } catch (const std::invalid_argument& e) {
            INFO(e.what());
            CHECK(std::string(e.what()).find(where) != std::string::npos);
            return;
        }
        FAIL("scene was accepted");
    };

    nlohmann::json j = good;
    j["units"] = "m";
    rejects(j, "$.units");

    j = good;
    j["patches"][1]["id"] = "front";
    rejects(j, "$.patches[1].id");

    j = good;
    j["patches"][0]["control_points"].erase(0);
    rejects(j, "$.patches[0]");

    j = good;
    j["edges"][0]["b"] = {"bonnet", "VMin"};
    rejects(j, "$.edges[0].b");

    j = good;
    j["symmetry"][0]["edge"] = {"front", "Left"};
    rejects(j, "$.symmetry[0].edge");

    j = good;
    j["budgets"] = {{"g2_relativ", 0.02}};
    rejects(j, "$.budgets");
}

TEST_CASE("the committed example scenes match their generators", "[doc][examples]") {
    // Regenerate with: ada_tests "[.generate-examples]"
    CHECK(ada::read_scene(examples_dir() / "hood.ada.json").patches[1].patch.control_points() ==
          hood_scene().patches[1].patch.control_points());
    CHECK(ada::scene_to_json(ada::read_scene(examples_dir() / "hood.ada.json")) ==
          ada::scene_to_json(hood_scene()));
    CHECK(ada::scene_to_json(ada::read_scene(examples_dir() / "hood-g2-broken.ada.json")) ==
          ada::scene_to_json(hood_scene_g2_broken()));
}

TEST_CASE("write the example scenes", "[.generate-examples]") {
    std::filesystem::create_directories(examples_dir());
    ada::write_scene(hood_scene(), examples_dir() / "hood.ada.json");
    ada::write_scene(hood_scene_g2_broken(), examples_dir() / "hood-g2-broken.ada.json");
}
