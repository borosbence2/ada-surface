#include "ada/analysis/quality.hpp"

#include "ada/analysis/differential.hpp"
#include "ada/doc/recipes.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <limits>
#include <map>
#include <string>

namespace ada {

namespace {

/// Samples per patch side when finding which way a scene faces.
constexpr int kFacingSamples = 4;

Check at_most(std::string name, double value, double budget) {
    return {std::move(name), value, budget, value <= budget};
}

Check flag(std::string name, bool set) {
    return at_most(std::move(name), set ? 1.0 : 0.0, 0.0);
}

bool all_pass(const std::vector<Check>& checks) {
    return std::all_of(checks.begin(), checks.end(), [](const Check& c) { return c.pass; });
}

std::string edge_name(const EdgeRef& ref) {
    return fmt::format("{}.{}", ref.patch, to_string(ref.edge));
}

nlohmann::json to_json(const std::vector<Check>& checks) {
    nlohmann::json out = nlohmann::json::array();
    for (const Check& c : checks) {
        out.push_back(
            {{"name", c.name}, {"value", c.value}, {"budget", c.budget}, {"pass", c.pass}});
    }
    return out;
}

nlohmann::json vec3_json(const Vec3& v) {
    return nlohmann::json::array({v.x(), v.y(), v.z()});
}

} // namespace

LightPlane resolve_light(const Scene& scene) {
    if (scene.light.has_value()) {
        return *scene.light;
    }
    Vec3 lo = Vec3::Constant(std::numeric_limits<double>::infinity());
    Vec3 hi = Vec3::Constant(-std::numeric_limits<double>::infinity());
    for (const ScenePatch& p : scene.patches) {
        for (const Vec3& point : p.patch.control_points()) {
            lo = lo.cwiseMin(point);
            hi = hi.cwiseMax(point);
        }
    }
    if (scene.patches.empty()) {
        return default_light_plane(Vec3::Zero(), Vec3::Zero(), Vec3::UnitZ());
    }

    // Which way the surfaces mostly face: the axis carrying the largest share
    // of their normals. The light goes on the side the part would be seen
    // from: above for Z, outboard (the side the part is on) for Y, ahead for X.
    Vec3 share = Vec3::Zero();
    for (const ScenePatch& p : scene.patches) {
        const PatchEvaluator e(p.patch);
        for (int i = 0; i <= kFacingSamples; ++i) {
            for (int j = 0; j <= kFacingSamples; ++j) {
                const auto n = unit_normal(e.derivatives(static_cast<double>(i) / kFacingSamples,
                                                         static_cast<double>(j) / kFacingSamples));
                if (n.has_value()) {
                    share += n->cwiseAbs();
                }
            }
        }
    }
    Eigen::Index axis = 2;
    share.maxCoeff(&axis);
    Vec3 facing = Vec3::Zero();
    const Vec3 centre = 0.5 * (lo + hi);
    facing(axis) = axis == 1 && centre.y() < 0.0 ? -1.0 : 1.0;
    return default_light_plane(lo, hi, facing);
}

QualityReport compute_quality(const Scene& scene, const AnalysisSettings& settings) {
    QualityReport report;
    report.budgets = scene.budgets;
    report.light = resolve_light(scene);
    const Budgets& b = scene.budgets;

    std::map<std::string, PatchEvaluator> evaluators;
    for (const ScenePatch& p : scene.patches) {
        evaluators.emplace(p.id, PatchEvaluator(p.patch));
    }

    for (const ScenePatch& p : scene.patches) {
        const PatchEvaluator& e = evaluators.at(p.id);
        PatchReport r;
        r.id = p.id;
        r.degree_u = p.patch.degree_u();
        r.degree_v = p.patch.degree_v();
        r.cv_count = p.patch.control_points().size();
        r.curvature = summarize_curvature(e, settings.surface_grid);
        r.highlights = analyze_highlights(e, report.light, settings.highlight_grid);
        r.hull = analyze_cv_hull(e);

        if (!p.solved) {
            // Only the recipe's starting guess: nothing below means anything
            // until `ada solve` has run.
            r.checks.push_back(flag("unsolved", true));
        }
        r.checks.push_back(at_most("cv_zigzag_rows",
                                   static_cast<double>(r.hull.zigzag_rows),
                                   static_cast<double>(b.max_zigzag_rows)));
        if (r.curvature.undefined_samples > 0) {
            r.checks.push_back(at_most(
                "degenerate_samples", static_cast<double>(r.curvature.undefined_samples), 0.0));
        }
        if (b.max_highlight_inflections.has_value()) {
            r.checks.push_back(at_most("highlight_inflections",
                                       static_cast<double>(r.highlights.max_inflections),
                                       static_cast<double>(*b.max_highlight_inflections)));
        }
        if (b.max_highlight_curvature_rate.has_value()) {
            r.checks.push_back(at_most("highlight_curvature_rate",
                                       r.highlights.max_curvature_rate,
                                       *b.max_highlight_curvature_rate));
        }
        if (b.allow_curvature_reversal.has_value() && !*b.allow_curvature_reversal) {
            r.checks.push_back(flag("curvature_reversal", r.curvature.mean_changes_sign));
        }
        if (b.max_degree.has_value()) {
            r.checks.push_back(at_most("degree",
                                       static_cast<double>(std::max(r.degree_u, r.degree_v)),
                                       static_cast<double>(*b.max_degree)));
        }
        r.pass = all_pass(r.checks);
        report.patches.push_back(std::move(r));
    }

    // Explicit links and the ones recipes imply (a blend's joins).
    for (const EdgeLink& link : all_edge_links(scene)) {
        EdgeReport r{link,
                     measure_edge(evaluators.at(link.a.patch),
                                  link.a.edge,
                                  evaluators.at(link.b.patch),
                                  link.b.edge,
                                  settings.edge_samples),
                     {},
                     true};
        r.checks.push_back(at_most("g0_gap_mm", r.measure.max_gap, b.g0_gap_mm));
        if (link.continuity != Continuity::G0) {
            r.checks.push_back(
                at_most("g1_angle_deg", r.measure.max_normal_angle_deg, b.g1_angle_deg));
            r.checks.push_back(flag("fold", r.measure.fold));
        }
        if (link.continuity == Continuity::G2) {
            r.checks.push_back(
                at_most("g2_relative", r.measure.max_curvature_deviation, b.g2_relative));
        }
        if (r.measure.undefined_samples > 0) {
            r.checks.push_back(at_most(
                "degenerate_samples", static_cast<double>(r.measure.undefined_samples), 0.0));
        }
        r.pass = all_pass(r.checks);
        report.edges.push_back(std::move(r));
    }

    for (const SymmetryLink& link : scene.symmetry) {
        SymmetryReport r{
            link,
            measure_symmetry(evaluators.at(link.edge.patch), link.edge.edge, settings.edge_samples),
            {},
            true};
        r.checks.push_back(
            at_most("g0_plane_distance_mm", r.measure.max_plane_distance, b.g0_gap_mm));
        // Mirror G2 follows from mirror G1 (see continuity.hpp), so a G2
        // requirement is checked through the G1 tilt.
        if (link.continuity != Continuity::G0) {
            r.checks.push_back(
                at_most("g1_normal_tilt_deg", r.measure.max_normal_tilt_deg, b.g1_angle_deg));
        }
        if (r.measure.undefined_samples > 0) {
            r.checks.push_back(at_most(
                "degenerate_samples", static_cast<double>(r.measure.undefined_samples), 0.0));
        }
        r.pass = all_pass(r.checks);
        report.symmetry.push_back(std::move(r));
    }

    report.pass =
        std::all_of(
            report.patches.begin(), report.patches.end(), [](const auto& r) { return r.pass; }) &&
        std::all_of(
            report.edges.begin(), report.edges.end(), [](const auto& r) { return r.pass; }) &&
        std::all_of(
            report.symmetry.begin(), report.symmetry.end(), [](const auto& r) { return r.pass; });
    return report;
}

nlohmann::json to_json(const QualityReport& report) {
    nlohmann::json patches = nlohmann::json::array();
    for (const PatchReport& r : report.patches) {
        const CurvatureSummary& k = r.curvature;
        patches.push_back({
            {"id", r.id},
            {"degree", {r.degree_u, r.degree_v}},
            {"cv_count", r.cv_count},
            {"curvature",
             {{"gaussian", {k.gaussian_min, k.gaussian_max}},
              {"mean", {k.mean_min, k.mean_max}},
              {"max_abs_principal", k.max_abs_principal},
              {"saddle_fraction", k.saddle_fraction},
              {"gaussian_changes_sign", k.gaussian_changes_sign},
              {"mean_changes_sign", k.mean_changes_sign},
              {"samples", k.samples},
              {"undefined_samples", k.undefined_samples}}},
            {"highlights",
             {{"lines", r.highlights.lines},
              {"max_inflections", r.highlights.max_inflections},
              {"max_curvature_rate", r.highlights.max_curvature_rate}}},
            {"cv_hull",
             {{"rows", r.hull.rows},
              {"zigzag_rows", r.hull.zigzag_rows},
              {"max_row_inflections", r.hull.max_row_inflections},
              {"max_turning_angle_deg", r.hull.max_turning_angle_deg}}},
            {"checks", to_json(r.checks)},
            {"pass", r.pass},
        });
    }

    nlohmann::json edges = nlohmann::json::array();
    for (const EdgeReport& r : report.edges) {
        const EdgeContinuity& m = r.measure;
        edges.push_back({
            {"a", edge_name(r.link.a)},
            {"b", edge_name(r.link.b)},
            {"continuity", to_string(r.link.continuity)},
            {"g0_gap_mm", m.max_gap},
            {"g1_angle_deg", m.max_normal_angle_deg},
            {"g2_relative", m.max_curvature_deviation},
            {"g2_absolute", m.max_curvature_difference},
            {"fold", m.fold},
            {"orientation_flipped", m.orientation_flipped},
            {"samples", m.samples},
            {"undefined_samples", m.undefined_samples},
            {"checks", to_json(r.checks)},
            {"pass", r.pass},
        });
    }

    nlohmann::json symmetry = nlohmann::json::array();
    for (const SymmetryReport& r : report.symmetry) {
        symmetry.push_back({
            {"edge", edge_name(r.link.edge)},
            {"continuity", to_string(r.link.continuity)},
            {"g0_plane_distance_mm", r.measure.max_plane_distance},
            {"g1_normal_tilt_deg", r.measure.max_normal_tilt_deg},
            {"g2_implied_by_g1", true},
            {"samples", r.measure.samples},
            {"undefined_samples", r.measure.undefined_samples},
            {"checks", to_json(r.checks)},
            {"pass", r.pass},
        });
    }

    return {
        {"pass", report.pass},
        {"budgets", to_json(report.budgets)},
        {"light",
         {{"origin", vec3_json(report.light.origin)},
          {"normal", vec3_json(report.light.normal)},
          {"direction", vec3_json(report.light.direction)},
          {"spacing", report.light.spacing}}},
        {"patches", std::move(patches)},
        {"edges", std::move(edges)},
        {"symmetry", std::move(symmetry)},
    };
}

std::string format_report(const QualityReport& report) {
    auto verdict = [](bool pass) { return pass ? "pass" : "FAIL"; };
    std::string out = fmt::format("quality: {}\n", report.pass ? "PASS" : "FAIL");

    if (!report.patches.empty()) {
        out += "\npatches\n";
    }
    for (const PatchReport& r : report.patches) {
        out += fmt::format("  {:<14} {}x{} {:>3} CVs  min radius {:>10.1f} mm  highlights {:>2} "
                           "(max {} inflections)  zig-zag rows {}  {}\n",
                           r.id,
                           r.degree_u,
                           r.degree_v,
                           r.cv_count,
                           r.curvature.max_abs_principal > 0.0
                               ? 1.0 / r.curvature.max_abs_principal
                               : std::numeric_limits<double>::infinity(),
                           r.highlights.lines,
                           r.highlights.max_inflections,
                           r.hull.zigzag_rows,
                           verdict(r.pass));
    }

    if (!report.edges.empty()) {
        out += "\nedges\n";
    }
    for (const EdgeReport& r : report.edges) {
        out +=
            fmt::format("  {:<28} {}  gap {:9.2e} mm  angle {:9.2e} deg  curvature {:9.2e}{}  {}\n",
                        fmt::format("{} ~ {}", edge_name(r.link.a), edge_name(r.link.b)),
                        to_string(r.link.continuity),
                        r.measure.max_gap,
                        r.measure.max_normal_angle_deg,
                        r.measure.max_curvature_deviation,
                        r.measure.fold ? "  FOLD" : "",
                        verdict(r.pass));
    }

    if (!report.symmetry.empty()) {
        out += "\nsymmetry (Y = 0)\n";
    }
    for (const SymmetryReport& r : report.symmetry) {
        out += fmt::format("  {:<28} {}  off-plane {:9.2e} mm  tilt {:9.2e} deg  {}\n",
                           edge_name(r.link.edge),
                           to_string(r.link.continuity),
                           r.measure.max_plane_distance,
                           r.measure.max_normal_tilt_deg,
                           verdict(r.pass));
    }

    std::string failures;
    auto list = [&failures](const std::string& where, const std::vector<Check>& checks) {
        for (const Check& c : checks) {
            if (!c.pass) {
                failures += fmt::format(
                    "  {}: {} = {:.4g} exceeds {:.4g}\n", where, c.name, c.value, c.budget);
            }
        }
    };
    for (const PatchReport& r : report.patches) {
        list("patch " + r.id, r.checks);
    }
    for (const EdgeReport& r : report.edges) {
        list("edge " + edge_name(r.link.a) + " ~ " + edge_name(r.link.b), r.checks);
    }
    for (const SymmetryReport& r : report.symmetry) {
        list("symmetry " + edge_name(r.link.edge), r.checks);
    }
    if (!failures.empty()) {
        out += "\nfailed checks\n" + failures;
    }
    return out;
}

} // namespace ada
