#include "ada/doc/scene.hpp"

#include "ada/doc/recipes.hpp"
#include "ada/geom/curve_fit.hpp"

#include <fmt/format.h>

#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

namespace ada {

namespace {

constexpr const char* kFormat = "ada-scene";
constexpr int kVersion = 1;

/// Errors carry the JSON path so a scene author can find the problem.
[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw std::invalid_argument(fmt::format("{}: {}", where, what));
}

/// Rethrows an error that already carries a JSON path unchanged; prefixes
/// `where` to one that does not.
[[noreturn]] void rethrow_at(const std::string& where, const std::invalid_argument& e) {
    const std::string message = e.what();
    if (message.rfind("$", 0) == 0) {
        throw e;
    }
    fail(where, message);
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

std::vector<Vec3> points_from_json(const nlohmann::json& json, const std::string& where) {
    if (!json.is_array()) {
        fail(where, "expected an array of [x, y, z]");
    }
    std::vector<Vec3> points;
    for (std::size_t k = 0; k < json.size(); ++k) {
        points.push_back(vec3_from_json(json[k], fmt::format("{}[{}]", where, k)));
    }
    return points;
}

nlohmann::json to_json(const Vec3& v) {
    return nlohmann::json::array({v.x(), v.y(), v.z()});
}

nlohmann::json to_json(const std::vector<Vec3>& points) {
    nlohmann::json out = nlohmann::json::array();
    for (const Vec3& p : points) {
        out.push_back(to_json(p));
    }
    return out;
}

EdgeRef edge_ref_from_json(const nlohmann::json& json, const std::string& where) {
    if (!json.is_array() || json.size() != 2 || !json[0].is_string() || !json[1].is_string()) {
        fail(where, "expected [\"patch id\", \"UMin|UMax|VMin|VMax\"]");
    }
    try {
        return {json[0].get<std::string>(), edge_from_string(json[1].get<std::string>())};
    } catch (const std::invalid_argument& e) {
        rethrow_at(where, e);
    }
}

nlohmann::json to_json(const EdgeRef& ref) {
    return nlohmann::json::array({ref.patch, to_string(ref.edge)});
}

Continuity continuity_from_json(const nlohmann::json& json, const std::string& where) {
    if (!json.is_string()) {
        fail(where, "expected \"G0\", \"G1\" or \"G2\"");
    }
    try {
        return continuity_from_string(json.get<std::string>());
    } catch (const std::invalid_argument& e) {
        rethrow_at(where, e);
    }
}

std::string string_field(const nlohmann::json& json, const char* key, const std::string& where) {
    const nlohmann::json value = field(json, key, where);
    if (!value.is_string() || value.get<std::string>().empty()) {
        fail(where + "." + key, "expected a non-empty string");
    }
    return value.get<std::string>();
}

SceneCurve curve_from_json(const nlohmann::json& json, const std::string& where) {
    SceneCurve curve{string_field(json, "id", where), BezierCurve({Vec3::Zero()}), std::nullopt};
    try {
        if (json.contains("through")) {
            const nlohmann::json degree = field(json, "degree", where);
            if (!degree.is_number_integer()) {
                fail(where + ".degree", "expected an integer");
            }
            std::vector<Vec3> through = points_from_json(json.at("through"), where + ".through");
            curve.curve = curve_through_points(through, degree.get<int>());
            curve.through = std::move(through);
        } else if (json.contains("control_points")) {
            curve.curve =
                BezierCurve(points_from_json(json.at("control_points"), where + ".control_points"));
        } else {
            fail(where, "a curve needs 'through' (with 'degree') or 'control_points'");
        }
    } catch (const std::invalid_argument& e) {
        rethrow_at(where, e);
    }
    return curve;
}

std::optional<int>
optional_degree(const nlohmann::json& json, const char* name, const std::string& where) {
    if (!json.contains(name)) {
        return std::nullopt;
    }
    const nlohmann::json& value = json.at(name);
    if (!value.is_number_integer() || value.get<int>() < 1 || value.get<int>() > kMaxRecipeDegree) {
        fail(fmt::format("{}.{}", where, name),
             fmt::format("expected an integer from 1 to {}", kMaxRecipeDegree));
    }
    return value.get<int>();
}

Recipe recipe_from_json(const nlohmann::json& json, const std::string& where) {
    const std::string type = string_field(json, "type", where);
    if (type == "from_boundaries") {
        FromBoundaries recipe;
        const std::array<Edge, 4> order{Edge::UMin, Edge::UMax, Edge::VMin, Edge::VMax};
        for (std::size_t k = 0; k < 4; ++k) {
            recipe.curves[k] = string_field(json, to_string(order[k]), where);
        }
        recipe.degree_u = optional_degree(json, "degree_u", where);
        recipe.degree_v = optional_degree(json, "degree_v", where);
        return recipe;
    }
    if (type == "blend") {
        Blend recipe{edge_ref_from_json(field(json, "from", where), where + ".from"),
                     edge_ref_from_json(field(json, "to", where), where + ".to"),
                     json.contains("continuity")
                         ? continuity_from_json(json.at("continuity"), where + ".continuity")
                         : Continuity::G2,
                     std::nullopt};
        recipe.degree_across = optional_degree(json, "degree_across", where);
        return recipe;
    }
    fail(where + ".type",
         fmt::format("unknown recipe '{}'; expected from_boundaries or blend", type));
}

nlohmann::json recipe_to_json(const Recipe& recipe) {
    if (const auto* fb = std::get_if<FromBoundaries>(&recipe)) {
        nlohmann::json out = {{"type", "from_boundaries"},
                              {"UMin", fb->curves[0]},
                              {"UMax", fb->curves[1]},
                              {"VMin", fb->curves[2]},
                              {"VMax", fb->curves[3]}};
        if (fb->degree_u.has_value()) {
            out["degree_u"] = *fb->degree_u;
        }
        if (fb->degree_v.has_value()) {
            out["degree_v"] = *fb->degree_v;
        }
        return out;
    }
    const auto& b = std::get<Blend>(recipe);
    nlohmann::json out = {{"type", "blend"},
                          {"from", to_json(b.from)},
                          {"to", to_json(b.to)},
                          {"continuity", to_string(b.continuity)}};
    if (b.degree_across.has_value()) {
        out["degree_across"] = *b.degree_across;
    }
    return out;
}

void check_ref(const Scene& scene, const std::string& patch, const std::string& where) {
    try {
        static_cast<void>(scene.patch(patch));
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
        rethrow_at(where, e);
    }
}

SolveSettings solve_from_json(const nlohmann::json& json, const std::string& where) {
    SolveSettings settings;
    if (json.is_null()) {
        return settings;
    }
    if (!json.is_object()) {
        fail(where, "expected an object");
    }
    for (const auto& [key, value] : json.items()) {
        try {
            if (key == "fairness_order") {
                const std::string order = value.get<std::string>();
                if (order == "E2") {
                    settings.order = FairnessOrder::E2;
                } else if (order == "E3") {
                    settings.order = FairnessOrder::E3;
                } else {
                    fail(where + ".fairness_order", "expected \"E2\" or \"E3\"");
                }
            } else if (key == "fairness") {
                settings.fairness = value.get<double>();
            } else if (key == "move") {
                settings.move = value.get<double>();
            } else {
                fail(where, fmt::format("unknown solve setting '{}'", key));
            }
        } catch (const nlohmann::json::exception& e) {
            fail(where + "." + key, e.what());
        }
    }
    return settings;
}

nlohmann::json solve_to_json(const SolveSettings& s) {
    nlohmann::json out = nlohmann::json::object();
    const SolveSettings defaults;
    if (s.order != defaults.order) {
        out["fairness_order"] = s.order == FairnessOrder::E2 ? "E2" : "E3";
    }
    if (s.fairness != defaults.fairness) {
        out["fairness"] = s.fairness;
    }
    if (s.move != defaults.move) {
        out["move"] = s.move;
    }
    return out;
}

} // namespace

const ScenePatch& Scene::patch(const std::string& id) const {
    return patches[patch_index(id)];
}

std::size_t Scene::patch_index(const std::string& id) const {
    for (std::size_t k = 0; k < patches.size(); ++k) {
        if (patches[k].id == id) {
            return k;
        }
    }
    throw std::invalid_argument(fmt::format("no patch with id '{}'", id));
}

const SceneCurve& Scene::curve(const std::string& id) const {
    for (const SceneCurve& c : curves) {
        if (c.id == id) {
            return c;
        }
    }
    throw std::invalid_argument(fmt::format("no curve with id '{}'", id));
}

Scene scene_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        fail("$", "a scene is a JSON object");
    }
    if (json.value("format", "") != kFormat) {
        fail("$.format", fmt::format("expected \"{}\"", kFormat));
    }
    const int version = json.value("version", -1);
    if (version != 0 && version != kVersion) {
        fail("$.version", fmt::format("this build reads versions 0 and {}", kVersion));
    }
    if (json.value("units", "") != "mm") {
        fail("$.units", "expected \"mm\"; model space is millimetres");
    }

