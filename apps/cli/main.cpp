// ada: command-line front end to the kernel. `bench` and `export` arrive with
// M6.
//
// Exit codes: 0 when the scene passes, 1 when a quality check fails, 2 when
// the input could not be read or analysed. Scripts and CI rely on the
// difference between 1 and 2.

#include "ada/analysis/quality.hpp"
#include "ada/build_info.hpp"
#include "ada/doc/scene.hpp"

#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr int kExitPass = 0;
constexpr int kExitFail = 1;
constexpr int kExitError = 2;

int run_report(const std::filesystem::path& scene_path, const std::string& json_path) {
    const ada::Scene scene = ada::read_scene(scene_path);
    const ada::QualityReport report = ada::compute_quality(scene);

    fmt::print("{}", ada::format_report(report));

    if (!json_path.empty()) {
        nlohmann::json json = ada::to_json(report);
        json["scene"] = scene_path.generic_string();
        json["ada"] = std::string{ada::build_info_string()};
        std::ofstream stream(json_path);
        if (!stream) {
            throw std::runtime_error(fmt::format("cannot open {} for writing", json_path));
        }
        stream << json.dump(2) << '\n';
    }
    return report.pass ? kExitPass : kExitFail;
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"ADA Surface command-line tools", "ada"};
    app.set_version_flag("--version", std::string{ada::build_info_string()});
    app.require_subcommand(0, 1);

    std::filesystem::path scene_path;
    std::string json_path;
    CLI::App* report = app.add_subcommand("report", "Check a scene against its quality budgets");
    report->add_option("scene", scene_path, "Scene file (.ada.json)")
        ->required()
        ->check(CLI::ExistingFile);
    report->add_option("--json", json_path, "Also write the full report as JSON to this path");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        // --help and --version arrive here too, with a success code.
        return app.exit(e) == 0 ? kExitPass : kExitError;
    }

    try {
        if (report->parsed()) {
            return run_report(scene_path, json_path);
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
        return kExitError;
    }

    fmt::print("{}", app.help());
    return kExitPass;
}
