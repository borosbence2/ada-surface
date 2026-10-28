#include "tools.hpp"

#include "ada/analysis/differential.hpp"
#include "ada/analysis/quality.hpp"
#include "ada/analysis/sampling.hpp"
#include "ada/doc/recipes.hpp"
#include "ada/fit/fit.hpp"
#include "ada/geom/patch_evaluator.hpp"
#include "ada/io/iges.hpp"

#include "base64.hpp"
#include "common.hpp"

#include <Eigen/Geometry>
#include <fmt/format.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace ada::mcp {

namespace {

constexpr int kDefaultTessellationGrid = 49;
constexpr int kMaxTessellationGrid = 257;
constexpr int kDefaultCombSamples = 33;
constexpr int kMaxCombSamples = 513;
/// Points Studio draws for a fit target: enough to see a deviation pattern,
/// few enough to send and draw after every edit.
constexpr int kDefaultDeviationPoints = 60000;
constexpr int kMaxDeviationPoints = 1000000;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

nlohmann::json vec3(const Vec3& v) {
    return nlohmann::json::array({v.x(), v.y(), v.z()});
}

nlohmann::json summary_json(const SceneSession& session, const Scene& scene) {
    nlohmann::json patches = nlohmann::json::array();
    for (const ScenePatch& p : scene.patches) {
        Vec3 lo = Vec3::Constant(std::numeric_limits<double>::infinity());
        Vec3 hi = -lo;
        for (const Vec3& point : p.patch.control_points()) {
            lo = lo.cwiseMin(point);
            hi = hi.cwiseMax(point);
        }
        nlohmann::json recipe = nullptr;
        if (p.recipe.has_value()) {
            if (const auto* fb = std::get_if<FromBoundaries>(&*p.recipe)) {
                recipe = {{"type", "from_boundaries"},
                          {"UMin", fb->curves[0]},
                          {"UMax", fb->curves[1]},
                          {"VMin", fb->curves[2]},
                          {"VMax", fb->curves[3]}};
            } else {
                const auto& b = std::get<Blend>(*p.recipe);
                recipe = {{"type", "blend"},
                          {"from", edge_name(b.from)},
                          {"to", edge_name(b.to)},
                          {"continuity", to_string(b.continuity)}};
            }
        }
        patches.push_back({{"id", p.id},
                           {"recipe", recipe},
                           {"solved", p.solved},
                           {"degree", {p.patch.degree_u(), p.patch.degree_v()}},
                           {"cv_count", p.patch.control_points().size()},
                           {"control_net_bounds", {{"min", vec3(lo)}, {"max", vec3(hi)}}}});
    }
    nlohmann::json edges = nlohmann::json::array();
    for (const EdgeLink& link : all_edge_links(scene)) {
        edges.push_back({{"a", edge_name(link.a)},
                         {"b", edge_name(link.b)},
                         {"continuity", to_string(link.continuity)}});
    }
    nlohmann::json curves = nlohmann::json::array();
    for (const SceneCurve& c : scene.curves) {
        curves.push_back({{"id", c.id},
                          {"degree", c.curve.degree()},
                          {"start", vec3(c.curve.control_points().front())},
                          {"end", vec3(c.curve.control_points().back())}});
    }
    nlohmann::json symmetry = nlohmann::json::array();
    for (const SymmetryLink& link : scene.symmetry) {
        symmetry.push_back(
            {{"edge", edge_name(link.edge)}, {"continuity", to_string(link.continuity)}});
    }
    nlohmann::json targets = nlohmann::json::array();
    for (const SceneTarget& t : scene.targets) {
        targets.push_back({{"id", t.id},
                           {"file", t.file},
                           {"points", t.points ? t.points->size() : 0},
                           {"spacing_mm", t.spacing}});
    }
    nlohmann::json fit = nullptr;
    if (scene.fit.has_value()) {
        fit = {{"target", scene.fit->target}, {"weight", scene.fit->weight}};
    }
    return {{"path", session.path().generic_string()},
            {"description", scene.description},
            {"targets", std::move(targets)},
            {"fit", std::move(fit)},
            {"curves", std::move(curves)},
            {"patches", std::move(patches)},
            {"edges", std::move(edges)},
            {"symmetry", std::move(symmetry)},
            {"budgets", to_json(scene.budgets)}};
}

/// Text form of the summary for clients without structured content and for
/// the agent's transcript.
std::string summary_text(const nlohmann::json& summary) {
    std::string text =
        fmt::format("scene {}: {} curves, {} patches, {} linked edges, {} symmetry links",
                    summary.at("path").get<std::string>(),
                    summary.at("curves").size(),
                    summary.at("patches").size(),
                    summary.at("edges").size(),
                    summary.at("symmetry").size());
    for (const auto& p : summary.at("patches")) {
        text += fmt::format("\n  {} degree {}x{} ({}){}",
                            p.at("id").get<std::string>(),
                            p.at("degree")[0].get<int>(),
                            p.at("degree")[1].get<int>(),
                            p.at("recipe").is_null() ? std::string("fixed")
                                                     : p.at("recipe").at("type").get<std::string>(),
                            p.at("solved").get<bool>() ? "" : " UNSOLVED");
    }
    for (const auto& e : summary.at("edges")) {
        text += fmt::format("\n  {} ~ {} {}",
                            e.at("a").get<std::string>(),
                            e.at("b").get<std::string>(),
                            e.at("continuity").get<std::string>());
    }
    for (const auto& s : summary.at("symmetry")) {
        text += fmt::format("\n  {} ~ mirror (Y = 0) {}",
                            s.at("edge").get<std::string>(),
                            s.at("continuity").get<std::string>());
    }
    return text;
}

nlohmann::json light_json(const LightPlane& light) {
    return {{"origin", vec3(light.origin)},
            {"normal", vec3(light.normal)},
            {"direction", vec3(light.direction)},
            {"across", vec3(light.across)},
            {"spacing", light.spacing}};
}

} // namespace

