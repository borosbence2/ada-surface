#include "tools.hpp"

#include "ada/analysis/differential.hpp"
#include "ada/analysis/quality.hpp"
#include "ada/analysis/sampling.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "base64.hpp"

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

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

int integer_argument(const nlohmann::json& args, const char* name, int fallback, int lo, int hi) {
    if (!args.contains(name)) {
        return fallback;
    }
    const nlohmann::json& value = args.at(name);
    if (!value.is_number_integer()) {
        throw ToolError(fmt::format("'{}' must be an integer", name));
    }
    const int n = value.get<int>();
    if (n < lo || n > hi) {
        throw ToolError(fmt::format("'{}' must be between {} and {}, got {}", name, lo, hi, n));
    }
    return n;
}

nlohmann::json vec3(const Vec3& v) {
    return nlohmann::json::array({v.x(), v.y(), v.z()});
}

std::string edge_name(const EdgeRef& ref) {
    return fmt::format("{}.{}", ref.patch, to_string(ref.edge));
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
        patches.push_back({{"id", p.id},
                           {"degree", {p.patch.degree_u(), p.patch.degree_v()}},
                           {"cv_count", p.patch.control_points().size()},
                           {"control_net_bounds", {{"min", vec3(lo)}, {"max", vec3(hi)}}}});
    }
    nlohmann::json edges = nlohmann::json::array();
    for (const EdgeLink& link : scene.edges) {
        edges.push_back({{"a", edge_name(link.a)},
                         {"b", edge_name(link.b)},
                         {"continuity", to_string(link.continuity)}});
    }
    nlohmann::json symmetry = nlohmann::json::array();
    for (const SymmetryLink& link : scene.symmetry) {
        symmetry.push_back(
            {{"edge", edge_name(link.edge)}, {"continuity", to_string(link.continuity)}});
    }
    return {{"path", session.path().generic_string()},
            {"patches", std::move(patches)},
            {"edges", std::move(edges)},
            {"symmetry", std::move(symmetry)},
            {"budgets", to_json(scene.budgets)}};
}

/// Text form of the summary for clients without structured content and for
/// the agent's transcript.
std::string summary_text(const nlohmann::json& summary) {
    std::string text = fmt::format("scene {}: {} patches, {} linked edges, {} symmetry links",
                                   summary.at("path").get<std::string>(),
                                   summary.at("patches").size(),
                                   summary.at("edges").size(),
                                   summary.at("symmetry").size());
    for (const auto& p : summary.at("patches")) {
        text += fmt::format("\n  {} degree {}x{}",
                            p.at("id").get<std::string>(),
                            p.at("degree")[0].get<int>(),
                            p.at("degree")[1].get<int>());
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

} // namespace ada::mcp
