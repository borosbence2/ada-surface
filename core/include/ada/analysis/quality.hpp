#pragma once

// The quality report: the contract between the kernel, the agent and Studio.
// Every geometry change is followed by one, and its checks decide pass/fail.

#include "ada/analysis/budgets.hpp"
#include "ada/analysis/continuity.hpp"
#include "ada/analysis/curvature.hpp"
#include "ada/analysis/cv_hull.hpp"
#include "ada/analysis/highlight.hpp"
#include "ada/doc/scene.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace ada {

/// Sampling densities. The defaults resolve the waviness a degree-7 patch can
/// have; raising them refines the measurements, never changes their meaning.
struct AnalysisSettings {
    std::size_t surface_grid = 33;   ///< Curvature samples per direction.
    std::size_t highlight_grid = 49; ///< Highlight field samples per direction.
    std::size_t edge_samples = 33;   ///< Samples along each checked edge.
};

/// One measured value against its budget. Booleans are reported as 0/1 with a
/// budget of 0.
struct Check {
    std::string name;
    double value = 0.0;
    double budget = 0.0;
    bool pass = true;
};

struct PatchReport {
    std::string id;
    int degree_u = 0;
    int degree_v = 0;
    std::size_t cv_count = 0;
    CurvatureSummary curvature;
    HighlightSummary highlights;
    CvHullSummary hull;
    std::vector<Check> checks;
    bool pass = true;
};

struct EdgeReport {
    EdgeLink link;
    EdgeContinuity measure;
    std::vector<Check> checks;
    bool pass = true;
};

struct SymmetryReport {
    SymmetryLink link;
    SymmetryContinuity measure;
    std::vector<Check> checks;
    bool pass = true;
};

struct QualityReport {
    Budgets budgets;
    LightPlane light;
    std::vector<PatchReport> patches;
    std::vector<EdgeReport> edges;
    std::vector<SymmetryReport> symmetry;
    bool pass = true;
};

/// The light the highlight analysis uses: the scene's own, or
/// `default_light_plane` over the bounding box of all control points, facing
/// the way most of the surface does.
LightPlane resolve_light(const Scene& scene);

QualityReport compute_quality(const Scene& scene, const AnalysisSettings& settings = {});

nlohmann::json to_json(const QualityReport& report);

/// Plain-text summary for a terminal: one line per patch, edge and symmetry
/// link, then every failed check.
std::string format_report(const QualityReport& report);

} // namespace ada