ToolResult scene_open(SceneSession& session, const nlohmann::json& args) {
    if (!args.contains("path") || !args.at("path").is_string()) {
        throw ToolError("scene_open needs 'path', the scene file to open");
    }
    session.open(args.at("path").get<std::string>());
    const nlohmann::json summary = summary_json(session, session.current());
    return {summary_text(summary), summary, false};
}

ToolResult scene_summary(SceneSession& session) {
    const nlohmann::json summary = summary_json(session, session.current());
    return {summary_text(summary), summary, false};
}

ToolResult quality_report(SceneSession& session) {
    const QualityReport report = compute_quality(session.current());
    nlohmann::json json = to_json(report);
    json["scene"] = session.path().generic_string();
    return {format_report(report), std::move(json), false};
}

ToolResult solve(SceneSession& session, History& history) {
    Scene scene = session.current();
    const Scene before = scene;
    std::string economy;
    SceneSolveSummary summary;
    if (scene.fit.has_value() && scene.fit->economy) {
        const EconomyResult result = fit_economically(scene);
        for (const std::string& step : result.steps) {
            economy += step + "\n";
        }
        economy += result.met ? "CV economy: the fit budgets are met\n"
                              : "CV economy: the fit budgets are out of reach at the degree cap\n";
        summary = result.last;
    } else {
        summary = solve_scene(scene);
    }
    history.record(before, "solved the scene");
    write_scene(scene, session.path());

    nlohmann::json curves = nlohmann::json::array();
    std::string moved;
    for (const CurveDeviation& c : summary.curves) {
        curves.push_back({{"curve", c.curve}, {"max_mm", c.max_mm}});
        if (c.max_mm > kReportedDeviationMm) {
            moved +=
                fmt::format("curve {} moved up to {:.3f} mm for continuity\n", c.curve, c.max_mm);
        }
    }
    const QualityReport report = compute_quality(scene);
    std::string text = fmt::format(
        "solved {} ({} stages, {} unknowns, max residual {:.2e} mm, {:.1f} ms) and wrote it back\n",
        session.path().generic_string(),
        summary.stages,
        summary.unknowns,
        summary.max_residual,
        summary.milliseconds);
    text += economy + moved + "\n" + format_report(report);
    nlohmann::json structured = {{"stages", summary.stages},
                                 {"unknowns", summary.unknowns},
                                 {"max_residual_mm", summary.max_residual},
                                 {"milliseconds", summary.milliseconds},
                                 {"curves", std::move(curves)},
                                 {"report", to_json(report)}};
    return {std::move(text), std::move(structured), false};
}

ToolResult export_iges(SceneSession& session, const nlohmann::json& args) {
    if (!args.contains("path") || !args.at("path").is_string()) {
        throw ToolError("export_iges needs 'path', the IGES file to write");
    }
    std::filesystem::path target = args.at("path").get<std::string>();
    if (target.is_relative()) {
        target = session.path().parent_path() / target;
    }
    const Scene& scene = session.current();
    try {
        write_iges(scene, target);
    } catch (const std::invalid_argument& e) {
        throw ToolError(e.what());
    }
    return {fmt::format("exported {} patches to {}", scene.patches.size(), target.generic_string()),
            {{"path", target.generic_string()}, {"patches", scene.patches.size()}},
            false};
}

