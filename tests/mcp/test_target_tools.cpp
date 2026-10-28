#include "ada/doc/scene.hpp"

#include "server.hpp"
#include "support/mcp_calls.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using ada::mcp::McpServer;
using ada::test::fails;
using ada::test::ok;
using ada::test::server_with_new_scene;
using ada::test::TempDir;
using nlohmann::json;

namespace {

/// The hood's height field (as in the authored cases), x 0..600 front to
/// rear, y 0..700 from the centre line out.
double hood_z(double x, double y) {
    const double s = x / 1200.0;
    const double t = y / 700.0;
    return 800.0 + 40.0 * s - 45.0 * s * s - 70.0 * t * t;
}

json hood_point(double x, double y) {
    return {x, y, hood_z(x, y)};
}

/// The hood as a CAD-like OBJ mesh: vertices on the surface every 25 mm.
void write_hood_obj(const std::filesystem::path& file) {
    constexpr int kNx = 24; // 600 / 25
    constexpr int kNy = 28; // 700 / 25
    std::ofstream out(file, std::ios::binary);
    out.precision(17);
    for (int j = 0; j <= kNy; ++j) {
        for (int i = 0; i <= kNx; ++i) {
            const double x = 25.0 * i;
            const double y = 25.0 * j;
            out << "v " << x << ' ' << y << ' ' << hood_z(x, y) << '\n';
        }
    }
    auto at = [](int i, int j) { return (kNx + 1) * j + i + 1; }; // OBJ counts from 1
    for (int j = 0; j < kNy; ++j) {
        for (int i = 0; i < kNx; ++i) {
            out << "f " << at(i, j) << ' ' << at(i + 1, j) << ' ' << at(i + 1, j + 1) << '\n';
            out << "f " << at(i, j) << ' ' << at(i + 1, j + 1) << ' ' << at(i, j + 1) << '\n';
        }
    }
}

/// Front centre, front side, rear side, rear centre: u runs out from the
/// centre line, v rearwards, and the centre line is the first column's UMin.
json hood_corners() {
    return {hood_point(0, 0), hood_point(0, 700), hood_point(600, 700), hood_point(600, 0)};
}

json read_json(const std::string& file) {
    std::ifstream stream(file);
    return json::parse(stream);
}

} // namespace

TEST_CASE("target_add attaches a mesh and target_info describes its outline", "[mcp][target]") {
    const TempDir dir("target-add");
    write_hood_obj(dir.path / "hood.obj");
    McpServer server = server_with_new_scene(dir);

    CHECK(fails(server, "target_info").at("code") == "no_target");
    const json added =
        ok(server, "target_add", {{"id", "scan"}, {"file", "hood.obj"}, {"sample", "vertices"}});
    CHECK(added.at("details").at("points") == 25 * 29);
    const json file = read_json(dir.file("test.ada.json"));
    CHECK(file.at("targets") ==
          json::array({{{"id", "scan"}, {"file", "hood.obj"}, {"sample", "vertices"}}}));

    const json info = ok(server, "target_info");
    CHECK(info.at("id") == "scan");
    CHECK(info.at("points") == 25 * 29);
    CHECK(info.at("triangles") == 2 * 24 * 28);
    CHECK(info.at("facing") == "z");
    REQUIRE(info.at("outlines").size() == 1);
    const json& outline = info.at("outlines")[0];
    REQUIRE(outline.at("corners").size() == 4);
    REQUIRE(outline.at("sides").size() == 4);
    int in_centre_plane = 0;
    for (const json& side : outline.at("sides")) {
        in_centre_plane += side.at("in_centre_plane").get<bool>() ? 1 : 0;
    }
    CHECK(in_centre_plane == 1);
    bool front_centre = false;
    for (const json& corner : outline.at("corners")) {
        front_centre = front_centre || corner.at("point") == hood_point(0, 0);
    }
    CHECK(front_centre);
    CHECK(ok(server, "target_info", {{"id", "scan"}}).at("id") == "scan");
    CHECK(fails(server, "target_info", {{"id", "nope"}}).at("code") == "unknown_target");

    CHECK(fails(server, "target_add", {{"id", "scan"}, {"file", "hood.obj"}}).at("code") ==
          "id_taken");
    ok(server, "target_add", {{"id", "scan"}, {"file", "hood.obj"}, {"replace", true}});
    CHECK(read_json(dir.file("test.ada.json")).at("targets")[0].contains("sample") == false);
    CHECK(fails(server, "target_add", {{"id", "gone"}, {"file", "missing.obj"}}).at("code") ==
          "target_unreadable");
    CHECK(fails(server, "target_add", {{"id", "odd"}, {"file", "hood.obj"}, {"sample", "every"}})
              .at("code") == "invalid_arguments");
}

