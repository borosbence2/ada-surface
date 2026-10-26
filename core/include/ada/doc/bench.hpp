#pragma once

// The case benchmark: solve every scene in a directory in memory, check it
// against its own budgets, and summarise the lot as a scorecard. The case
// files are never written.

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace ada {

struct BenchCase {
    std::string name; ///< File stem, e.g. "L2-hood-2x2-sym".
    bool pass = false;
    std::string error; ///< Set when the case could not be read or solved.
    std::size_t patches = 0;
    std::size_t unknowns = 0;
    double solve_ms = 0.0;
    double report_ms = 0.0;
    double worst_gap_mm = 0.0;    ///< Over all edge and symmetry links.
    double worst_angle_deg = 0.0; ///< Over all edge and symmetry links.
    double worst_curvature = 0.0; ///< Relative G2 deviation, over G2 edge links.
    double curve_moved_mm = 0.0;  ///< Largest curve adjustment the solve made.
    std::size_t highlight_inflections = 0;
    std::size_t zigzag_rows = 0;
    std::vector<std::string> failed; ///< "where: check = value > budget".
};

struct BenchResult {
    std::vector<BenchCase> cases;
    bool pass = true;
};

/// Every `*.ada.json` directly in `directory` (not in subdirectories), in
/// name order. A case that fails to load or solve is recorded with its error
/// and counts as a failure; the run continues.
BenchResult run_bench(const std::filesystem::path& directory);

/// GitHub-flavoured Markdown: one table row per case, then the failed checks.
std::string bench_markdown(const BenchResult& result);

nlohmann::json to_json(const BenchResult& result);

} // namespace ada
