#include "ada/doc/bench.hpp"

#include "ada/analysis/quality.hpp"
#include "ada/doc/recipes.hpp"
#include "ada/doc/scene.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <exception>

namespace ada {

namespace {

double since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

void collect_failures(BenchCase& c, const std::string& where, const std::vector<Check>& checks) {
    for (const Check& check : checks) {
        if (!check.pass) {
            c.failed.push_back(fmt::format(
                "{}: {} = {:.4g} > {:.4g}", where, check.name, check.value, check.budget));
        }
    }
}

BenchCase run_case(const std::filesystem::path& path) {
    BenchCase c;
    c.name = path.filename().string();
    if (const auto dot = c.name.find(".ada.json"); dot != std::string::npos) {
        c.name.resize(dot);
    }
    try {
        Scene scene = read_scene(path);
        c.patches = scene.patches.size();

        const auto t0 = std::chrono::steady_clock::now();
        const SceneSolveSummary solved = scene.fit.has_value() && scene.fit->economy
                                             ? fit_economically(scene).last
                                             : solve_scene(scene);
        c.solve_ms = since(t0);
        c.unknowns = solved.unknowns;
        for (const CurveDeviation& d : solved.curves) {
            c.curve_moved_mm = std::max(c.curve_moved_mm, d.max_mm);
        }

        const auto t1 = std::chrono::steady_clock::now();
        const QualityReport report = compute_quality(scene);
        c.report_ms = since(t1);
        c.pass = report.pass;

        for (const PatchReport& p : report.patches) {
            c.highlight_inflections =
                std::max(c.highlight_inflections, p.highlights.max_inflections);
            c.zigzag_rows += p.hull.zigzag_rows;
            collect_failures(c, p.id, p.checks);
        }
        for (const EdgeReport& e : report.edges) {
            c.worst_gap_mm = std::max(c.worst_gap_mm, e.measure.max_gap);
            if (e.link.continuity != Continuity::G0) {
                c.worst_angle_deg = std::max(c.worst_angle_deg, e.measure.max_normal_angle_deg);
            }
            if (e.link.continuity == Continuity::G2) {
                c.worst_curvature = std::max(c.worst_curvature, e.measure.max_curvature_deviation);
            }
            collect_failures(c,
                             fmt::format("{}.{} ~ {}.{}",
                                         e.link.a.patch,
                                         to_string(e.link.a.edge),
                                         e.link.b.patch,
                                         to_string(e.link.b.edge)),
                             e.checks);
        }
        for (const SymmetryReport& s : report.symmetry) {
            c.worst_gap_mm = std::max(c.worst_gap_mm, s.measure.max_plane_distance);
            c.worst_angle_deg = std::max(c.worst_angle_deg, s.measure.max_normal_tilt_deg);
            collect_failures(
                c,
                fmt::format("{}.{} (Y=0)", s.link.edge.patch, to_string(s.link.edge.edge)),
                s.checks);
        }
    } catch (const std::exception& e) {
        c.pass = false;
        c.error = e.what();
    }
    return c;
}

} // namespace

BenchResult run_bench(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.size() > 9 && name.ends_with(".ada.json")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    BenchResult result;
    for (const auto& file : files) {
        result.cases.push_back(run_case(file));
        result.pass = result.pass && result.cases.back().pass;
    }
    return result;
}

std::string bench_markdown(const BenchResult& result) {
    std::string out =
        fmt::format("### ADA bench: {}\n\n", result.pass ? "all cases pass" : "FAILURES");
    out += "| case | result | patches | unknowns | solve ms | report ms | worst G0 mm | worst G1 "
           "deg | "
           "worst G2 | curve moved mm | highlight infl. | zig-zag rows |\n";
    out += "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const BenchCase& c : result.cases) {
        out += fmt::format("| {} | {} | {} | {} | {:.1f} | {:.1f} | {:.1e} | {:.1e} | {:.1e} | "
                           "{:.3f} | {} | {} |\n",
                           c.name,
                           c.error.empty() ? (c.pass ? "pass" : "**FAIL**") : "**ERROR**",
                           c.patches,
                           c.unknowns,
                           c.solve_ms,
                           c.report_ms,
                           c.worst_gap_mm,
                           c.worst_angle_deg,
                           c.worst_curvature,
                           c.curve_moved_mm,
                           c.highlight_inflections,
                           c.zigzag_rows);
    }
    std::string problems;
    for (const BenchCase& c : result.cases) {
        if (!c.error.empty()) {
            problems += fmt::format("- **{}**: {}\n", c.name, c.error);
        }
        for (const std::string& f : c.failed) {
            problems += fmt::format("- **{}**: {}\n", c.name, f);
        }
    }
    if (!problems.empty()) {
        out += "\n" + problems;
    }
    return out;
}

nlohmann::json to_json(const BenchResult& result) {
    nlohmann::json cases = nlohmann::json::array();
    for (const BenchCase& c : result.cases) {
        cases.push_back({{"name", c.name},
                         {"pass", c.pass},
                         {"error", c.error},
                         {"patches", c.patches},
                         {"unknowns", c.unknowns},
                         {"solve_ms", c.solve_ms},
                         {"report_ms", c.report_ms},
                         {"worst_gap_mm", c.worst_gap_mm},
                         {"worst_angle_deg", c.worst_angle_deg},
                         {"worst_curvature", c.worst_curvature},
                         {"curve_moved_mm", c.curve_moved_mm},
                         {"highlight_inflections", c.highlight_inflections},
                         {"zigzag_rows", c.zigzag_rows},
                         {"failed", c.failed}});
    }
    return {{"pass", result.pass}, {"cases", std::move(cases)}};
}

} // namespace ada
