#include "ada/doc/scene.hpp"

#include <fmt/format.h>

#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

namespace ada {

namespace {

constexpr const char* kFormat = "ada-scene";
constexpr int kVersion = 0;

/// Errors carry the JSON path so a scene author can find the problem.
[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw std::invalid_argument(fmt::format("{}: {}", where, what));
}

/// Returned by value: GCC's dangling-reference check cannot tell that the
/// result refers into `object` rather than the temporary `where`.
nlohmann::json field(const nlohmann::json& object, const char* key, const std::string& where) {
    if (!object.is_object() || !object.contains(key)) {
        fail(where, fmt::format("missing '{}'", key));
    }
    return object.at(key);
}

Vec3 vec3_from_json(const nlohmann::json& json, const std::string& where) {
    if (!json.is_array() || json.size() != 3) {
        fail(where, "expected [x, y, z]");
    }
    try {
        return {json[0].get<double>(), json[1].get<double>(), json[2].get<double>()};
    } catch (const nlohmann::json::exception&) {
        fail(where, "coordinates must be numbers");
    }
}

nlohmann::json to_json(const Vec3& v) {
    return nlohmann::json::array({v.x(), v.y(), v.z()});
}

EdgeRef edge_ref_from_json(const nlohmann::json& json, const std::string& where) {
    if (!json.is_array() || json.size() != 2 || !json[0].is_string() || !json[1].is_string()) {
        fail(where, "expected [\"patch id\", \"UMin|UMax|VMin|VMax\"]");
    }
    try {
        return {json[0].get<std::string>(), edge_from_string(json[1].get<std::string>())};
    } catch (const std::invalid_argument& e) {
        fail(where, e.what());
    }
}

nlohmann::json to_json(const EdgeRef& ref) {
    return nlohmann::json::array({ref.patch, to_string(ref.edge)});
}

Continuity continuity_from_json(const nlohmann::json& json, const std::string& where) {
    try {
        return continuity_from_string(json.get<std::string>());
    } catch (const std::exception& e) {
        fail(where, e.what());
    }
}

ScenePatch patch_from_json(const nlohmann::json& json, const std::string& where) {
    const nlohmann::json& degree = field(json, "degree", where);
    if (!degree.is_array() || degree.size() != 2 || !degree[0].is_number_integer() ||
        !degree[1].is_number_integer()) {
        fail(where + ".degree", "expected [degree_u, degree_v]");
    }
    const nlohmann::json& id = field(json, "id", where);
    if (!id.is_string() || id.get<std::string>().empty()) {
        fail(where + ".id", "expected a non-empty string");
    }
    const nlohmann::json& points_json = field(json, "control_points", where);
    if (!points_json.is_array()) {
        fail(where + ".control_points", "expected an array of [x, y, z]");
    }
    std::vector<Vec3> points;
    for (std::size_t k = 0; k < points_json.size(); ++k) {
        points.push_back(
            vec3_from_json(points_json[k], fmt::format("{}.control_points[{}]", where, k)));
    }
    try {
        return {id.get<std::string>(),
                BezierPatch(degree[0].get<int>(), degree[1].get<int>(), std::move(points))};
    } catch (const std::invalid_argument& e) {
        fail(where, e.what());
    }
}

std::optional<LightPlane> light_from_json(const nlohmann::json& json, const std::string& where) {
    if (json.is_null()) {
        return std::nullopt;
    }
    try {
        return make_light_plane(
            vec3_from_json(field(json, "origin", where), where + ".origin"),
            vec3_from_json(field(json, "normal", where), where + ".normal"),
            vec3_from_json(field(json, "direction", where), where + ".direction"),
            field(json, "spacing", where).get<double>());
    } catch (const nlohmann::json::exception& e) {
        fail(where, e.what());
    } catch (const std::invalid_argument& e) {
        fail(where, e.what());
    }
}

void check_ref(const Scene& scene, const EdgeRef& ref, const std::string& where) {
    try {
        static_cast<void>(scene.patch(ref.patch));
    } catch (const std::invalid_argument& e) {
        fail(where, e.what());
    }
}

} // namespace

const char* to_string(Continuity continuity) {
    switch (continuity) {
    case Continuity::G0:
        return "G0";
    case Continuity::G1:
        return "G1";
    case Continuity::G2:
        return "G2";
    }
    throw std::invalid_argument("unknown continuity");
}

Continuity continuity_from_string(const std::string& name) {
    for (const Continuity c : {Continuity::G0, Continuity::G1, Continuity::G2}) {
        if (name == to_string(c)) {
            return c;
        }
    }
    throw std::invalid_argument(
        fmt::format("'{}' is not a continuity; expected G0, G1 or G2", name));
}

const ScenePatch& Scene::patch(const std::string& id) const {
    for (const ScenePatch& p : patches) {
        if (p.id == id) {
            return p;
        }
    }
    throw std::invalid_argument(fmt::format("no patch with id '{}'", id));
}

Scene scene_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        fail("$", "a scene is a JSON object");
    }
    if (json.value("format", "") != kFormat) {
        fail("$.format", fmt::format("expected \"{}\"", kFormat));
    }
    if (json.value("version", -1) != kVersion) {
        fail("$.version", fmt::format("this build reads version {}", kVersion));
    }
    if (json.value("units", "") != "mm") {
        fail("$.units", "expected \"mm\"; model space is millimetres");
    }

    Scene scene;
    const nlohmann::json& patches = field(json, "patches", "$");
    if (!patches.is_array()) {
        fail("$.patches", "expected an array");
    }
    std::set<std::string> ids;
    for (std::size_t k = 0; k < patches.size(); ++k) {
        const std::string where = fmt::format("$.patches[{}]", k);
        ScenePatch patch = patch_from_json(patches[k], where);
        if (!ids.insert(patch.id).second) {
            fail(where + ".id", fmt::format("duplicate patch id '{}'", patch.id));
        }
        scene.patches.push_back(std::move(patch));
    }

    const nlohmann::json edges = json.value("edges", nlohmann::json::array());
    for (std::size_t k = 0; k < edges.size(); ++k) {
        const std::string where = fmt::format("$.edges[{}]", k);
        EdgeLink link{
            edge_ref_from_json(field(edges[k], "a", where), where + ".a"),
            edge_ref_from_json(field(edges[k], "b", where), where + ".b"),
            continuity_from_json(field(edges[k], "continuity", where), where + ".continuity")};
        check_ref(scene, link.a, where + ".a");
        check_ref(scene, link.b, where + ".b");
        scene.edges.push_back(std::move(link));
    }

    const nlohmann::json symmetry = json.value("symmetry", nlohmann::json::array());
    for (std::size_t k = 0; k < symmetry.size(); ++k) {
        const std::string where = fmt::format("$.symmetry[{}]", k);
        SymmetryLink link{
            edge_ref_from_json(field(symmetry[k], "edge", where), where + ".edge"),
            continuity_from_json(field(symmetry[k], "continuity", where), where + ".continuity")};
        check_ref(scene, link.edge, where + ".edge");
        scene.symmetry.push_back(std::move(link));
    }

    try {
        scene.budgets = budgets_from_json(json.value("budgets", nlohmann::json(nullptr)));
    } catch (const std::invalid_argument& e) {
        fail("$.budgets", e.what());
    }
    scene.light = light_from_json(json.value("light", nlohmann::json(nullptr)), "$.light");
    return scene;
}