ToolResult tessellate(SceneSession& session, const nlohmann::json& args) {
    const int grid =
        integer_argument(args, "grid", kDefaultTessellationGrid, 2, kMaxTessellationGrid);
    const Scene& scene = session.current();
    // The same light the quality report uses, so Studio's highlight lines are
    // the ones the report counted.
    const LightPlane scene_light = resolve_light(scene);

    const std::vector<double> params = uniform_parameters(static_cast<std::size_t>(grid));
    const auto n = static_cast<std::uint32_t>(grid);

    std::vector<std::uint32_t> indices;
    for (std::uint32_t i = 0; i + 1 < n; ++i) {
        for (std::uint32_t j = 0; j + 1 < n; ++j) {
            const std::uint32_t a = i * n + j;
            const std::uint32_t b = (i + 1) * n + j;
            const std::uint32_t c = (i + 1) * n + j + 1;
            const std::uint32_t d = i * n + j + 1;
            // Counter-clockwise in (u, v), so the triangle normal agrees with
            // Su x Sv, the surface normal everything else uses.
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    }
    const std::string encoded_indices = encode_u32(indices);

    nlohmann::json patches = nlohmann::json::array();
    for (const ScenePatch& p : scene.patches) {
        const PatchEvaluator evaluator(p.patch);
        std::vector<double> positions;
        std::vector<double> normals;
        std::vector<double> uvs;
        std::vector<double> gaussian;
        std::vector<double> mean;
        std::vector<double> highlight;
        for (const double u : params) {
            for (const double v : params) {
                const PatchDerivatives d = evaluator.derivatives(u, v);
                const std::optional<Vec3> normal = unit_normal(d);
                const std::optional<n2s::SurfaceCurvature> k = curvature(d);
                const Vec3 n_or_zero = normal.value_or(Vec3::Zero());
                positions.insert(positions.end(),
                                 {d.position().x(), d.position().y(), d.position().z()});
                normals.insert(normals.end(), {n_or_zero.x(), n_or_zero.y(), n_or_zero.z()});
                uvs.insert(uvs.end(), {u, v});
                gaussian.push_back(k.has_value() ? k->gaussian : kNaN);
                mean.push_back(k.has_value() ? k->mean : kNaN);
                highlight.push_back(normal.has_value()
                                        ? highlight_parameter(scene_light, d.position(), *normal)
                                        : kNaN);
            }
        }
        patches.push_back({{"id", p.id},
                           {"vertex_count", params.size() * params.size()},
                           {"positions", encode_f32(positions)},
                           {"normals", encode_f32(normals)},
                           {"uvs", encode_f32(uvs)},
                           {"indices", encoded_indices},
                           {"gaussian", encode_f32(gaussian)},
                           {"mean", encode_f32(mean)},
                           {"highlight", encode_f32(highlight)}});
    }

    nlohmann::json out = {{"grid", grid},
                          {"encoding", "base64 little-endian float32; indices uint32"},
                          {"light", light_json(scene_light)},
                          {"patches", std::move(patches)}};
    return {
        fmt::format("tessellated {} patches at {}x{} vertices", scene.patches.size(), grid, grid),
        std::move(out),
        false};
}

ToolResult cv_hulls(SceneSession& session) {
    const Scene& scene = session.current();
    nlohmann::json patches = nlohmann::json::array();
    for (const ScenePatch& p : scene.patches) {
        nlohmann::json points = nlohmann::json::array();
        for (const Vec3& point : p.patch.control_points()) {
            points.push_back(vec3(point));
        }
        patches.push_back({{"id", p.id},
                           {"degree", {p.patch.degree_u(), p.patch.degree_v()}},
                           {"control_points", std::move(points)}});
    }
    return {fmt::format("{} control nets", scene.patches.size()),
            {{"patches", std::move(patches)}},
            false};
}

ToolResult boundary_combs(SceneSession& session, const nlohmann::json& args) {
    const int samples = integer_argument(args, "samples", kDefaultCombSamples, 2, kMaxCombSamples);
    const Scene& scene = session.current();
    const std::vector<double> params = uniform_parameters(static_cast<std::size_t>(samples));

    nlohmann::json patches = nlohmann::json::array();
    for (const ScenePatch& p : scene.patches) {
        nlohmann::json edges = nlohmann::json::array();
        for (const Edge edge : {Edge::UMin, Edge::UMax, Edge::VMin, Edge::VMax}) {
            const BezierCurve curve = p.patch.boundary(edge);
            const BezierCurve d1 = curve.hodograph(1);
            const BezierCurve d2 = curve.hodograph(2);
            std::vector<double> points;
            std::vector<double> directions;
            std::vector<double> curvature_values;
            for (const double t : params) {
                const Vec3 c = curve.evaluate(t);
                const Vec3 first = d1.evaluate(t);
                const Vec3 second = d2.evaluate(t);
                const double speed = first.norm();
                double k = 0.0;
                Vec3 away = Vec3::Zero();
                if (speed > 0.0) {
                    const Vec3 tangent = first / speed;
                    k = first.cross(second).norm() / (speed * speed * speed);
                    const Vec3 inward = second - second.dot(tangent) * tangent;
                    if (inward.norm() > 0.0) {
                        // Combs point away from the centre of curvature, so
                        // their envelope sits on the convex side.
                        away = -inward.normalized();
                    }
                }
                points.insert(points.end(), {c.x(), c.y(), c.z()});
                directions.insert(directions.end(), {away.x(), away.y(), away.z()});
                curvature_values.push_back(k);
            }
            edges.push_back({{"edge", to_string(edge)},
                             {"points", std::move(points)},
                             {"directions", std::move(directions)},
                             {"curvature", std::move(curvature_values)}});
        }
        patches.push_back({{"id", p.id}, {"edges", std::move(edges)}});
    }
    return {fmt::format("curvature combs on {} patches, {} samples per edge",
                        scene.patches.size(),
                        samples),
            {{"patches", std::move(patches)}},
            false};
}

ToolResult fit_deviations(SceneSession& session, const nlohmann::json& args) {
    const int max_points =
        integer_argument(args, "max_points", kDefaultDeviationPoints, 1, kMaxDeviationPoints);
    const Scene& scene = session.current();
    if (!scene.fit.has_value()) {
        return {"the scene fits no target", {{"fit", nullptr}}, false};
    }
    const SceneTarget& target = scene.target(scene.fit->target);
    if (!target.points) {
        throw ToolError(fmt::format("target '{}' is not loaded", target.id), "target_not_loaded");
    }
    const std::vector<Vec3>& all = *target.points;
    const std::size_t stride =
        std::max<std::size_t>(1,
                              (all.size() + static_cast<std::size_t>(max_points) - 1) /
                                  static_cast<std::size_t>(max_points));
    std::vector<Vec3> picked;
    for (std::size_t k = 0; k < all.size(); k += stride) {
        picked.push_back(all[k]);
    }
    std::vector<BezierPatch> patches;
    for (const ScenePatch& p : scene.patches) {
        patches.push_back(p.patch);
    }
    const std::vector<FitAssignment> assignment = assign_points(patches, picked);

    // A patch's normal may point either way (this depends on how its curves
    // run), so the sign is turned to a fixed meaning: positive towards the
    // positive end of the axis the surface mostly faces. Above a hood or roof,
    // outboard of a right-hand (y >= 0) door.
    Vec3 facing = Vec3::Zero();
    for (const BezierPatch& p : patches) {
        if (const std::optional<Vec3> n = unit_normal(PatchEvaluator(p).derivatives(0.5, 0.5))) {
            facing += n->cwiseAbs();
        }
    }
    Eigen::Index axis = 2;
    facing.maxCoeff(&axis);
    std::vector<PatchEvaluator> evaluators(patches.begin(), patches.end());

    std::vector<double> positions;
    std::vector<double> deviation;
    for (std::size_t k = 0; k < picked.size(); ++k) {
        positions.insert(positions.end(), {picked[k].x(), picked[k].y(), picked[k].z()});
        const FitAssignment& a = assignment[k];
        if (a.outside) {
            deviation.push_back(kNaN);
            continue;
        }
        const std::optional<Vec3> n = unit_normal(evaluators[a.patch].derivatives(a.u, a.v));
        const double sign = n.has_value() && (*n)(axis) < 0.0 ? -1.0 : 1.0;
        deviation.push_back(sign * a.signed_distance);
    }
    nlohmann::json budget = nullptr;
    if (scene.budgets.fit_max_mm.has_value()) {
        budget = *scene.budgets.fit_max_mm;
    }
    return {fmt::format("{} of {} target points with their deviation", picked.size(), all.size()),
            {{"fit",
              {{"target", target.id},
               {"total", all.size()},
               {"count", picked.size()},
               {"positions", encode_f32(positions)},
               {"deviation", encode_f32(deviation)},
               {"budget_mm", budget},
               {"positive_towards", std::string("+") + "xyz"[axis]}}}},
            false};
}

} // namespace ada::mcp
