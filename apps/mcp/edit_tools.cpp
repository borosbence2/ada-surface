// The editing tools. Each one reads the open scene, applies one edit to a
// copy, solves it, and only then writes it back: an edit whose scene does not
// solve is refused with the reason and what to try, and the file never holds
// a half-made scene. History snapshots make every edit undoable.

#include "ada/analysis/quality.hpp"
#include "ada/doc/recipes.hpp"
#include "ada/doc/topology.hpp"
#include "ada/geom/closest_point.hpp"
#include "ada/geom/curve_fit.hpp"
#include "ada/solve/solver.hpp"
#include "ada/tolerances.hpp"

#include "common.hpp"
#include "tools.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ada::mcp {

namespace {

constexpr std::array<const char*, 4> kEdgeNames{"UMin", "UMax", "VMin", "VMax"};
constexpr std::array<Edge, 4> kEdges{Edge::UMin, Edge::UMax, Edge::VMin, Edge::VMax};

constexpr const char* kLoopHint =
    "the four curves must form a closed loop: UMin opposite UMax, VMin opposite VMax, and each "
    "curve meeting its two neighbours at a corner (which way a curve runs does not matter). "
    "Check which curve sits on which edge, or refit the curves through shared corner points";

/// What an edit did, for the agent: a sentence, and the same as data.
struct EditOutcome {
    std::string text;
    nlohmann::json data = nlohmann::json::object();
};

using Edit = std::function<EditOutcome(Scene&)>;

// --- Lookups with errors an agent can act on ----------------------------------

std::vector<std::string> patch_ids(const Scene& scene) {
    std::vector<std::string> ids;
    for (const ScenePatch& p : scene.patches) {
        ids.push_back(p.id);
    }
    return ids;
}

std::vector<std::string> curve_ids(const Scene& scene) {
    std::vector<std::string> ids;
    for (const SceneCurve& c : scene.curves) {
        ids.push_back(c.id);
    }
    return ids;
}

const ScenePatch* find_patch(const Scene& scene, const std::string& id) {
    for (const ScenePatch& p : scene.patches) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

ScenePatch* find_patch(Scene& scene, const std::string& id) {
    return const_cast<ScenePatch*>(find_patch(std::as_const(scene), id));
}

ScenePatch& require_patch(Scene& scene, const std::string& id) {
    if (ScenePatch* p = find_patch(scene, id)) {
        return *p;
    }
    throw ToolError(fmt::format("there is no patch '{}'", id),
                    "unknown_patch",
                    scene.patches.empty() ? "the scene has no patches yet; make one with "
                                            "patch_from_boundaries, network_from_curves or "
                                            "patch_blend"
                                          : "patches in this scene: " + id_list(patch_ids(scene)),
                    {{"id", id}});
}

const SceneCurve& require_curve(const Scene& scene, const std::string& id) {
    for (const SceneCurve& c : scene.curves) {
        if (c.id == id) {
            return c;
        }
    }
    throw ToolError(fmt::format("there is no curve '{}'", id),
                    "unknown_curve",
                    scene.curves.empty() ? "the scene has no curves yet; add them with "
                                           "curve_from_points"
                                         : "curves in this scene: " + id_list(curve_ids(scene)),
                    {{"id", id}});
}

ToolError id_taken(const std::string& kind, const std::string& id) {
    return ToolError(fmt::format("there is already a {} '{}'", kind, id),
                     "id_taken",
                     "pass replace: true to change it, or pick another id",
                     {{"id", id}});
}

double contact_tolerance(const Scene& scene) {
    return tol::kEdgeContact * model_size(scene);
}

std::vector<std::string> edge_names(const std::vector<EdgeRef>& edges) {
    std::vector<std::string> names;
    for (const EdgeRef& e : edges) {
        names.push_back(edge_name(e));
    }
    return names;
}

bool same_edge(const EdgeRef& x, const EdgeRef& y) {
    return x.patch == y.patch && x.edge == y.edge;
}

/// The direction a patch edge runs in: UMin and UMax are lines of constant
/// u, so they run along v.
bool runs_along_v(Edge edge) {
    return edge == Edge::UMin || edge == Edge::UMax;
}

const char* direction(bool along_v) {
    return along_v ? "v" : "u";
}

ToolError degree_mismatch(const Scene& scene, const EdgeRef& a, const EdgeRef& b) {
    const int da = scene.patch(a.patch).patch.degree_along(a.edge);
    const int db = scene.patch(b.patch).patch.degree_along(b.edge);
    const EdgeRef& low = da < db ? a : b;
    return ToolError(
        fmt::format("{} is degree {} along the edge and {} is degree {}; joined edges need the "
                    "same degree",
                    edge_name(a),
                    da,
                    edge_name(b),
                    db),
        "degree_mismatch",
        fmt::format("raise {} to degree {} in {} with patch_set_degree",
                    low.patch,
                    std::max(da, db),
                    direction(runs_along_v(low.edge))),
        {{"a", edge_name(a)}, {"b", edge_name(b)}, {"degree_a", da}, {"degree_b", db}});
}

// --- Solve failures -----------------------------------------------------------

/// What to try for each kind of constraint a contradiction names. Labels come
/// from `solve_scene`: "G2 a.UMax ~ b.UMin", "Y=0 a.UMin", "a boundary UMin",
/// "fixed a", "pin a (0.5, 0.5)".
std::string contradiction_hint(const std::vector<ConstraintResidual>& conflicts) {
    auto any = [&](auto&& predicate) {
        return std::any_of(conflicts.begin(), conflicts.end(), predicate);
    };
    std::vector<std::string> hints;
    if (any([](const ConstraintResidual& c) {
            return c.label.starts_with("G1 ") || c.label.starts_with("G2 ");
        })) {
        hints.emplace_back("a G1 or G2 joint needs free rows of control points on both sides: "
                           "raise the degree across it (patch_set_degree) or ask for less "
                           "continuity (edge_constrain)");
    }
    if (any([](const ConstraintResidual& c) {
            return c.label.find(" boundary ") != std::string::npos;
        })) {
        hints.emplace_back("boundary curves hold their patch edges exactly; refit a curve "
                           "(curve_from_points with replace: true) so it already meets what "
                           "the joint asks of it");
    }
    if (any([](const ConstraintResidual& c) { return c.label.starts_with("Y=0 "); })) {
        hints.emplace_back("symmetry needs the edge exactly in Y = 0 and its curve crossing the "
                           "plane at a right angle");
    }
    if (any([](const ConstraintResidual& c) { return c.label.starts_with("fixed "); })) {
        hints.emplace_back("a patch without a recipe never moves, so its neighbours must fit it");
    }
    if (hints.empty()) {
        hints.emplace_back("undo the last change to the constraints named, or relax one of them");
    }
    std::string text;
    for (const std::string& h : hints) {
        text += (text.empty() ? "" : "; ") + h;
    }
    return text;
}

ToolError solve_failure(const SolveError& e) {
    nlohmann::json conflicts = nlohmann::json::array();
    std::string names;
    for (const ConstraintResidual& c : e.conflicts()) {
        conflicts.push_back({{"constraint", c.label}, {"mm", c.max_residual}});
        names +=
            fmt::format("{}{} ({:.3g} mm)", names.empty() ? "" : ", ", c.label, c.max_residual);
    }
    if (e.conflicts().empty()) {
        return ToolError(fmt::format("the scene does not solve: {}", e.what()),
                         "solve_failed",
                         "undo the edit or change it");
    }
    return ToolError(fmt::format("these constraints cannot all hold: {}", names),
                     "constraints_contradict",
                     contradiction_hint(e.conflicts()),
                     {{"conflicts", std::move(conflicts)}});
}

// --- Results -------------------------------------------------------------------

/// The quality report in brief: pass or fail and every failed check. The
/// full report is quality_report's.
void add_quality(std::string& text, nlohmann::json& data, const Scene& scene) {
    if (scene.patches.empty()) {
        text += "\nquality: no patches yet";
        data["quality"] = {{"pass", true}, {"failed", nlohmann::json::array()}};
        return;
    }
    const QualityReport report = compute_quality(scene);
    const std::vector<FailedCheck> failed = failed_checks(report);
    nlohmann::json list = nlohmann::json::array();
    text += report.pass ? "\nquality: PASS"
                        : fmt::format("\nquality: FAIL, {} failed checks", failed.size());
    for (const FailedCheck& f : failed) {
        text += fmt::format("\n  {}: {} = {:.4g} exceeds {:.4g}",
                            f.where,
                            f.check.name,
                            f.check.value,
                            f.check.budget);
        list.push_back({{"where", f.where},
                        {"check", f.check.name},
                        {"value", f.check.value},
                        {"budget", f.check.budget}});
    }
    data["quality"] = {{"pass", report.pass}, {"failed", std::move(list)}};
}

ToolResult
edit_result(const EditOutcome& outcome, const SceneSolveSummary& solve, const Scene& scene) {
    std::string text = outcome.text;
    nlohmann::json data = {{"changed", outcome.text}, {"details", outcome.data}};

    nlohmann::json moved = nlohmann::json::array();
    std::string moved_text;
    for (const CurveDeviation& c : solve.curves) {
        if (c.max_mm > kReportedDeviationMm) {
            moved.push_back({{"curve", c.curve}, {"max_mm", c.max_mm}});
            moved_text +=
                fmt::format("{}{} {:.3f} mm", moved_text.empty() ? "" : ", ", c.curve, c.max_mm);
        }
    }
    text += fmt::format("\nsolved in {:.1f} ms ({} unknowns)", solve.milliseconds, solve.unknowns);
    if (!moved_text.empty()) {
        text += "; curves that gave way for continuity: " + moved_text;
    }
    data["solve"] = {{"stages", solve.stages},
                     {"unknowns", solve.unknowns},
                     {"milliseconds", solve.milliseconds},
                     {"max_residual_mm", solve.max_residual},
                     {"curves_moved", std::move(moved)}};
    add_quality(text, data, scene);
    return {std::move(text), std::move(data), false};
}

/// Applies `edit` to a copy of the open scene, solves it and writes it back,
/// or changes nothing and says why.
ToolResult apply_edit(SceneSession& session, History& history, const Edit& edit) {
    const Scene before = session.current();
    Scene after = before;
    try {
        EditOutcome outcome;
        try {
            outcome = edit(after);
        } catch (const std::invalid_argument& e) {
            throw ToolError(e.what(), "invalid_edit", "check the ids, edges and values named");
        }
        SceneSolveSummary summary;
        try {
            summary = solve_scene(after);
        } catch (const SolveError& e) {
            throw solve_failure(e);
        } catch (const std::invalid_argument& e) {
            throw ToolError(e.what(), "unsolvable", "undo the edit or change it");
        }
        history.record(before, outcome.text);
        write_scene(after, session.path());
        return edit_result(outcome, summary, after);
    } catch (const ToolError& e) {
        throw ToolError(std::string(e.what()) + " (the scene was not changed)",
                        e.code,
                        e.what_to_try,
                        e.details);
    }
}

// --- Degrees -------------------------------------------------------------------

/// One direction of one patch: its u degree (`along_v` false) or v degree.
struct Axis {
    std::string patch;
    bool along_v;

    bool operator==(const Axis&) const = default;
};

/// Every patch direction whose degree must equal `start`'s because linked
/// edges join them (matched edges have equal degrees along them).
std::vector<Axis> tied_axes(const Scene& scene, const Axis& start) {
    const std::vector<EdgeLink> links = all_edge_links(scene);
    std::vector<Axis> found{start};
    std::deque<Axis> queue{start};
    while (!queue.empty()) {
        const Axis current = queue.front();
        queue.pop_front();
        for (const EdgeLink& link : links) {
            for (const auto& [end, other] :
                 {std::pair{link.a, link.b}, std::pair{link.b, link.a}}) {
                if (end.patch != current.patch || runs_along_v(end.edge) != current.along_v) {
                    continue;
                }
                const Axis next{other.patch, runs_along_v(other.edge)};
                if (std::find(found.begin(), found.end(), next) == found.end()) {
                    found.push_back(next);
                    queue.push_back(next);
                }
            }
        }
    }
    return found;
}

/// The degree a from_boundaries patch has without an override: its curves'.
int curve_degree(const Scene& scene, const ScenePatch& p, bool along_v) {
    FromBoundaries plain = std::get<FromBoundaries>(*p.recipe);
    plain.degree_u.reset();
    plain.degree_v.reset();
    const Boundaries b = resolve_boundaries(scene, plain, p.id);
    return along_v ? b.degree_v() : b.degree_u();
}

/// Sets one direction's degree on `axis` and on every direction tied to it.
/// Returns the patches changed.
std::vector<std::string> set_degree(Scene& scene, const Axis& axis, int degree) {
    std::vector<std::string> changed;
    for (const Axis& a : tied_axes(scene, axis)) {
        ScenePatch& p = require_patch(scene, a.patch);
        const char* name = a.along_v ? "degree_v" : "degree_u";
        const int current = a.along_v ? p.patch.degree_v() : p.patch.degree_u();
        if (!p.recipe.has_value()) {
            if (current != degree) {
                throw ToolError(
                    fmt::format("patch '{}' has no recipe, so its {} stays {}; it is linked to "
                                "'{}' along that direction",
                                p.id,
                                name,
                                current,
                                axis.patch),
                    "fixed_patch",
                    "a patch without a recipe is fixed geometry; remove the link that ties it "
                    "(edge_constrain with continuity none) or leave the degree as it is");
            }
            continue;
        }
        if (auto* fb = std::get_if<FromBoundaries>(&*p.recipe)) {
            const int floor = curve_degree(scene, p, a.along_v);
            if (degree < floor) {
                throw ToolError(
                    fmt::format("patch '{}' cannot go below {} {}: its curves are degree {}",
                                p.id,
                                name,
                                floor,
                                floor),
                    "degree_below_curves",
                    "refit the boundary curves at a lower degree (curve_from_points with "
                    "replace: true and a lower degree)");
            }
            (a.along_v ? fb->degree_v : fb->degree_u) =
                degree == floor ? std::nullopt : std::optional<int>(degree);
        } else {
            auto& blend = std::get<Blend>(*p.recipe);
            if (a.along_v) {
                const int k = blend.continuity == Continuity::G0   ? 0
                              : blend.continuity == Continuity::G1 ? 1
                                                                   : 2;
                if (degree < 2 * k + 1) {
                    throw ToolError(
                        fmt::format("blend '{}' needs degree {} across for {} on both sides",
                                    p.id,
                                    2 * k + 1,
                                    to_string(blend.continuity)),
                        "invalid_arguments");
                }
                blend.degree_across = degree;
            }
            // Along its edges a blend takes its sources' degree, which are
            // tied to it and set in this same loop.
        }
        if (current != degree) {
            changed.push_back(p.id);
        }
    }
    return changed;
}

} // namespace

// --- The tools -----------------------------------------------------------------

ToolResult scene_new(SceneSession& session, const nlohmann::json& args) {
    std::filesystem::path path = string_argument(args, "path");
    if (path.is_relative()) {
        path = session.is_open() ? session.path().parent_path() / path
                                 : std::filesystem::absolute(path);
    }
    const std::string name = path.filename().string();
    if (!name.ends_with(".ada.json")) {
        throw ToolError(fmt::format("'{}' does not end in .ada.json", name),
                        "invalid_arguments",
                        "scene files are named <name>.ada.json");
    }
    std::error_code made;
    std::filesystem::create_directories(path.parent_path(), made);
    if (made) {
        throw ToolError(fmt::format("cannot create the directory {}: {}",
                                    path.parent_path().generic_string(),
                                    made.message()),
                        "invalid_arguments",
                        "give a path in a directory that can be written");
    }
    if (std::filesystem::exists(path) && !bool_argument(args, "overwrite", false)) {
        throw ToolError(fmt::format("{} already exists", path.generic_string()),
                        "file_exists",
                        "open it with scene_open, pick another path, or pass overwrite: true");
    }
    Scene scene;
    if (args.contains("description")) {
        scene.description = string_argument(args, "description");
    }
    if (args.contains("budgets")) {
        try {
            scene.budgets = budgets_from_json(args.at("budgets"));
        } catch (const std::exception& e) {
            throw ToolError(fmt::format("budgets: {}", e.what()),
                            "invalid_arguments",
                            "budgets take the fields of a scene file's budgets section, e.g. "
                            "{\"max_degree\": 5, \"g2_relative\": 0.01}");
        }
    }
    write_scene(scene, path);
    session.open(path);
    return {fmt::format("created and opened {}: an empty scene", path.generic_string()),
            {{"path", path.generic_string()}, {"budgets", to_json(scene.budgets)}},
            false};
}

ToolResult curve_from_points(SceneSession& session, History& history, const nlohmann::json& args) {
    if (!args.contains("curves") || !args.at("curves").is_array() || args.at("curves").empty()) {
        throw ToolError("'curves' must be a non-empty list of {id, points, degree}");
    }
    const bool replace = bool_argument(args, "replace", false);
    return apply_edit(session, history, [&](Scene& scene) {
        std::vector<std::string> added;
        std::vector<std::string> replaced;
        std::set<std::string> seen;
        std::string fits;
        nlohmann::json details = nlohmann::json::array();
        const nlohmann::json& list = args.at("curves");
        for (std::size_t k = 0; k < list.size(); ++k) {
            const nlohmann::json& item = list[k];
            const std::string where = fmt::format("curves[{}]", k);
            if (!item.is_object()) {
                throw ToolError(where + " must be an object {id, points, degree}");
            }
            const std::string id = checked_id(string_argument(item, "id"), where + ".id");
            if (!seen.insert(id).second) {
                throw ToolError(fmt::format("curve '{}' appears twice in this call", id));
            }
            if (!item.contains("points")) {
                throw ToolError(fmt::format("{} ('{}') needs 'points'", where, id));
            }
            const std::vector<Vec3> points = points_argument(item.at("points"), where + ".points");
            const int degree = integer_argument(item, "degree", 3, 1, kMaxRecipeDegree);
            const BezierCurve curve = [&] {
                try {
                    return curve_through_points(points, degree);
                } catch (const std::invalid_argument& e) {
                    throw ToolError(fmt::format("curve '{}': {}", id, e.what()),
                                    "invalid_arguments",
                                    "give at least two points and no point twice in a row");
                }
            }();
            double fit = 0.0;
            for (const Vec3& p : points) {
                fit = std::max(fit, closest_point(curve, p).distance);
            }
            details.push_back({{"id", id},
                               {"degree", curve.degree()},
                               {"points", points.size()},
                               {"fit_mm", fit}});
            if (fit > kReportedDeviationMm) {
                fits +=
                    fmt::format("; '{}' passes {:.3f} mm from its farthest point (a least-squares "
                                "fit: more points than degree + 1)",
                                id,
                                fit);
            }

            const auto existing = std::find_if(scene.curves.begin(),
                                               scene.curves.end(),
                                               [&](const SceneCurve& c) { return c.id == id; });
            if (existing != scene.curves.end()) {
                if (!replace) {
                    throw id_taken("curve", id);
                }
                *existing = {id, curve, points};
                replaced.push_back(id);
            } else {
                scene.curves.push_back({id, curve, points});
                added.push_back(id);
            }
        }
        std::string text;
        if (!added.empty()) {
            text += "added curves " + id_list(added);
        }
        if (!replaced.empty()) {
            text += (text.empty() ? "replaced curves " : "; replaced ") + id_list(replaced);
        }
        return EditOutcome{text + fits, {{"curves", std::move(details)}}};
    });
}

ToolResult
patch_from_boundaries(SceneSession& session, History& history, const nlohmann::json& args) {
    return apply_edit(session, history, [&](Scene& scene) {
        const std::string id = checked_id(string_argument(args, "id"), "patch id");
        FromBoundaries recipe{{}, std::nullopt, std::nullopt};
        for (std::size_t k = 0; k < 4; ++k) {
            recipe.curves[k] = string_argument(args, kEdgeNames[k]);
            static_cast<void>(require_curve(scene, recipe.curves[k]));
        }
        ScenePatch patch{id, BezierPatch(1, 1, std::vector<Vec3>(4, Vec3::Zero())), recipe, false};
        try {
            patch.patch = initial_patch(scene, patch);
        } catch (const std::invalid_argument& e) {
            throw ToolError(e.what(), "curves_do_not_meet", kLoopHint, {{"patch", id}});
        }
        const std::string text = fmt::format("patch '{}' from curves UMin {}, UMax {}, VMin {}, "
                                             "VMax {} (degree {}x{})",
                                             id,
                                             recipe.curves[0],
                                             recipe.curves[1],
                                             recipe.curves[2],
                                             recipe.curves[3],
                                             patch.patch.degree_u(),
                                             patch.patch.degree_v());
        if (ScenePatch* existing = find_patch(scene, id)) {
            if (!bool_argument(args, "replace", false)) {
                throw id_taken("patch", id);
            }
            *existing = std::move(patch);
            return EditOutcome{"replaced " + text};
        }
        scene.patches.push_back(std::move(patch));
        return EditOutcome{"added " + text};
    });
}

ToolResult patch_blend(SceneSession& session, History& history, const nlohmann::json& args) {
    return apply_edit(session, history, [&](Scene& scene) {
        const std::string id = checked_id(string_argument(args, "id"), "patch id");
        const EdgeRef from = edge_argument(args, "from");
        const EdgeRef to = edge_argument(args, "to");
        const Continuity continuity = continuity_argument(args, "continuity", Continuity::G2);
        const std::optional<int> across =
            optional_integer(args, "degree_across", 1, kMaxRecipeDegree);
        if (from.patch == id || to.patch == id) {
            throw ToolError("a blend cannot start or end on itself");
        }
        static_cast<void>(require_patch(scene, from.patch));
        static_cast<void>(require_patch(scene, to.patch));
        if (scene.patch(from.patch).patch.degree_along(from.edge) !=
            scene.patch(to.patch).patch.degree_along(to.edge)) {
            throw degree_mismatch(scene, from, to);
        }
        if (edge_gap(scene, from, to) < contact_tolerance(scene)) {
            throw ToolError(
                fmt::format("{} and {} already touch; there is no gap to blend",
                            edge_name(from),
                            edge_name(to)),
                "edges_touch",
                "a blend fills a gap between two edges; join touching edges with edge_constrain");
        }
        const int k = continuity == Continuity::G0 ? 0 : continuity == Continuity::G1 ? 1 : 2;
        if (across.has_value() && *across < 2 * k + 1) {
            throw ToolError(fmt::format("degree_across {} is too low for {} on both sides; it "
                                        "needs at least {}",
                                        *across,
                                        to_string(continuity),
                                        2 * k + 1));
        }
        ScenePatch patch{id,
                         BezierPatch(1, 1, std::vector<Vec3>(4, Vec3::Zero())),
                         Blend{from, to, continuity, across},
                         false};
        patch.patch = initial_patch(scene, patch);
        const std::string text =
            fmt::format("blend '{}' from {} to {}, {} on both sides (degree {} "
                        "across)",
                        id,
                        edge_name(from),
                        edge_name(to),
                        to_string(continuity),
                        patch.patch.degree_v());
        if (ScenePatch* existing = find_patch(scene, id)) {
            if (!bool_argument(args, "replace", false)) {
                throw id_taken("patch", id);
            }
            *existing = std::move(patch);
            return EditOutcome{"replaced " + text};
        }
        scene.patches.push_back(std::move(patch));
        return EditOutcome{"added " + text};
    });
}

ToolResult
network_from_curves(SceneSession& session, History& history, const nlohmann::json& args) {
    return apply_edit(session, history, [&](Scene& scene) {
        NetworkSpec spec{checked_id(string_argument(args, "prefix"), "prefix"),
                         {},
                         {},
                         continuity_argument(args, "continuity", Continuity::G2)};
        auto lines = [&](const char* name) {
            std::vector<std::vector<std::string>> out;
            const nlohmann::json& value = args.contains(name) ? args.at(name) : nlohmann::json();
            if (!value.is_array() || value.size() < 2) {
                throw ToolError(
                    fmt::format("'{}' must list at least two lines of curve ids", name));
            }
            for (const nlohmann::json& line : value) {
                if (!line.is_array() || line.empty()) {
                    throw ToolError(
                        fmt::format("every line in '{}' is a non-empty list of curve ids", name));
                }
                std::vector<std::string> ids;
                for (const nlohmann::json& id : line) {
                    if (!id.is_string()) {
                        throw ToolError(fmt::format("'{}' holds curve ids (strings)", name));
                    }
                    static_cast<void>(require_curve(scene, id.get<std::string>()));
                    ids.push_back(id.get<std::string>());
                }
                out.push_back(std::move(ids));
            }
            return out;
        };
        spec.along_u = lines("along_u");
        spec.along_v = lines("along_v");
        const std::size_t rows = spec.along_u.size() - 1;
        const std::size_t cols = spec.along_v.size() - 1;
        const std::string shape_hint =
            "patch (r, c) is bounded by along_u[r][c] and along_u[r + 1][c] (its VMin and VMax) "
            "and by along_v[c][r] and along_v[c + 1][r] (its UMin and UMax): along_u holds rows + "
            "1 lines of cols curves each, along_v holds cols + 1 lines of rows curves each";
        for (const auto& line : spec.along_u) {
            if (line.size() != cols) {
                throw ToolError(
                    fmt::format("every along_u line needs {} curves, one per column", cols),
                    "invalid_arguments",
                    shape_hint);
            }
        }
        for (const auto& line : spec.along_v) {
            if (line.size() != rows) {
                throw ToolError(
                    fmt::format("every along_v line needs {} curves, one per row", rows),
                    "invalid_arguments",
                    shape_hint);
            }
        }
        for (std::size_t r = 0; r < rows; ++r) {
            for (std::size_t c = 0; c < cols; ++c) {
                const std::string id = fmt::format("{}_{}_{}", spec.prefix, r, c);
                if (find_patch(scene, id) != nullptr) {
                    throw ToolError(fmt::format("there is already a patch '{}'", id),
                                    "id_taken",
                                    "pick another prefix",
                                    {{"id", id}});
                }
            }
        }
        std::vector<std::string> ids;
        try {
            ids = add_network(scene, spec);
        } catch (const std::invalid_argument& e) {
            throw ToolError(
                e.what(), "curves_do_not_meet", std::string(kLoopHint) + "; " + shape_hint);
        }
        const std::size_t joints = rows * (cols - 1) + cols * (rows - 1);
        return EditOutcome{fmt::format("added a {} x {} network: patches {}; {} internal edges "
                                       "joined {}",
                                       rows,
                                       cols,
                                       id_list(ids),
                                       joints,
                                       to_string(spec.continuity)),
                           {{"patches", ids}, {"rows", rows}, {"cols", cols}}};
    });
}

ToolResult edge_constrain(SceneSession& session, History& history, const nlohmann::json& args) {
    return apply_edit(session, history, [&](Scene& scene) {
        const EdgeRef a = edge_argument(args, "a");
        const EdgeRef b = edge_argument(args, "b");
        const std::optional<Continuity> continuity = continuity_or_none(args, "continuity");
        static_cast<void>(require_patch(scene, a.patch));
        static_cast<void>(require_patch(scene, b.patch));
        if (a.patch == b.patch) {
            throw ToolError("an edge link joins edges of two different patches");
        }
        for (const ScenePatch& p : scene.patches) {
            const auto* blend = p.recipe.has_value() ? std::get_if<Blend>(&*p.recipe) : nullptr;
            if (blend == nullptr) {
                continue;
            }
            const EdgeRef start{p.id, Edge::VMin};
            const EdgeRef end{p.id, Edge::VMax};
            const bool implied = (same_edge(a, blend->from) && same_edge(b, start)) ||
                                 (same_edge(b, blend->from) && same_edge(a, start)) ||
                                 (same_edge(a, end) && same_edge(b, blend->to)) ||
                                 (same_edge(b, end) && same_edge(a, blend->to));
            if (implied) {
                throw ToolError(
                    fmt::format("{} ~ {} belongs to blend '{}', which joins both its sides {}",
                                edge_name(a),
                                edge_name(b),
                                p.id,
                                to_string(blend->continuity)),
                    "implied_by_blend",
                    fmt::format(
                        "change it with patch_blend (id '{}', replace: true, continuity: ...)",
                        p.id));
            }
        }

        const auto link =
            std::find_if(scene.edges.begin(), scene.edges.end(), [&](const EdgeLink& l) {
                return (same_edge(l.a, a) && same_edge(l.b, b)) ||
                       (same_edge(l.a, b) && same_edge(l.b, a));
            });
        if (!continuity.has_value()) {
            if (link == scene.edges.end()) {
                throw ToolError(fmt::format("{} and {} are not linked", edge_name(a), edge_name(b)),
                                "not_linked",
                                "scene_summary lists the links");
            }
            scene.edges.erase(link);
            return EditOutcome{fmt::format("removed the link {} ~ {}", edge_name(a), edge_name(b))};
        }

        const double gap = edge_gap(scene, a, b);
        if (gap > contact_tolerance(scene)) {
            const std::vector<std::string> touching = edge_names(touching_edges(scene, a));
            throw ToolError(
                fmt::format("{} and {} do not meet: they are up to {:.1f} mm apart",
                            edge_name(a),
                            edge_name(b),
                            gap),
                "edges_do_not_meet",
                touching.empty()
                    ? "an edge link joins edges that already lie along each other, such as two "
                      "patches built on a shared curve; nothing lies along " +
                          edge_name(a)
                    : fmt::format(
                          "{} lies along {}; link that instead", edge_name(a), id_list(touching)),
                {{"gap_mm", gap}, {"touching", touching}});
        }
        if (scene.patch(a.patch).patch.degree_along(a.edge) !=
            scene.patch(b.patch).patch.degree_along(b.edge)) {
            throw degree_mismatch(scene, a, b);
        }
        if (link != scene.edges.end()) {
            const Continuity before = link->continuity;
            link->continuity = *continuity;
            return EditOutcome{fmt::format("changed {} ~ {} from {} to {}",
                                           edge_name(a),
                                           edge_name(b),
                                           to_string(before),
                                           to_string(*continuity))};
        }
        scene.edges.push_back({a, b, *continuity});
        return EditOutcome{fmt::format(
            "linked {} ~ {} at {}", edge_name(a), edge_name(b), to_string(*continuity))};
    });
}

ToolResult symmetry_constrain(SceneSession& session, History& history, const nlohmann::json& args) {
    return apply_edit(session, history, [&](Scene& scene) {
        const EdgeRef edge = edge_argument(args, "edge");
        const std::optional<Continuity> continuity = continuity_or_none(args, "continuity");
        static_cast<void>(require_patch(scene, edge.patch));
        const auto link =
            std::find_if(scene.symmetry.begin(), scene.symmetry.end(), [&](const SymmetryLink& l) {
                return same_edge(l.edge, edge);
            });
        if (!continuity.has_value()) {
            if (link == scene.symmetry.end()) {
                throw ToolError(fmt::format("{} has no symmetry link", edge_name(edge)),
                                "not_linked",
                                "scene_summary lists the symmetry links");
            }
            scene.symmetry.erase(link);
            return EditOutcome{fmt::format("removed the symmetry link on {}", edge_name(edge))};
        }
        if (!on_centre_plane(scene, edge)) {
            std::vector<std::string> in_plane;
            for (const Edge e : kEdges) {
                if (on_centre_plane(scene, {edge.patch, e})) {
                    in_plane.push_back(edge_name({edge.patch, e}));
                }
            }
            const double distance = centre_plane_distance(scene, edge);
            throw ToolError(
                fmt::format("{} is not in the centre plane Y = 0: it is up to {:.1f} mm from it",
                            edge_name(edge),
                            distance),
                "not_on_centre_plane",
                in_plane.empty()
                    ? "symmetry joins an edge lying in Y = 0 to its mirror image; refit the curve "
                      "on that edge with y = 0 at every point (curve_from_points, replace: true)"
                    : fmt::format("{} lies in Y = 0; link that instead", id_list(in_plane)),
                {{"distance_mm", distance}});
        }
        if (link != scene.symmetry.end()) {
            const Continuity before = link->continuity;
            link->continuity = *continuity;
            return EditOutcome{fmt::format("changed the symmetry of {} from {} to {}",
                                           edge_name(edge),
                                           to_string(before),
                                           to_string(*continuity))};
        }
        scene.symmetry.push_back({edge, *continuity});
        return EditOutcome{fmt::format("{} now joins its mirror image in Y = 0 at {}",
                                       edge_name(edge),
                                       to_string(*continuity))};
    });
}

ToolResult patch_set_degree(SceneSession& session, History& history, const nlohmann::json& args) {
    return apply_edit(session, history, [&](Scene& scene) {
        const std::string id = string_argument(args, "patch");
        const std::optional<int> du = optional_integer(args, "degree_u", 1, kMaxRecipeDegree);
        const std::optional<int> dv = optional_integer(args, "degree_v", 1, kMaxRecipeDegree);
        if (!du.has_value() && !dv.has_value()) {
            throw ToolError("give 'degree_u', 'degree_v' or both");
        }
        const ScenePatch& patch = require_patch(scene, id);
        if (!patch.recipe.has_value()) {
            throw ToolError(fmt::format("patch '{}' has no recipe; its degrees are fixed", id),
                            "fixed_patch",
                            "only patches made by a recipe (from_boundaries, blend) change degree");
        }
        std::string text;
        nlohmann::json details = nlohmann::json::object();
        for (const auto& [degree, along_v] : {std::pair{du, false}, std::pair{dv, true}}) {
            if (!degree.has_value()) {
                continue;
            }
            const std::vector<std::string> changed = set_degree(scene, {id, along_v}, *degree);
            const char* name = along_v ? "degree_v" : "degree_u";
            details[name] = {{"degree", *degree}, {"patches", changed}};
            text += fmt::format("{}{} {} on {}",
                                text.empty() ? "set " : "; ",
                                name,
                                *degree,
                                changed.empty() ? std::string("no patch (already that degree)")
                                                : id_list(changed));
        }
        return EditOutcome{text + " (linked edges carry a degree across to their neighbours)",
                           std::move(details)};
    });
}

ToolResult fairness_set(SceneSession& session, History& history, const nlohmann::json& args) {
    return apply_edit(session, history, [&](Scene& scene) {
        SolveSettings settings = scene.solve;
        if (args.contains("order")) {
            const std::string order = string_argument(args, "order");
            if (order != "E2" && order != "E3") {
                throw ToolError("'order' must be E2 (bending) or E3 (change of curvature)");
            }
            settings.order = order == "E2" ? FairnessOrder::E2 : FairnessOrder::E3;
        }
        for (const auto& [name, target] :
             {std::pair{"fairness", &settings.fairness}, std::pair{"move", &settings.move}}) {
            if (const std::optional<double> value = optional_number(args, name)) {
                if (*value < 0.0) {
                    throw ToolError(fmt::format("'{}' must not be negative", name));
                }
                *target = *value;
            }
        }
        if (!args.contains("order") && !args.contains("fairness") && !args.contains("move")) {
            throw ToolError("give 'order', 'fairness', 'move' or several of them");
        }
        if (settings.fairness + settings.move <= 0.0) {
            throw ToolError("with fairness and move both 0, nothing decides the free control "
                            "points",
                            "invalid_arguments",
                            "fairness 1, move 0 for the fairest surface; fairness 0, move 1 for "
                            "the least change from the current one");
        }
        scene.solve = settings;
        return EditOutcome{fmt::format("solve settings: fairness {} ({}), move {}",
                                       settings.fairness,
                                       settings.order == FairnessOrder::E2 ? "E2" : "E3",
                                       settings.move),
                           {{"fairness", settings.fairness},
                            {"order", settings.order == FairnessOrder::E2 ? "E2" : "E3"},
                            {"move", settings.move}}};
    });
}

ToolResult undo(SceneSession& session, History& history) {
    if (!history.can_undo()) {
        throw ToolError("there is nothing to undo",
                        "nothing_to_undo",
                        "undo steps back through the edits these tools made since the scene was "
                        "opened");
    }
    std::string text = "undid: " + history.undo_label();
    write_scene(history.undo(session.current()), session.path());
    nlohmann::json data = {{"changed", text}};
    // Re-read: the snapshot has no target points, the file's targets do.
    add_quality(text, data, session.current());
    return {std::move(text), std::move(data), false};
}

ToolResult redo(SceneSession& session, History& history) {
    if (!history.can_redo()) {
        throw ToolError("there is nothing to redo",
                        "nothing_to_redo",
                        "redo repeats an edit that undo took back, until a new edit is made");
    }
    std::string text = "redid: " + history.redo_label();
    write_scene(history.redo(session.current()), session.path());
    nlohmann::json data = {{"changed", text}};
    // Re-read: the snapshot has no target points, the file's targets do.
    add_quality(text, data, session.current());
    return {std::move(text), std::move(data), false};
}

ToolResult get_selection(SceneSession& session) {
    const Scene& scene = session.current();
    const std::filesystem::path file = session.path().parent_path() / "selection.json";
    const char* pick_hint = "ask the user to click the patch or edge in ADA Studio, then call "
                            "get_selection again";
    std::ifstream stream(file);
    if (!stream) {
        throw ToolError("nothing is selected", "no_selection", pick_hint);
    }
    nlohmann::json selection;
    try {
        selection = nlohmann::json::parse(stream);
    } catch (const nlohmann::json::exception& e) {
        throw ToolError(fmt::format("{} does not parse: {}", file.generic_string(), e.what()),
                        "no_selection",
                        pick_hint);
    }
    const std::filesystem::path picked_in = selection.value("scene", "");
    std::error_code ignored;
    if (picked_in.empty() || !std::filesystem::equivalent(picked_in, session.path(), ignored)) {
        throw ToolError(
            fmt::format("the selection was made in another scene ({})", picked_in.generic_string()),
            "stale_selection",
            pick_hint);
    }
    const std::string patch = selection.value("patch", "");
    if (find_patch(scene, patch) == nullptr) {
        throw ToolError(fmt::format("the selected patch '{}' is no longer in the scene", patch),
                        "stale_selection",
                        pick_hint);
    }

    nlohmann::json out = {{"patch", patch},
                          {"uv", selection.value("uv", nlohmann::json::array())},
                          {"point", selection.value("point", nlohmann::json::array())},
                          {"time", selection.value("time", "")}};
    const nlohmann::json edge_value = selection.value("edge", nlohmann::json());
    if (!edge_value.is_string()) {
        out["edge"] = nullptr;
        std::string text = fmt::format("selected patch {}", patch);
        if (out["uv"].size() == 2 && out["point"].size() == 3) {
            text += fmt::format(" at (u, v) = ({:.3f}, {:.3f}), point ({:.1f}, {:.1f}, {:.1f}) mm",
                                out["uv"][0].get<double>(),
                                out["uv"][1].get<double>(),
                                out["point"][0].get<double>(),
                                out["point"][1].get<double>(),
                                out["point"][2].get<double>());
        }
        return {std::move(text), std::move(out), false};
    }

    const EdgeRef edge =
        parse_edge(patch + "." + edge_value.get<std::string>(), "the selected edge");
    out["edge"] = edge_name(edge);
    std::string text = fmt::format("selected edge {}", edge_name(edge));
    nlohmann::json links = nlohmann::json::array();
    for (const EdgeLink& link : all_edge_links(scene)) {
        for (const auto& [end, other] : {std::pair{link.a, link.b}, std::pair{link.b, link.a}}) {
            if (same_edge(end, edge)) {
                links.push_back(
                    {{"edge", edge_name(other)}, {"continuity", to_string(link.continuity)}});
                text += fmt::format(
                    "\n  joined {} to {}", to_string(link.continuity), edge_name(other));
            }
        }
    }
    out["links"] = std::move(links);
    out["symmetry"] = nullptr;
    for (const SymmetryLink& link : scene.symmetry) {
        if (same_edge(link.edge, edge)) {
            out["symmetry"] = to_string(link.continuity);
            text += fmt::format("\n  joined {} to its mirror image in Y = 0",
                                to_string(link.continuity));
        }
    }
    const std::vector<std::string> touching = edge_names(touching_edges(scene, edge));
    out["touching"] = touching;
    out["on_centre_plane"] = on_centre_plane(scene, edge);
    text += touching.empty() ? "\n  no other patch edge lies along it"
                             : "\n  lies along " + id_list(touching);
    if (out["on_centre_plane"].get<bool>()) {
        text += "\n  lies in the centre plane Y = 0";
    }
    return {std::move(text), std::move(out), false};
}

} // namespace ada::mcp