nlohmann::json scene_to_json(const Scene& scene) {
    nlohmann::json patches = nlohmann::json::array();
    for (const ScenePatch& p : scene.patches) {
        nlohmann::json points = nlohmann::json::array();
        for (const Vec3& point : p.patch.control_points()) {
            points.push_back(to_json(point));
        }
        patches.push_back({{"id", p.id},
                           {"degree", {p.patch.degree_u(), p.patch.degree_v()}},
                           {"control_points", std::move(points)}});
    }
    nlohmann::json edges = nlohmann::json::array();
    for (const EdgeLink& link : scene.edges) {
        edges.push_back({{"a", to_json(link.a)},
                         {"b", to_json(link.b)},
                         {"continuity", to_string(link.continuity)}});
    }
    nlohmann::json symmetry = nlohmann::json::array();
    for (const SymmetryLink& link : scene.symmetry) {
        symmetry.push_back(
            {{"edge", to_json(link.edge)}, {"continuity", to_string(link.continuity)}});
    }

    // Only budgets that differ from the defaults are written, so a scene file
    // states its own requirements and inherits future default changes.
    const nlohmann::json defaults = to_json(Budgets{});
    const nlohmann::json actual = to_json(scene.budgets);
    nlohmann::json budgets = nlohmann::json::object();
    for (const auto& [key, value] : actual.items()) {
        if (value != defaults.at(key)) {
            budgets[key] = value;
        }
    }

    nlohmann::json out = {
        {"format", kFormat},
        {"version", kVersion},
        {"units", "mm"},
        {"patches", std::move(patches)},
        {"edges", std::move(edges)},
        {"symmetry", std::move(symmetry)},
        {"budgets", std::move(budgets)},
    };
    if (scene.light.has_value()) {
        out["light"] = {{"origin", to_json(scene.light->origin)},
                        {"normal", to_json(scene.light->normal)},
                        {"direction", to_json(scene.light->direction)},
                        {"spacing", scene.light->spacing}};
    }
    return out;
}

Scene read_scene(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::invalid_argument(fmt::format("cannot open {} for reading", path.string()));
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(stream);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::invalid_argument(fmt::format("{}: {}", path.string(), e.what()));
    }
    return scene_from_json(json);
}

void write_scene(const Scene& scene, const std::filesystem::path& path) {
    std::ofstream stream(path);
    if (!stream) {
        throw std::invalid_argument(fmt::format("cannot open {} for writing", path.string()));
    }
    stream << scene_to_json(scene).dump(2) << '\n';
}

} // namespace ada
