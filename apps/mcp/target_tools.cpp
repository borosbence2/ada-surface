// The target tools: attach a mesh or point cloud as a fit target, describe
// its outline, lay a curve network on it, and set the fit and the budgets it
// is judged by. Curves made from a target are fitted through its points like
// any other curve, and the solver decides every control point.

#include "ada/analysis/budgets.hpp"
#include "ada/doc/recipes.hpp"
#include "ada/fit/layout.hpp"
#include "ada/geom/curve_fit.hpp"
#include "ada/tolerances.hpp"

#include "common.hpp"
#include "edit.hpp"
#include "tools.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ada::mcp {

namespace {

/// Degree of the curves along a target's outline, through every outline
/// vertex: cubics through a thinned outline missed the DrivAer hood's edge
/// by up to 0.9 mm, and boundary rows hold their curves exactly.
constexpr int kOutlineCurveDegree = 5;

/// Degree of the inner lines across a target: a cubic follows a hood's crown
/// through nine points.
constexpr int kSectionCurveDegree = 3;

/// Most rows or columns `network_from_target` lays out: past this, a panel
/// wants a different layout rather than a finer grid.
constexpr int kMaxNetworkSide = 12;

constexpr std::array<char, 3> kAxisNames{'x', 'y', 'z'};

nlohmann::json point_json(const Vec3& p) {
    return {p.x(), p.y(), p.z()};
}

std::vector<std::string> target_ids(const Scene& scene) {
    std::vector<std::string> ids;
    for (const SceneTarget& t : scene.targets) {
        ids.push_back(t.id);
    }
    return ids;
}

/// The target a call means: the one named by `name` if given, else the
/// fit's, else the only one.
const SceneTarget& pick_target(const Scene& scene, const nlohmann::json& args, const char* name) {
    if (scene.targets.empty()) {
        throw ToolError("the scene has no target",
                        "no_target",
                        "attach a mesh or point cloud with target_add {id, file}");
    }
    if (args.contains(name)) {
        const std::string id = string_argument(args, name);
        for (const SceneTarget& t : scene.targets) {
            if (t.id == id) {
                return t;
            }
        }
        throw ToolError(fmt::format("there is no target '{}'", id),
                        "unknown_target",
                        "targets in this scene: " + id_list(target_ids(scene)),
                        {{"id", id}});
    }
    if (scene.fit.has_value()) {
        return scene.target(scene.fit->target);
    }
    if (scene.targets.size() == 1) {
        return scene.targets.front();
    }
    throw ToolError(
        fmt::format("the scene has {} targets; say which with '{}'", scene.targets.size(), name),
        "invalid_arguments",
        "targets in this scene: " + id_list(target_ids(scene)));
}

const TargetMesh& loaded_mesh(const SceneTarget& t) {
    if (!t.mesh || !t.points) {
        throw ToolError(fmt::format("target '{}' is not loaded", t.id),
                        "target_unreadable",
                        fmt::format("check that {} is next to the scene file", t.file));
    }
    return *t.mesh;
}

/// The target as data: size, bounds, the axis it faces and its outline
/// loops, each with its corners and the sides between them.
nlohmann::json describe_target(const SceneTarget& t) {
    const TargetMesh& mesh = loaded_mesh(t);
    Vec3 lo = Vec3::Constant(std::numeric_limits<double>::infinity());
    Vec3 hi = -lo;
    double size = 0.0;
    for (const Vec3& v : mesh.vertices) {
        lo = lo.cwiseMin(v);
        hi = hi.cwiseMax(v);
        size = std::max(size, v.cwiseAbs().maxCoeff());
    }
    const double centre_plane = tol::kEdgeContact * size;
    nlohmann::json outlines = nlohmann::json::array();
    for (const TargetOutline& o : target_outlines(mesh)) {
        nlohmann::json corners = nlohmann::json::array();
        for (const OutlineCorner& c : o.corners) {
            corners.push_back(
                {{"point", point_json(o.points[c.index])}, {"turn_degrees", c.turn_degrees}});
        }
        nlohmann::json sides = nlohmann::json::array();
        const std::size_t n = o.points.size();
        for (std::size_t k = 0; k < o.corners.size(); ++k) {
            const std::size_t from = o.corners[k].index;
            const std::size_t to = o.corners[(k + 1) % o.corners.size()].index;
            double length = 0.0;
            double off_plane = std::abs(o.points[from].y());
            std::size_t count = 1;
            std::size_t i = from;
            do { // all the way round when the loop has one corner
                const std::size_t next = (i + 1) % n;
                length += (o.points[next] - o.points[i]).norm();
                off_plane = std::max(off_plane, std::abs(o.points[next].y()));
                ++count;
                i = next;
            } while (i != to);
            sides.push_back({{"from", point_json(o.points[from])},
                             {"to", point_json(o.points[to])},
                             {"points", count},
                             {"length_mm", length},
                             {"max_abs_y_mm", off_plane},
                             {"in_centre_plane", off_plane <= centre_plane}});
        }
        outlines.push_back({{"points", n},
                            {"length_mm", o.length},
                            {"corners", std::move(corners)},
                            {"sides", std::move(sides)}});
    }
    nlohmann::json facing = nullptr;
    if (!mesh.triangles.empty()) {
        facing = std::string(1, kAxisNames[static_cast<std::size_t>(facing_axis(mesh))]);
    }
    return {{"id", t.id},
            {"file", t.file},
            {"sample", t.sample == TargetSample::Vertices ? "vertices" : "surface"},
            {"points", t.points->size()},
            {"vertices", mesh.vertices.size()},
            {"triangles", mesh.triangles.size()},
            {"bounds", {{"min", point_json(lo)}, {"max", point_json(hi)}}},
            {"facing", facing},
            {"outlines", std::move(outlines)}};
}

std::string describe_text(const nlohmann::json& d) {
    const auto& lo = d.at("bounds").at("min");
    const auto& hi = d.at("bounds").at("max");
    std::string text = fmt::format(
        "target '{}': {}, {} fit points ({}), {} triangles{}\n"
        "  bounds x {:.1f}..{:.1f}, y {:.1f}..{:.1f}, z {:.1f}..{:.1f} mm",
        d.at("id").get<std::string>(),
        d.at("file").get<std::string>(),
        d.at("points").get<std::size_t>(),
        d.at("sample") == "vertices" ? "its vertices" : "spread over its triangles",
        d.at("triangles").get<std::size_t>(),
        d.at("facing").is_null() ? std::string() : ", faces " + d.at("facing").get<std::string>(),
        lo[0].get<double>(),
        hi[0].get<double>(),
        lo[1].get<double>(),
        hi[1].get<double>(),
        lo[2].get<double>(),
        hi[2].get<double>());
    std::size_t number = 0;
    for (const auto& o : d.at("outlines")) {
        text += fmt::format("\n  outline {}: {} points, {:.0f} mm round, {} corners",
                            ++number,
                            o.at("points").get<std::size_t>(),
                            o.at("length_mm").get<double>(),
                            o.at("corners").size());
        for (const auto& c : o.at("corners")) {
            const auto& p = c.at("point");
            text += fmt::format("\n    corner ({:.1f}, {:.1f}, {:.1f}), turns {:.0f} deg",
                                p[0].get<double>(),
                                p[1].get<double>(),
                                p[2].get<double>(),
                                c.at("turn_degrees").get<double>());
        }
        for (const auto& s : o.at("sides")) {
            const auto& a = s.at("from");
            const auto& b = s.at("to");
            text += fmt::format("\n    side ({:.1f}, {:.1f}, {:.1f}) -> ({:.1f}, {:.1f}, {:.1f}): "
                                "{:.0f} mm, {} points{}",
                                a[0].get<double>(),
                                a[1].get<double>(),
                                a[2].get<double>(),
                                b[0].get<double>(),
                                b[1].get<double>(),
                                b[2].get<double>(),
                                s.at("length_mm").get<double>(),
                                s.at("points").get<std::size_t>(),
                                s.at("in_centre_plane").get<bool>() ? ", in Y = 0" : "");
        }
    }
    return text;
}

/// Points for a curve of `degree`: a polyline with too few gets midpoints
/// of its longest segments, which stay on it.
std::vector<Vec3> enough_points(std::vector<Vec3> points, int degree) {
    while (points.size() < static_cast<std::size_t>(degree) + 1) {
        std::size_t longest = 0;
        for (std::size_t k = 1; k + 1 < points.size(); ++k) {
            if ((points[k + 1] - points[k]).norm() >
                (points[longest + 1] - points[longest]).norm()) {
                longest = k;
            }
        }
        points.insert(points.begin() + static_cast<std::ptrdiff_t>(longest) + 1,
                      0.5 * (points[longest] + points[longest + 1]));
    }
    return points;
}

bool in_centre_plane(const std::vector<std::vector<Vec3>>& lines, double tolerance) {
    for (const auto& line : lines) {
        for (const Vec3& p : line) {
            if (std::abs(p.y()) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

ToolResult target_add(SceneSession& session, History& history, const nlohmann::json& args) {
    SceneTarget target;
    target.id = checked_id(string_argument(args, "id"), "id");
    target.file = string_argument(args, "file");
    if (args.contains("sample")) {
        const std::string sample = string_argument(args, "sample");
        if (sample != "surface" && sample != "vertices") {
            throw ToolError("'sample' must be \"surface\" or \"vertices\"",
                            "invalid_arguments",
                            "\"vertices\" for a CAD tessellation, whose vertices lie on the "
                            "surface; \"surface\" (the default) for a scan, whose triangles are "
                            "the measurement");
        }
        target.sample = sample == "vertices" ? TargetSample::Vertices : TargetSample::Surface;
    }
    for (const auto& [name, value] :
         {std::pair{"spacing_mm", &target.spacing}, std::pair{"scale", &target.scale}}) {
        if (const std::optional<double> given = optional_number(args, name)) {
            if (!(*given > 0.0)) {
                throw ToolError(fmt::format("'{}' must be positive", name));
            }
            *value = *given;
        }
    }
    const bool replace = bool_argument(args, "replace", false);
    return apply_edit(session, history, [&](Scene& scene) {
        const auto existing = std::find_if(scene.targets.begin(),
                                           scene.targets.end(),
                                           [&](const SceneTarget& t) { return t.id == target.id; });
        if (existing != scene.targets.end() && !replace) {
            throw id_taken("target", target.id);
        }
        Scene loading;
        loading.targets = {target};
        try {
            load_targets(loading, session.path().parent_path());
        } catch (const std::invalid_argument& e) {
            throw ToolError(e.what(),
                            "target_unreadable",
                            "give an OBJ, STL or PLY file; a relative path is taken from the "
                            "scene file's folder");
        }
        const SceneTarget& loaded = loading.targets.front();
        nlohmann::json details = describe_target(loaded);
        std::string text = fmt::format("{} target '{}': {} points from {}",
                                       existing != scene.targets.end() ? "replaced" : "added",
                                       loaded.id,
                                       loaded.points->size(),
                                       loaded.file);
        if (!details.at("outlines").empty()) {
            text += "; target_info lists its outline and corners";
        }
        if (existing != scene.targets.end()) {
            *existing = loaded;
        } else {
            scene.targets.push_back(loaded);
        }
        return EditOutcome{std::move(text), std::move(details)};
    });
}

ToolResult target_info(SceneSession& session, const nlohmann::json& args) {
    const nlohmann::json described = describe_target(pick_target(session.current(), args, "id"));
    return {describe_text(described), described, false};
}

ToolResult
network_from_target(SceneSession& session, History& history, const nlohmann::json& args) {
    for (const char* name : {"rows", "cols", "corners"}) {
        if (!args.contains(name)) {
            throw ToolError(fmt::format("network_from_target needs '{}'", name));
        }
    }
    const int rows = integer_argument(args, "rows", 1, 1, kMaxNetworkSide);
    const int cols = integer_argument(args, "cols", 1, 1, kMaxNetworkSide);
    const std::vector<Vec3> given = points_argument(args.at("corners"), "corners");
    if (given.size() != 4) {
        throw ToolError(fmt::format("'corners' needs 4 points, got {}", given.size()),
                        "invalid_arguments",
                        "target_info lists the outline's corners");
    }
    const std::array<Vec3, 4> corners{given[0], given[1], given[2], given[3]};
    const int outline_degree =
        integer_argument(args, "outline_degree", kOutlineCurveDegree, 1, kMaxRecipeDegree);
    const int section_degree =
        integer_argument(args, "section_degree", kSectionCurveDegree, 1, kMaxRecipeDegree);
    const int section_points =
        integer_argument(args, "section_points", kDefaultSectionPoints, section_degree + 1, 99);
    const Continuity continuity = continuity_argument(args, "continuity", Continuity::G2);

    return apply_edit(session, history, [&](Scene& scene) {
        const std::string prefix = checked_id(string_argument(args, "prefix"), "prefix");
        const SceneTarget& target = pick_target(scene, args, "target");
        const std::string target_id = target.id;
        NetworkLayout layout;
        try {
            layout = layout_network(loaded_mesh(target), corners, rows, cols, section_points);
        } catch (const std::invalid_argument& e) {
            throw ToolError(e.what(),
                            "layout_failed",
                            "target_info lists the outline's corners: give four of them in order "
                            "round the outline (either way), and fewer rows or columns where a "
                            "side has few outline points");
        }

        const auto R = static_cast<std::size_t>(rows);
        const auto C = static_cast<std::size_t>(cols);
        std::set<std::string> taken;
        for (const SceneCurve& c : scene.curves) {
            taken.insert(c.id);
        }
        for (const ScenePatch& p : scene.patches) {
            taken.insert(p.id);
        }
        NetworkSpec spec{prefix, {}, {}, continuity};
        std::vector<std::string> curve_ids;
        auto add_curve = [&](const std::string& id, const std::vector<Vec3>& points, int degree) {
            if (taken.count(id) > 0) {
                throw ToolError(fmt::format("there is already a curve or patch '{}'", id),
                                "id_taken",
                                "pick another prefix",
                                {{"id", id}});
            }
            const std::vector<Vec3> through = enough_points(points, degree);
            scene.curves.push_back({id, curve_through_points(through, degree), through});
            curve_ids.push_back(id);
            return id;
        };
        for (std::size_t r = 0; r <= R; ++r) {
            std::vector<std::string> line;
            for (std::size_t c = 0; c < C; ++c) {
                line.push_back(add_curve(fmt::format("{}_u{}_{}", prefix, r, c),
                                         layout.along_u[r][c],
                                         r == 0 || r == R ? outline_degree : section_degree));
            }
            spec.along_u.push_back(std::move(line));
        }
        for (std::size_t c = 0; c <= C; ++c) {
            std::vector<std::string> line;
            for (std::size_t r = 0; r < R; ++r) {
                line.push_back(add_curve(fmt::format("{}_v{}_{}", prefix, c, r),
                                         layout.along_v[c][r],
                                         c == 0 || c == C ? outline_degree : section_degree));
            }
            spec.along_v.push_back(std::move(line));
        }
        for (std::size_t r = 0; r < R; ++r) {
            for (std::size_t c = 0; c < C; ++c) {
                const std::string id = fmt::format("{}_{}_{}", prefix, r, c);
                if (taken.count(id) > 0) {
                    throw ToolError(fmt::format("there is already a patch '{}'", id),
                                    "id_taken",
                                    "pick another prefix",
                                    {{"id", id}});
                }
            }
        }
        const std::vector<std::string> patches = add_network(scene, spec);

        // The network's outer edges that lie in Y = 0, for symmetry_constrain.
        double size = 0.0;
        for (const Vec3& v : loaded_mesh(scene.target(target_id)).vertices) {
            size = std::max(size, v.cwiseAbs().maxCoeff());
        }
        const double centre_plane = tol::kEdgeContact * size;
        std::vector<std::string> centre_edges;
        auto side =
            [&](const std::vector<std::vector<Vec3>>& lines, auto&& edge_of, std::size_t count) {
                if (in_centre_plane(lines, centre_plane)) {
                    for (std::size_t k = 0; k < count; ++k) {
                        centre_edges.push_back(edge_of(k));
                    }
                }
            };
        side(
            layout.along_v[0],
            [&](std::size_t r) { return fmt::format("{}_{}_0.UMin", prefix, r); },
            R);
        side(
            layout.along_v[C],
            [&](std::size_t r) { return fmt::format("{}_{}_{}.UMax", prefix, r, C - 1); },
            R);
        side(
            layout.along_u[0],
            [&](std::size_t c) { return fmt::format("{}_0_{}.VMin", prefix, c); },
            C);
        side(
            layout.along_u[R],
            [&](std::size_t c) { return fmt::format("{}_{}_{}.VMax", prefix, R - 1, c); },
            C);

        nlohmann::json corner_points = nlohmann::json::array();
        for (const Vec3& p : layout.corners) {
            corner_points.push_back(point_json(p));
        }
        const double offset =
            *std::max_element(layout.corner_offsets.begin(), layout.corner_offsets.end());
        std::string text = fmt::format(
            "laid a {} x {} network on target '{}': patches {}; {} curves, the outline's through "
            "every outline point at degree {}, the inner lines through {} points on the target at "
            "degree {}; internal edges joined {}",
            rows,
            cols,
            target_id,
            id_list(patches),
            curve_ids.size(),
            outline_degree,
            section_points,
            section_degree,
            to_string(continuity));
        if (offset > kReportedDeviationMm) {
            text += fmt::format("; corners taken at the nearest outline points, up to {:.2f} mm "
                                "from the points given",
                                offset);
        }
        if (!centre_edges.empty()) {
            text += "\nedges in the centre plane Y = 0 (symmetry_constrain them for a symmetric "
                    "part): " +
                    id_list(centre_edges);
        }
        return EditOutcome{std::move(text),
                           {{"patches", patches},
                            {"curves", curve_ids},
                            {"rows", rows},
                            {"cols", cols},
                            {"corners", std::move(corner_points)},
                            {"corner_offset_mm", offset},
                            {"centre_plane_edges", centre_edges}}};
    });
}

ToolResult fit_set(SceneSession& session, History& history, const nlohmann::json& args) {
    return apply_edit(session, history, [&](Scene& scene) {
        if (args.contains("target") && args.at("target") == "none") {
            scene.fit.reset();
            return EditOutcome{"removed the fit: the patches are again the fairest their curves "
                               "and constraints allow",
                               {{"fit", nullptr}}};
        }
        FitSettings fit = scene.fit.value_or(FitSettings{});
        if (args.contains("target")) {
            fit.target = pick_target(scene, args, "target").id;
        } else if (!scene.fit.has_value()) {
            throw ToolError("the scene has no fit yet: give 'target'",
                            "invalid_arguments",
                            "fit_set {target: <id>} starts one; target_add attaches a target");
        }
        if (const std::optional<double> weight = optional_number(args, "weight")) {
            if (!(*weight > 0.0)) {
                throw ToolError("'weight' must be positive");
            }
            fit.weight = *weight;
        }
        fit.economy = bool_argument(args, "economy", fit.economy);
        const Budgets& b = scene.budgets;
        if (fit.economy && !b.fit_max_mm && !b.fit_rms_mm && !b.fit_p95_mm) {
            throw ToolError("CV economy needs a fit budget to aim for",
                            "invalid_arguments",
                            "set one first with budgets_set, e.g. {\"fit_max_mm\": 0.5}");
        }
        scene.fit = fit;
        return EditOutcome{
            fmt::format("fit: the patches follow target '{}' at weight {}{}",
                        fit.target,
                        fit.weight,
                        fit.economy ? "; CV economy on: every solve starts the patches at their "
                                      "curves' degrees and raises them until the fit budgets pass"
                                    : ""),
            {{"target", fit.target}, {"weight", fit.weight}, {"economy", fit.economy}}};
    });
}

ToolResult budgets_set(SceneSession& session, History& history, const nlohmann::json& args) {
    if (!args.contains("budgets") || !args.at("budgets").is_object() ||
        args.at("budgets").empty()) {
        throw ToolError("'budgets' must be an object of budgets to change, e.g. "
                        "{\"fit_max_mm\": 0.5}");
    }
    return apply_edit(session, history, [&](Scene& scene) {
        const nlohmann::json current = to_json(scene.budgets);
        nlohmann::json merged = nlohmann::json::object();
        for (const auto& [key, value] : current.items()) {
            if (!value.is_null()) {
                merged[key] = value;
            }
        }
        std::string changed;
        for (const auto& [key, value] : args.at("budgets").items()) {
            if (value.is_null()) {
                merged.erase(key);
            } else {
                merged[key] = value;
            }
            changed += fmt::format("{}{} = {}", changed.empty() ? "" : ", ", key, value.dump());
        }
        try {
            scene.budgets = budgets_from_json(merged);
        } catch (const std::invalid_argument& e) {
            throw ToolError(fmt::format("budgets: {}", e.what()),
                            "invalid_arguments",
                            "budgets take the fields of a scene file's budgets section (the "
                            "quality report names them); null puts one back to its default");
        }
        return EditOutcome{"budgets: " + changed, {{"budgets", to_json(scene.budgets)}}};
    });
}

} // namespace ada::mcp