TEST_CASE("network_from_target lays a network on the target, and the fit follows the target",
          "[mcp][target]") {
    const TempDir dir("target-network");
    write_hood_obj(dir.path / "hood.obj");
    McpServer server = server_with_new_scene(dir);
    CHECK(fails(server,
                "network_from_target",
                {{"prefix", "hood"}, {"corners", hood_corners()}, {"rows", 2}, {"cols", 2}})
              .at("code") == "no_target");
    ok(server, "target_add", {{"id", "scan"}, {"file", "hood.obj"}, {"sample", "vertices"}});

    json shuffled = hood_corners();
    std::swap(shuffled[1], shuffled[2]);
    CHECK(fails(server,
                "network_from_target",
                {{"prefix", "hood"}, {"corners", shuffled}, {"rows", 2}, {"cols", 2}})
              .at("code") == "layout_failed");

    const json network =
        ok(server,
           "network_from_target",
           {{"prefix", "hood"}, {"corners", hood_corners()}, {"rows", 2}, {"cols", 2}});
    const json& details = network.at("details");
    CHECK(details.at("patches") == json::array({"hood_0_0", "hood_0_1", "hood_1_0", "hood_1_1"}));
    CHECK(details.at("curves").size() == 12);
    CHECK(details.at("centre_plane_edges") == json::array({"hood_0_0.UMin", "hood_1_0.UMin"}));
    CHECK(fails(server,
                "network_from_target",
                {{"prefix", "hood"}, {"corners", hood_corners()}, {"rows", 2}, {"cols", 2}})
              .at("code") == "id_taken");
    for (const char* edge : {"hood_0_0.UMin", "hood_1_0.UMin"}) {
        ok(server, "symmetry_constrain", {{"edge", edge}, {"continuity", "G2"}});
    }

    // Budgets merge into the scene's.
    ok(server, "budgets_set", {{"budgets", {{"fit_max_mm", 0.05}}}});
    CHECK(read_json(dir.file("test.ada.json")).at("budgets").at("fit_max_mm") == 0.05);
    CHECK(fails(server, "budgets_set", {{"budgets", {{"nonsense", 1}}}}).at("code") ==
          "invalid_arguments");

    CHECK(fails(server, "fit_set", {{"target", "nope"}}).at("code") == "unknown_target");
    CHECK(fails(server, "fit_set", {{"weight", 5}}).at("code") == "invalid_arguments");
    const json fitted = ok(server, "fit_set", {{"target", "scan"}});
    const json& fit = fitted.at("fit");
    CHECK(fit.at("target") == "scan");
    CHECK(fit.at("points") == 25 * 29);
    CHECK(fit.at("outside") == 0);
    CHECK(fit.at("max_mm").get<double>() < 0.05);
    CHECK(read_json(dir.file("test.ada.json")).at("fit").at("target") == "scan");

    // Economy: already within budget at the curves' degree, so no raise.
    const json economical = ok(server, "fit_set", {{"economy", true}});
    CHECK(economical.at("economy").at("met") == true);
    CHECK(economical.at("economy").at("steps").empty());
    CHECK(read_json(dir.file("test.ada.json")).at("fit").at("economy") == true);

    ok(server, "fit_set", {{"target", "none"}});
    CHECK(read_json(dir.file("test.ada.json")).contains("fit") == false);
}

