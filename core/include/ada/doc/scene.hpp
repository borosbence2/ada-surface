#pragma once

// Scene document, format version 0: solved patches plus the continuity
// relations and budgets the quality report checks. M5 extends this with
// recipes (how each patch is made) and history; v0 is what `ada report` reads.

#include "ada/analysis/budgets.hpp"
#include "ada/analysis/highlight.hpp"
#include "ada/geom/bezier_patch.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ada {

enum class Continuity { G0, G1, G2 };

const char* to_string(Continuity continuity);

/// Throws `std::invalid_argument` unless `name` is "G0", "G1" or "G2".
Continuity continuity_from_string(const std::string& name);

struct ScenePatch {
    std::string id;
    BezierPatch patch;
};

struct EdgeRef {
    std::string patch;
    Edge edge;
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

struct Scene {
    std::vector<ScenePatch> patches;
    std::vector<EdgeLink> edges;
    std::vector<SymmetryLink> symmetry;
    Budgets budgets;
    /// Light for the highlight analysis; the report uses
    /// `default_light_plane` over the scene's bounding box when absent.
    std::optional<LightPlane> light;

    /// Throws `std::invalid_argument` naming the id if there is no such patch.
    const ScenePatch& patch(const std::string& id) const;
};

/// Parses and validates a v0 scene: format tag and version, units "mm",
/// unique patch ids, control-point counts matching the degrees, and edge
/// references to existing patches. Throws `std::invalid_argument` with the
/// JSON path of the first problem.
Scene scene_from_json(const nlohmann::json& json);

nlohmann::json scene_to_json(const Scene& scene);

Scene read_scene(const std::filesystem::path& path);

void write_scene(const Scene& scene, const std::filesystem::path& path);

} // namespace ada