    Scene scene;
    if (json.contains("description")) {
        if (!json.at("description").is_string()) {
            fail("$.description", "expected a string");
        }
        scene.description = json.at("description").get<std::string>();
    }
    const nlohmann::json curves = json.value("curves", nlohmann::json::array());
    std::set<std::string> curve_ids;
    for (std::size_t k = 0; k < curves.size(); ++k) {
        const std::string where = fmt::format("$.curves[{}]", k);
        SceneCurve curve = curve_from_json(curves[k], where);
        if (!curve_ids.insert(curve.id).second) {
            fail(where + ".id", fmt::format("duplicate curve id '{}'", curve.id));
        }
        scene.curves.push_back(std::move(curve));
    }

    const nlohmann::json patches = field(json, "patches", "$");
    if (!patches.is_array()) {
        fail("$.patches", "expected an array");
    }
    std::set<std::string> ids;
    for (std::size_t k = 0; k < patches.size(); ++k) {
        const std::string where = fmt::format("$.patches[{}]", k);
        const nlohmann::json& p = patches[k];
        ScenePatch patch{
            string_field(p, "id", where), BezierPatch(0, 0, {Vec3::Zero()}), std::nullopt, true};
        if (!ids.insert(patch.id).second) {
            fail(where + ".id", fmt::format("duplicate patch id '{}'", patch.id));
        }
        if (p.contains("recipe")) {
            patch.recipe = recipe_from_json(p.at("recipe"), where + ".recipe");
        }
        if (p.contains("control_points")) {
            const nlohmann::json degree = field(p, "degree", where);
            if (!degree.is_array() || degree.size() != 2 || !degree[0].is_number_integer() ||
                !degree[1].is_number_integer()) {
                fail(where + ".degree", "expected [degree_u, degree_v]");
            }
            try {
                patch.patch = BezierPatch(
                    degree[0].get<int>(),
                    degree[1].get<int>(),
                    points_from_json(p.at("control_points"), where + ".control_points"));
            } catch (const std::invalid_argument& e) {
                rethrow_at(where, e);
            }
        } else if (patch.recipe.has_value()) {
            patch.solved = false; // starting geometry is filled in below
        } else {
            fail(where, "a patch without a recipe needs 'degree' and 'control_points'");
        }
        scene.patches.push_back(std::move(patch));
    }

