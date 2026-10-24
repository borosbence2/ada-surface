#pragma once

// The scene document. A scene says how its surfaces are made (recipes over
// curves and other patches) and what they must satisfy (edge and symmetry
// links, pins, budgets); `solve_scene` turns that into control points. The
// solved control points are stored too, so that reports and viewers work
// without re-solving, and the next solve can start from them.
//
// Format version 1. Version 0 files (solved patches and links only, M2) still
// load; their patches have no recipe and are treated as fixed.

#include "ada/analysis/budgets.hpp"
#include "ada/analysis/highlight.hpp"
#include "ada/geom/bezier_curve.hpp"
#include "ada/geom/bezier_patch.hpp"
#include "ada/geom/continuity.hpp"
#include "ada/solve/fairness.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ada {

/// A curve given the way a designer gives one: points it passes through and a
/// degree (`through`), or, for imported data, its control points.
struct SceneCurve {
    std::string id;
    BezierCurve curve;
    /// The authored points when the curve came from `through`; written back
    /// instead of the derived control points, so a round trip keeps the input.
    std::optional<std::vector<Vec3>> through;
};

struct EdgeRef {
    std::string patch;
    Edge edge;
};

/// A patch bounded by four curves, one per edge, keyed in `Edge` order
/// (UMin, UMax, VMin, VMax). The curves must meet at the corners; they may
/// run either way. The interior is left to the solver's fairness energy.
struct FromBoundaries {
    std::array<std::string, 4> curves;
};

/// A patch filling the gap between an edge of one patch and an edge of
/// another, joined to both with `continuity`. v runs from `from` (VMin) to
/// `to` (VMax); u follows `from`'s edge. Both sources are held fixed while
/// the blend is solved.
struct Blend {
    EdgeRef from;
    EdgeRef to;
    Continuity continuity = Continuity::G2;
    /// Degree across the gap; at least 2k + 1 for Gk on both sides. Defaults
    /// to exactly that.
    std::optional<int> degree_across;
};

using Recipe = std::variant<FromBoundaries, Blend>;

struct ScenePatch {
    std::string id;
    BezierPatch patch;
    /// How the patch is made. Absent for fixed (imported or v0) patches.
    std::optional<Recipe> recipe;
    /// False when the control points are only the recipe's starting guess
    /// (the file had none); `ada solve` fixes that.
    bool solved = true;
};

/// Two patch edges that meet and must join with `continuity`.
struct EdgeLink {
    EdgeRef a;
    EdgeRef b;
    Continuity continuity;
};

/// A patch edge on the vehicle centre plane Y = 0 that must join its mirror
/// image with `continuity`.
struct SymmetryLink {
    EdgeRef edge;
    Continuity continuity;
};

/// The surface of `patch` passes through `at` at parameters (u, v).
struct SurfacePin {
    std::string patch;
    double u = 0.5;
    double v = 0.5;
    Vec3 at = Vec3::Zero();
};

struct SolveSettings {
    FairnessOrder order = FairnessOrder::E3;
    double fairness = 1.0;
    /// Pull towards the stored control points (see SolveWeights).
    double move = 0.0;
};

struct Scene {
    std::vector<SceneCurve> curves;
    std::vector<ScenePatch> patches;
    std::vector<EdgeLink> edges;
    std::vector<SymmetryLink> symmetry;
    std::vector<SurfacePin> pins;
    SolveSettings solve;
    Budgets budgets;
    /// Light for the highlight analysis; the report uses
    /// `default_light_plane` over the scene's bounding box when absent.
    std::optional<LightPlane> light;

    /// Throw `std::invalid_argument` naming the id if there is no such item.
    const ScenePatch& patch(const std::string& id) const;
    std::size_t patch_index(const std::string& id) const;
    const SceneCurve& curve(const std::string& id) const;
};

/// Parses and validates a scene (version 0 or 1): format tag, units "mm",
/// unique ids, references to existing curves and patches, control-point
/// counts matching degrees. Patches with a recipe but no control points get
/// the recipe's starting geometry and `solved = false`. Throws
/// `std::invalid_argument` with the JSON path of the first problem.
Scene scene_from_json(const nlohmann::json& json);

/// Always writes version 1.
nlohmann::json scene_to_json(const Scene& scene);

Scene read_scene(const std::filesystem::path& path);

void write_scene(const Scene& scene, const std::filesystem::path& path);

} // namespace ada
