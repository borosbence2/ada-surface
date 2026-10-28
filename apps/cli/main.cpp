// ada: command-line front end to the kernel.
//
// `solve` turns a scene's recipes into control points and writes them back;
// `report` checks a scene against its budgets; `bench` does both, in memory,
// for every case in a directory; `export` writes IGES.
//
// Exit codes: 0 when the scene passes, 1 when a quality check fails, 2 when
// the input could not be read or analysed. Scripts and CI rely on the
// difference between 1 and 2.

#include "ada/analysis/quality.hpp"
#include "ada/build_info.hpp"
#include "ada/doc/bench.hpp"
#include "ada/doc/recipes.hpp"
#include "ada/doc/scene.hpp"
#include "ada/io/iges.hpp"

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

/// Curve adjustments smaller than this are rounding, not worth a line.
constexpr double kReportedCurveDeviationMm = 1e-6;

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

int run_solve(const std::filesystem::path& scene_path, const std::string& output, bool economy) {
    ada::Scene scene = ada::read_scene(scene_path);
    ada::SceneSolveSummary summary;
    if (economy || (scene.fit.has_value() && scene.fit->economy)) {
        const ada::EconomyResult result = ada::fit_economically(scene);
        for (const std::string& step : result.steps) {
            fmt::print("{}\n", step);
        }
        fmt::print("CV economy: {}\n",
                   result.met ? "the fit budgets are met"
                              : "the fit budgets are out of reach at the degree cap");
        summary = result.last;
    } else {
        summary = ada::solve_scene(scene);
    }
    const std::filesystem::path target =
        output.empty() ? scene_path : std::filesystem::path(output);
    ada::write_scene(scene, target);
    fmt::print("solved {} ({} stages, {} unknowns, max residual {:.2e} mm, {:.1f} ms) -> {}\n\n",
               scene_path.generic_string(),
               summary.stages,
               summary.unknowns,
               summary.max_residual,
               summary.milliseconds,
               target.generic_string());
    // Curves give way only next to G1/G2 nodes and symmetry edges; say how
    // much, so a designer can see where the network was not smooth already.
    for (const ada::CurveDeviation& c : summary.curves) {
        if (c.max_mm > kReportedCurveDeviationMm) {
            fmt::print("curve {} moved up to {:.3f} mm for continuity\n", c.curve, c.max_mm);
        }
    }
    if (summary.fit.active) {
        fmt::print("fit: {} parameter-correction solves\n", summary.fit.iterations);
    }
    const ada::QualityReport report = ada::compute_quality(scene);
    fmt::print("{}", ada::format_report(report));
    return report.pass ? kExitPass : kExitFail;
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error(fmt::format("cannot open {} for writing", path));
    }
    stream << text;
}

int run_bench(const std::filesystem::path& directory,
              const std::string& json_path,
              const std::string& markdown_path) {
    const ada::BenchResult result = ada::run_bench(directory);
    const std::string markdown = ada::bench_markdown(result);
    fmt::print("{}", markdown);
    if (!markdown_path.empty()) {
        write_text(markdown_path, markdown);
    }
    if (!json_path.empty()) {
        nlohmann::json json = ada::to_json(result);
        json["ada"] = std::string{ada::build_info_string()};
        write_text(json_path, json.dump(2) + "\n");
    }
    return result.pass ? kExitPass : kExitFail;
}

int run_export(const std::filesystem::path& scene_path,
               const std::string& output,
               bool solve_first) {
    ada::Scene scene = ada::read_scene(scene_path);
    if (solve_first) {
        ada::solve_scene(scene);
    }
    ada::write_iges(scene, output);
    fmt::print("exported {} patches to {}\n", scene.patches.size(), output);
    return kExitPass;
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

    std::filesystem::path solve_path;
    std::string solve_output;
    bool solve_economy = false;
    CLI::App* solve = app.add_subcommand(
        "solve", "Solve a scene's recipes, write the control points back, then report");
    solve->add_option("scene", solve_path, "Scene file (.ada.json)")
        ->required()
        ->check(CLI::ExistingFile);
    solve->add_option(
        "-o,--output", solve_output, "Write the solved scene here instead of in place");
    solve->add_flag("--economy",
                    solve_economy,
                    "Fit with CV economy: start at the curves' degrees and raise patches until "
                    "the fit budgets are met");

    std::filesystem::path bench_dir;
    std::string bench_json;
    std::string bench_markdown;
    CLI::App* bench = app.add_subcommand(
        "bench", "Solve every scene in a directory in memory and check it against its budgets");
    bench->add_option("directory", bench_dir, "Directory of .ada.json cases")
        ->required()
        ->check(CLI::ExistingDirectory);
    bench->add_option("--json", bench_json, "Also write the scorecard as JSON to this path");
    bench->add_option(
        "--markdown", bench_markdown, "Also write the scorecard as Markdown to this path");

    std::filesystem::path export_path;
    std::string export_output;
    bool export_solve = false;
    CLI::App* export_cmd =
        app.add_subcommand("export", "Write a scene's patches as IGES (entity 128)");
    export_cmd->add_option("scene", export_path, "Scene file (.ada.json)")
        ->required()
        ->check(CLI::ExistingFile);
    export_cmd->add_option("-o,--output", export_output, "IGES file to write")->required();
    export_cmd->add_flag(
        "--solve", export_solve, "Solve the scene in memory first (for authored cases)");

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
        if (solve->parsed()) {
            return run_solve(solve_path, solve_output, solve_economy);
        }
        if (bench->parsed()) {
            return run_bench(bench_dir, bench_json, bench_markdown);
        }
        if (export_cmd->parsed()) {
            return run_export(export_path, export_output, export_solve);
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
        return kExitError;
    }

    fmt::print("{}", app.help());
    return kExitPass;
}
