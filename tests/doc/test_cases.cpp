#include "ada/analysis/continuity.hpp"
#include "ada/analysis/quality.hpp"
#include "ada/doc/recipes.hpp"
#include "ada/doc/scene.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "support/cases.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace ada::test;

namespace {

std::filesystem::path cases_dir() {
    return std::filesystem::path(ADA_SOURCE_DIR) / "cases";
}

std::string failures(const ada::QualityReport& report) {
    std::string out;
    auto add = [&out](const std::string& where, const std::vector<ada::Check>& checks) {
        for (const ada::Check& c : checks) {
            if (!c.pass) {
                out += where + ": " + c.name + " = " + std::to_string(c.value) + "; ";
            }
        }
    };
    for (const auto& p : report.patches) {
        add(p.id, p.checks);
    }
    for (const auto& e : report.edges) {
        add(e.link.a.patch + "~" + e.link.b.patch, e.checks);
    }
    for (const auto& s : report.symmetry) {
        add(s.link.edge.patch + " Y0", s.checks);
    }
    return out;
}

} // namespace

TEST_CASE("every L0-L2 case is authored without control points", "[doc][cases]") {
    // MILESTONES M5 exit: the committed case files carry curves through points
    // and recipes only. A control point in any patch would mean someone wrote
    // geometry by hand.
    for (const AuthoredCase& c : all_cases()) {
        const nlohmann::json json = ada::scene_to_json(c.scene);
        INFO(c.name);
        for (const nlohmann::json& patch : json.at("patches")) {
            CHECK(patch.contains("recipe"));
            CHECK_FALSE(patch.contains("control_points"));
        }
        for (const nlohmann::json& curve : json.at("curves")) {
            CHECK(curve.contains("through"));
        }
    }
}

TEST_CASE("the committed case files match their generators", "[doc][cases]") {
    // Regenerate with: ada_tests "[.generate-cases]"
    for (const AuthoredCase& c : all_cases()) {
        INFO(c.name);
        const ada::Scene committed = ada::read_scene(cases_dir() / (c.name + ".ada.json"));
        CHECK(ada::scene_to_json(committed) == ada::scene_to_json(c.scene));
    }
}

TEST_CASE("every case solves to its continuity and passes the default budgets", "[doc][cases]") {
    for (AuthoredCase c : all_cases()) {
        INFO(c.name);
        ada::solve_scene(c.scene);
        const ada::QualityReport report = ada::compute_quality(c.scene);
        INFO(failures(report));
        CHECK(report.pass);
    }
}

TEST_CASE("write the case files", "[.generate-cases]") {
    for (const AuthoredCase& c : all_cases()) {
        ada::write_scene(c.scene, cases_dir() / (c.name + ".ada.json"));
    }
}