    // Recipe references, then starting geometry for unsolved patches. Blends
    // need their sources' geometry first, so this runs in dependency order.
    for (std::size_t k = 0; k < scene.patches.size(); ++k) {
        const ScenePatch& p = scene.patches[k];
        const std::string where = fmt::format("$.patches[{}].recipe", k);
        if (!p.recipe.has_value()) {
            continue;
        }
        if (const auto* fb = std::get_if<FromBoundaries>(&*p.recipe)) {
            for (const std::string& id : fb->curves) {
                if (curve_ids.count(id) == 0) {
                    fail(where, fmt::format("no curve with id '{}'", id));
                }
            }
        } else {
            const auto& b = std::get<Blend>(*p.recipe);
            check_ref(scene, b.from.patch, where + ".from");
            check_ref(scene, b.to.patch, where + ".to");
        }
    }
    std::set<std::string> ready;
    for (const ScenePatch& p : scene.patches) {
        if (p.solved) {
            ready.insert(p.id);
        }
    }
    for (bool progress = true; progress;) {
        progress = false;
        for (std::size_t k = 0; k < scene.patches.size(); ++k) {
            ScenePatch& p = scene.patches[k];
            if (ready.count(p.id) > 0) {
                continue;
            }
            if (const auto* b = std::get_if<Blend>(&*p.recipe)) {
                if (ready.count(b->from.patch) == 0 || ready.count(b->to.patch) == 0) {
                    continue;
                }
            }
            try {
                p.patch = initial_patch(scene, p);
            } catch (const std::invalid_argument& e) {
                rethrow_at(fmt::format("$.patches[{}]", k), e);
            }
            ready.insert(p.id);
            progress = true;
        }
    }
    for (std::size_t k = 0; k < scene.patches.size(); ++k) {
        if (ready.count(scene.patches[k].id) == 0) {
            fail(fmt::format("$.patches[{}].recipe", k), "blends depend on each other in a cycle");
        }
    }

    const nlohmann::json edges = json.value("edges", nlohmann::json::array());
    for (std::size_t k = 0; k < edges.size(); ++k) {
        const std::string where = fmt::format("$.edges[{}]", k);
        EdgeLink link{
            edge_ref_from_json(field(edges[k], "a", where), where + ".a"),
            edge_ref_from_json(field(edges[k], "b", where), where + ".b"),
            continuity_from_json(field(edges[k], "continuity", where), where + ".continuity")};
        check_ref(scene, link.a.patch, where + ".a");
        check_ref(scene, link.b.patch, where + ".b");
        scene.edges.push_back(std::move(link));
    }