namespace {

const std::filesystem::path kL3Case =
    std::filesystem::path(ADA_SOURCE_DIR) / "cases" / "L3-hood-from-target.ada.json";
const std::filesystem::path kDrivAerHood =
    std::filesystem::path(ADA_SOURCE_DIR) / "cases" / "targets" / "drivaer-hood.ply";

/// The L3 case as an agent builds it from "rebuild this hood, G2 at Y = 0,
/// 0.5 mm": the target, its outline's corners, a 4 x 4 network on them,
/// symmetry on the centre edges, the budget, and the fit last. Returns the
/// last edit's result.
json build_l3(McpServer& server, const std::filesystem::path& scene) {
    ok(server,
       "scene_new",
       {{"path", scene.generic_string()},
        {"overwrite", true},
        {"description",
         "L3: the DrivAer hood (DrivAerML run 1, CC BY-SA 4.0) rebuilt from its CAD "
         "tessellation: a 4 x 4 network laid on the hood's outline (network_from_target), G2 on "
         "every joint and at Y = 0, fitted to the tessellation's vertices (they lie on the CAD "
         "surface; the flat facets between them do not), within 0.5 mm. Curvature reversal is "
         "allowed because the hood itself bends upwards at its rear-outer corner. The "
         "control-net zig-zag and highlight-inflection budgets are the observed values, a "
         "regression guard: G2 joints with matched parameter speed space control points "
         "unevenly between patches of different size."}});
    ok(server,
       "target_add",
       {{"id", "drivaer"}, {"file", kDrivAerHood.generic_string()}, {"sample", "vertices"}});

    // Corners: the two in Y = 0 are the centre ends, the others the sides;
    // the front has the smaller x.
    const json info = ok(server, "target_info");
    REQUIRE(info.at("outlines").size() == 1);
    std::vector<json> centre;
    std::vector<json> side;
    for (const json& c : info.at("outlines")[0].at("corners")) {
        (std::abs(c.at("point")[1].get<double>()) < 1.0 ? centre : side).push_back(c.at("point"));
    }
    REQUIRE(centre.size() == 2);
    REQUIRE(side.size() == 2);
    auto by_x = [](const json& a, const json& b) {
        return a[0].get<double>() < b[0].get<double>();
    };
    std::sort(centre.begin(), centre.end(), by_x);
    std::sort(side.begin(), side.end(), by_x);
    const json network = ok(server,
                            "network_from_target",
                            {{"prefix", "hood"},
                             {"target", "drivaer"},
                             {"corners", {centre[0], side[0], side[1], centre[1]}},
                             {"rows", 4},
                             {"cols", 4}});
    const json& centre_edges = network.at("details").at("centre_plane_edges");
    REQUIRE(centre_edges.size() == 4);
    for (const json& edge : centre_edges) {
        ok(server, "symmetry_constrain", {{"edge", edge}, {"continuity", "G2"}});
    }
    ok(server,
       "budgets_set",
       {{"budgets",
         {{"fit_max_mm", 0.5},
          {"max_degree", 7},
          {"allow_curvature_reversal", true},
          {"max_zigzag_rows", 12},
          {"max_highlight_inflections", 3}}}});
    return ok(server, "fit_set", {{"target", "drivaer"}, {"weight", 10}, {"economy", true}});
}

/// The authored part of a scene document: no control points or degrees
/// (the solve's), and target files by name only (they are written relative
/// to wherever the scene is).
json authored(const std::filesystem::path& file) {
    json doc = ada::scene_to_json(ada::read_scene(file));
    for (json& p : doc.at("patches")) {
        p.erase("control_points");
        p.erase("degree");
    }
    for (json& t : doc.at("targets")) {
        t["file"] = std::filesystem::path(t.at("file").get<std::string>()).filename().string();
    }
    return doc;
}

} // namespace

TEST_CASE("write the L3 case through tool calls", "[.generate-cases]") {
    McpServer server = ada::test::started_server();
    const json last = build_l3(server, kL3Case);
    CHECK(last.at("quality").at("pass") == true);

    // Committed as authored, like the other cases: the bench solves it.
    json doc = ada::scene_to_json(ada::read_scene(kL3Case));
    for (json& p : doc.at("patches")) {
        p.erase("control_points");
        p.erase("degree");
    }
    std::ofstream(kL3Case, std::ios::binary) << doc.dump(2) << '\n';
}

TEST_CASE("the L3 case can be built through tool calls alone", "[mcp][target][exit]") {
    const TempDir dir("l3");
    McpServer server = ada::test::started_server();
    const json last = build_l3(server, dir.path / "hood.ada.json");
    INFO(last.dump(2));
    CHECK(last.at("quality").at("pass") == true);
    CHECK(last.at("fit").at("max_mm").get<double>() <= 0.5);
    CHECK(last.at("economy").at("met") == true);
    CHECK(authored(dir.path / "hood.ada.json") == authored(kL3Case));
}