    const nlohmann::json symmetry = json.value("symmetry", nlohmann::json::array());
    for (std::size_t k = 0; k < symmetry.size(); ++k) {
        const std::string where = fmt::format("$.symmetry[{}]", k);
        SymmetryLink link{
            edge_ref_from_json(field(symmetry[k], "edge", where), where + ".edge"),
            continuity_from_json(field(symmetry[k], "continuity", where), where + ".continuity")};
        check_ref(scene, link.edge.patch, where + ".edge");
        scene.symmetry.push_back(std::move(link));
    }

    const nlohmann::json pins = json.value("pins", nlohmann::json::array());
    for (std::size_t k = 0; k < pins.size(); ++k) {
        const std::string where = fmt::format("$.pins[{}]", k);
        SurfacePin pin{string_field(pins[k], "patch", where), 0.5, 0.5, Vec3::Zero()};
        check_ref(scene, pin.patch, where + ".patch");
        const nlohmann::json uv = field(pins[k], "uv", where);
        if (!uv.is_array() || uv.size() != 2 || !uv[0].is_number() || !uv[1].is_number()) {
            fail(where + ".uv", "expected [u, v]");
        }
        pin.u = uv[0].get<double>();
        pin.v = uv[1].get<double>();
        pin.at = vec3_from_json(field(pins[k], "at", where), where + ".at");
        scene.pins.push_back(pin);
    }

    scene.solve = solve_from_json(json.value("solve", nlohmann::json(nullptr)), "$.solve");
    try {
        scene.budgets = budgets_from_json(json.value("budgets", nlohmann::json(nullptr)));
    } catch (const std::invalid_argument& e) {
        fail("$.budgets", e.what());
    }
    scene.light = light_from_json(json.value("light", nlohmann::json(nullptr)), "$.light");
    return scene;
}

nlohmann::json scene_to_json(const Scene& scene) {
    // Canonical form: sections that are empty or at their defaults are left
    // out, so an authored scene round-trips to itself.
    nlohmann::json out = {{"format", kFormat}, {"version", kVersion}, {"units", "mm"}};
    if (!scene.description.empty()) {
        out["description"] = scene.description;
    }

    if (!scene.curves.empty()) {
        nlohmann::json curves = nlohmann::json::array();
        for (const SceneCurve& c : scene.curves) {
            if (c.through.has_value()) {
                curves.push_back(
                    {{"id", c.id}, {"degree", c.curve.degree()}, {"through", to_json(*c.through)}});
            } else {
                curves.push_back(
                    {{"id", c.id}, {"control_points", to_json(c.curve.control_points())}});
            }
        }
        out["curves"] = std::move(curves);
    }

    nlohmann::json patches = nlohmann::json::array();
    for (const ScenePatch& p : scene.patches) {
        nlohmann::json patch = {{"id", p.id}};
        if (p.recipe.has_value()) {
            patch["recipe"] = recipe_to_json(*p.recipe);
        }
        if (p.solved) {
            patch["degree"] = {p.patch.degree_u(), p.patch.degree_v()};
            patch["control_points"] = to_json(p.patch.control_points());
        }
        patches.push_back(std::move(patch));
    }
    out["patches"] = std::move(patches);

    if (!scene.edges.empty()) {
        nlohmann::json edges = nlohmann::json::array();
        for (const EdgeLink& link : scene.edges) {
            edges.push_back({{"a", to_json(link.a)},
                             {"b", to_json(link.b)},
                             {"continuity", to_string(link.continuity)}});
        }
        out["edges"] = std::move(edges);
    }
    if (!scene.symmetry.empty()) {
        nlohmann::json symmetry = nlohmann::json::array();
        for (const SymmetryLink& link : scene.symmetry) {
            symmetry.push_back(
                {{"edge", to_json(link.edge)}, {"continuity", to_string(link.continuity)}});
        }
        out["symmetry"] = std::move(symmetry);
    }
    if (!scene.pins.empty()) {
        nlohmann::json pins = nlohmann::json::array();
        for (const SurfacePin& pin : scene.pins) {
            pins.push_back({{"patch", pin.patch}, {"uv", {pin.u, pin.v}}, {"at", to_json(pin.at)}});
        }
        out["pins"] = std::move(pins);
    }
    if (const nlohmann::json solve = solve_to_json(scene.solve); !solve.empty()) {
        out["solve"] = solve;
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
    if (!budgets.empty()) {
        out["budgets"] = std::move(budgets);
    }
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
