#include "ada/doc/recipes.hpp"
#include "ada/doc/scene.hpp"

#include "server.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using ada::mcp::McpServer;
using nlohmann::json;

namespace {

/// The quality budgets' G2 tolerance is 1 %; a solve reaches far better.
constexpr double kSameMm = 1e-6;

/// A fresh, empty directory for one test, removed again when it ends.
struct TempDir {
    std::filesystem::path path;

    explicit TempDir(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("ada-mcp-edit-" + name)) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string file(const std::string& name) const { return (path / name).generic_string(); }
};

json call(McpServer& server, const std::string& tool, json arguments = json::object()) {
    const auto response =
        server.handle({{"jsonrpc", "2.0"},
                       {"id", 7},
                       {"method", "tools/call"},
                       {"params", {{"name", tool}, {"arguments", std::move(arguments)}}}});
    REQUIRE(response.has_value());
    REQUIRE(response->contains("result"));
    return response->at("result");
}

/// Calls a tool that must succeed and returns its structured content.
json ok(McpServer& server, const std::string& tool, json arguments = json::object()) {
    const json result = call(server, tool, std::move(arguments));
    INFO(tool << ": " << result.at("content")[0].at("text").get<std::string>());
    REQUIRE(result.at("isError") == false);
    return result.at("structuredContent");
}

/// Calls a tool that must fail and returns its structured error.
json fails(McpServer& server, const std::string& tool, json arguments = json::object()) {
    const json result = call(server, tool, std::move(arguments));
    INFO(tool << ": " << result.at("content")[0].at("text").get<std::string>());
    REQUIRE(result.at("isError") == true);
    return result.at("structuredContent").at("error");
}

std::string contents(const std::string& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

McpServer server_with_new_scene(const TempDir& dir) {
    McpServer server({});
    static_cast<void>(server.handle({{"jsonrpc", "2.0"},
                                     {"id", 1},
                                     {"method", "initialize"},
                                     {"params", {{"protocolVersion", "2025-06-18"}}}}));
    ok(server, "scene_new", {{"path", dir.file("test.ada.json")}});
    return server;
}

/// The hood's height field (as in the authored cases), even in y.
double hood_z(double x, double y) {
    const double s = x / 1200.0;
    const double t = y / 700.0;
    return 800.0 + 40.0 * s - 45.0 * s * s - 70.0 * t * t;
}

/// Four points on the hood along a straight line in plan view: a cubic
/// passes through them exactly.
json line(double x0, double y0, double x1, double y1) {
    json points = json::array();
    for (int k = 0; k <= 3; ++k) {
        const double a = k / 3.0;
        const double x = x0 + a * (x1 - x0);
        const double y = y0 + a * (y1 - y0);
        points.push_back({x, y, hood_z(x, y)});
    }
    return points;
}

json curve(const std::string& id, json points) {
    return {{"id", id}, {"points", std::move(points)}};
}

/// Two patches over x 0..600: "left" on y 0..`left_to`, "right" on
/// y `right_from`..700. With left_to == right_from they share the curve
/// "mid" and touch along left.UMax ~ right.UMin; otherwise a gap lies
/// between them.
void two_patches(McpServer& server, double left_to, double right_from) {
    const bool shared = left_to == right_from;
    json curves = {curve("y0", line(0, 0, 600, 0)),
                   curve("l_x0", line(0, 0, 0, left_to)),
                   curve("l_x600", line(600, 0, 600, left_to)),
                   curve("r_x0", line(0, right_from, 0, 700)),
                   curve("r_x600", line(600, right_from, 600, 700)),
                   curve("y700", line(0, 700, 600, 700))};
    if (shared) {
        curves.push_back(curve("mid", line(0, left_to, 600, left_to)));
    } else {
        curves.push_back(curve("l_edge", line(0, left_to, 600, left_to)));
        curves.push_back(curve("r_edge", line(0, right_from, 600, right_from)));
    }
    ok(server, "curve_from_points", {{"curves", curves}});
    ok(server,
       "patch_from_boundaries",
       {{"id", "left"},
        {"UMin", "y0"},
        {"UMax", shared ? "mid" : "l_edge"},
        {"VMin", "l_x0"},
        {"VMax", "l_x600"}});
    ok(server,
       "patch_from_boundaries",
       {{"id", "right"},
        {"UMin", shared ? "mid" : "r_edge"},
        {"UMax", "y700"},
        {"VMin", "r_x0"},
        {"VMax", "r_x600"}});
}

/// "hood_u01" and the like: the authored hood's curve names.
std::string fmt_line(const char* prefix, int a, int b) {
    return prefix + std::to_string(a) + std::to_string(b);
}

json summary(McpServer& server) {
    return ok(server, "scene_summary");
}

json patch_degree(McpServer& server, const std::string& id) {
    // Keep the summary alive: a range-for over a reference into a temporary
    // would dangle (lifetime extension only reaches it in C++23).
    const json scene = summary(server);
    for (const json& p : scene.at("patches")) {
        if (p.at("id") == id) {
            return p.at("degree");
        }
    }
    FAIL("no patch " << id);
    return nullptr;
}

} // namespace

TEST_CASE("scene_new creates an empty scene and will not overwrite one", "[mcp][edit]") {
    const TempDir dir("new");
    McpServer server = server_with_new_scene(dir);
    CHECK(summary(server).at("patches").empty());

    CHECK(fails(server, "scene_new", {{"path", dir.file("test.ada.json")}}).at("code") ==
          "file_exists");
    CHECK(fails(server, "scene_new", {{"path", dir.file("test.json")}}).at("code") ==
          "invalid_arguments");
    // A new scene may go in a new folder.
    ok(server, "scene_new", {{"path", dir.file("new/folder/scene.ada.json")}});
    CHECK(std::filesystem::exists(dir.path / "new" / "folder" / "scene.ada.json"));
    ok(server, "scene_open", {{"path", dir.file("test.ada.json")}});
    ok(server,
       "scene_new",
       {{"path", dir.file("test.ada.json")},
        {"overwrite", true},
        {"description", "a roof"},
        {"budgets", {{"max_degree", 5}}}});
    const ada::Scene scene = ada::read_scene(dir.file("test.ada.json"));
    CHECK(scene.description == "a roof");
    CHECK(scene.budgets.max_degree == 5);
    CHECK(fails(server, "scene_new", {{"path", dir.file("b.ada.json")}, {"budgets", {{"nope", 1}}}})
              .at("code") == "invalid_arguments");
}

TEST_CASE("curve_from_points adds curves, reports their fit and replaces only when asked",
          "[mcp][edit]") {
    const TempDir dir("curves");
    McpServer server = server_with_new_scene(dir);

    const json added =
        ok(server, "curve_from_points", {{"curves", {curve("a", line(0, 0, 600, 0))}}});
    CHECK(added.at("details").at("curves")[0].at("fit_mm").get<double>() < kSameMm);
    CHECK(added.at("quality").at("pass") == true);

    CHECK(fails(server, "curve_from_points", {{"curves", {curve("a", line(0, 0, 600, 10))}}})
              .at("code") == "id_taken");
    ok(server,
       "curve_from_points",
       {{"curves", {curve("a", line(0, 0, 600, 10))}}, {"replace", true}});

    // Six points on a sine wave: a cubic can only approximate them.
    json wave = json::array();
    for (int k = 0; k <= 5; ++k) {
        wave.push_back({100.0 * k, 0.0, 800.0 + 20.0 * std::sin(k * 1.3)});
    }
    const json fit = ok(server, "curve_from_points", {{"curves", {curve("wave", wave)}}});
    CHECK(fit.at("details").at("curves")[0].at("fit_mm").get<double>() > 0.1);

    CHECK(fails(server, "curve_from_points", {{"curves", {curve("bad id", line(0, 0, 1, 1))}}})
              .at("code") == "invalid_arguments");
    CHECK(fails(server,
                "curve_from_points",
                {{"curves", {{{"id", "repeat"}, {"points", {{0, 0, 0}, {0, 0, 0}}}}}}})
              .at("code") == "invalid_arguments");
    CHECK(summary(server).at("curves").size() == 2);
}

TEST_CASE("patch_from_boundaries builds and solves a patch, or changes nothing", "[mcp][edit]") {
    const TempDir dir("patch");
    McpServer server = server_with_new_scene(dir);
    ok(server,
       "curve_from_points",
       {{"curves",
         {curve("y0", line(0, 0, 600, 0)),
          curve("y350", line(0, 350, 600, 350)),
          curve("x0", line(0, 0, 0, 350)),
          curve("x600", line(600, 0, 600, 350))}}});

    const std::string before = contents(dir.file("test.ada.json"));
    // UMin and VMin swapped: y0 and x0 meet, but y0 does not meet y350.
    const json error =
        fails(server,
              "patch_from_boundaries",
              {{"id", "p"}, {"UMin", "y0"}, {"UMax", "x0"}, {"VMin", "y350"}, {"VMax", "x600"}});
    CHECK(error.at("code") == "curves_do_not_meet");
    CHECK(error.at("try").get<std::string>().find("closed loop") != std::string::npos);
    CHECK(contents(dir.file("test.ada.json")) == before);

    CHECK(fails(server,
                "patch_from_boundaries",
                {{"id", "p"}, {"UMin", "y0"}, {"UMax", "nope"}, {"VMin", "x0"}, {"VMax", "x600"}})
              .at("code") == "unknown_curve");

    const json made =
        ok(server,
           "patch_from_boundaries",
           {{"id", "p"}, {"UMin", "y0"}, {"UMax", "y350"}, {"VMin", "x0"}, {"VMax", "x600"}});
    CHECK(made.at("quality").at("pass") == true);
    CHECK(ada::read_scene(dir.file("test.ada.json")).patches.at(0).solved);
    CHECK(fails(server,
                "patch_from_boundaries",
                {{"id", "p"}, {"UMin", "y0"}, {"UMax", "y350"}, {"VMin", "x0"}, {"VMax", "x600"}})
              .at("code") == "id_taken");
}

TEST_CASE("edge_constrain links edges that lie along each other", "[mcp][edit]") {
    const TempDir dir("edges");
    McpServer server = server_with_new_scene(dir);
    two_patches(server, 350, 350);

    const json linked = ok(
        server, "edge_constrain", {{"a", "left.UMax"}, {"b", "right.UMin"}, {"continuity", "G2"}});
    CHECK(linked.at("quality").at("pass") == true);
    REQUIRE(summary(server).at("edges").size() == 1);

    // right.UMin lies along left.UMax, not along left.VMin; the error says so.
    const json apart = fails(
        server, "edge_constrain", {{"a", "right.UMin"}, {"b", "left.VMin"}, {"continuity", "G1"}});
    CHECK(apart.at("code") == "edges_do_not_meet");
    CHECK(apart.at("try").get<std::string>().find("left.UMax") != std::string::npos);

    CHECK(fails(server,
                "edge_constrain",
                {{"a", "left.Top"}, {"b", "right.UMin"}, {"continuity", "G1"}})
              .at("code") == "invalid_arguments");

    ok(server, "edge_constrain", {{"a", "right.UMin"}, {"b", "left.UMax"}, {"continuity", "G1"}});
    REQUIRE(summary(server).at("edges").size() == 1);
    CHECK(summary(server).at("edges")[0].at("continuity") == "G1");

    ok(server, "edge_constrain", {{"a", "left.UMax"}, {"b", "right.UMin"}, {"continuity", "none"}});
    CHECK(summary(server).at("edges").empty());
    CHECK(fails(server,
                "edge_constrain",
                {{"a", "left.UMax"}, {"b", "right.UMin"}, {"continuity", "none"}})
              .at("code") == "not_linked");
}

TEST_CASE("symmetry_constrain needs an edge in Y = 0", "[mcp][edit]") {
    const TempDir dir("symmetry");
    McpServer server = server_with_new_scene(dir);
    two_patches(server, 350, 350);

    const json off =
        fails(server, "symmetry_constrain", {{"edge", "left.UMax"}, {"continuity", "G2"}});
    CHECK(off.at("code") == "not_on_centre_plane");
    CHECK(off.at("try").get<std::string>().find("left.UMin") != std::string::npos);

    const json on = ok(server, "symmetry_constrain", {{"edge", "left.UMin"}, {"continuity", "G2"}});
    CHECK(on.at("quality").at("pass") == true);
    CHECK(summary(server).at("symmetry").size() == 1);
}

TEST_CASE("a contradiction comes back as a structured error and changes nothing", "[mcp][edit]") {
    const TempDir dir("contradiction");
    McpServer server = server_with_new_scene(dir);
    // A patch whose UMin curve runs 0.5 mm off the centre plane: close enough
    // to count as lying in it, but its curve holds the edge where it is.
    ok(server,
       "curve_from_points",
       {{"curves",
         {curve("y0", line(0, 0.5, 600, 0.5)),
          curve("y350", line(0, 350, 600, 350)),
          curve("x0", line(0, 0.5, 0, 350)),
          curve("x600", line(600, 0.5, 600, 350))}}});
    ok(server,
       "patch_from_boundaries",
       {{"id", "p"}, {"UMin", "y0"}, {"UMax", "y350"}, {"VMin", "x0"}, {"VMax", "x600"}});
    const std::string before = contents(dir.file("test.ada.json"));

    const json error =
        fails(server, "symmetry_constrain", {{"edge", "p.UMin"}, {"continuity", "G1"}});
    CHECK(error.at("code") == "constraints_contradict");
    const json& conflicts = error.at("details").at("conflicts");
    REQUIRE_FALSE(conflicts.empty());
    const bool names_symmetry = std::any_of(conflicts.begin(), conflicts.end(), [](const json& c) {
        return c.at("constraint") == "Y=0 p.UMin";
    });
    CHECK(names_symmetry);
    CHECK(error.at("try").get<std::string>().find("Y = 0") != std::string::npos);
    CHECK(error.at("message").get<std::string>().find("not changed") != std::string::npos);
    CHECK(contents(dir.file("test.ada.json")) == before);
}

TEST_CASE("patch_set_degree carries a degree across linked edges", "[mcp][edit]") {
    const TempDir dir("degree");
    McpServer server = server_with_new_scene(dir);
    two_patches(server, 350, 350);
    ok(server, "edge_constrain", {{"a", "left.UMax"}, {"b", "right.UMin"}, {"continuity", "G2"}});

    // The joint runs along v, so v degrees are tied; u degrees are not.
    const json set =
        ok(server, "patch_set_degree", {{"patch", "left"}, {"degree_v", 5}, {"degree_u", 4}});
    CHECK(set.at("details").at("degree_v").at("patches").size() == 2);
    CHECK(patch_degree(server, "left") == json({4, 5}));
    CHECK(patch_degree(server, "right") == json({3, 5}));
    for (const json& f : set.at("quality").at("failed")) {
        CHECK(f.at("where").get<std::string>().rfind("edge", 0) != 0); // still G2
    }

    CHECK(fails(server, "patch_set_degree", {{"patch", "left"}, {"degree_v", 2}}).at("code") ==
          "degree_below_curves");
    ok(server, "patch_set_degree", {{"patch", "right"}, {"degree_v", 3}});
    CHECK(patch_degree(server, "left") == json({4, 3}));
}

TEST_CASE("patch_blend fills a gap G2 and refuses edges that touch", "[mcp][edit]") {
    const TempDir dir("blend");
    McpServer server = server_with_new_scene(dir);
    two_patches(server, 300, 400);

    const json blended =
        ok(server, "patch_blend", {{"id", "gap"}, {"from", "left.UMax"}, {"to", "right.UMin"}});
    for (const json& f : blended.at("quality").at("failed")) {
        CHECK(f.at("where").get<std::string>().rfind("edge", 0) != 0);
    }
    CHECK(patch_degree(server, "gap") == json({3, 5}));
    CHECK(summary(server).at("edges").size() == 2); // implied by the blend

    CHECK(fails(server,
                "edge_constrain",
                {{"a", "left.UMax"}, {"b", "gap.VMin"}, {"continuity", "G1"}})
              .at("code") == "implied_by_blend");
    CHECK(fails(server, "patch_blend", {{"id", "g2"}, {"from", "left.UMax"}, {"to", "gap.VMin"}})
              .at("code") == "edges_touch");
}

TEST_CASE("fairness_set changes the solve settings within sense", "[mcp][edit]") {
    const TempDir dir("fairness");
    McpServer server = server_with_new_scene(dir);
    CHECK(fails(server, "fairness_set", {{"fairness", 0.0}}).at("code") == "invalid_arguments");
    CHECK(fails(server, "fairness_set").at("code") == "invalid_arguments");
    ok(server, "fairness_set", {{"fairness", 0.0}, {"move", 1.0}, {"order", "E2"}});
    const ada::Scene scene = ada::read_scene(dir.file("test.ada.json"));
    CHECK(scene.solve.fairness == 0.0);
    CHECK(scene.solve.move == 1.0);
    CHECK(scene.solve.order == ada::FairnessOrder::E2);
}

TEST_CASE("undo and redo step through edits", "[mcp][edit]") {
    const TempDir dir("undo");
    McpServer server = server_with_new_scene(dir);
    const std::string file = dir.file("test.ada.json");
    CHECK(fails(server, "undo").at("code") == "nothing_to_undo");

    const std::string empty = contents(file);
    ok(server, "curve_from_points", {{"curves", {curve("a", line(0, 0, 600, 0))}}});
    const std::string one = contents(file);
    ok(server, "curve_from_points", {{"curves", {curve("b", line(0, 10, 600, 10))}}});
    const std::string two = contents(file);

    // An agent sees the structured result, so it has to say what was undone.
    const json undone = ok(server, "undo");
    CHECK(contents(file) == one);
    CHECK(undone.at("changed") == "undid: added curves b");
    ok(server, "undo");
    CHECK(contents(file) == empty);
    CHECK(fails(server, "undo").at("code") == "nothing_to_undo");
    CHECK(ok(server, "redo").at("changed") == "redid: added curves a");
    CHECK(contents(file) == one);

    // A new edit drops what could be redone.
    ok(server, "curve_from_points", {{"curves", {curve("c", line(0, 20, 600, 20))}}});
    CHECK(fails(server, "redo").at("code") == "nothing_to_redo");

    // Opening a scene starts a new history.
    ok(server, "scene_open", {{"path", file}});
    CHECK(fails(server, "undo").at("code") == "nothing_to_undo");
    CHECK(two != one);
}

TEST_CASE("get_selection reads what Studio picked and what lies along it", "[mcp][edit]") {
    const TempDir dir("selection");
    McpServer server = server_with_new_scene(dir);
    two_patches(server, 350, 350);
    ok(server, "edge_constrain", {{"a", "left.UMax"}, {"b", "right.UMin"}, {"continuity", "G2"}});
    CHECK(fails(server, "get_selection").at("code") == "no_selection");

    auto pick = [&](json edge) {
        std::ofstream(dir.path / "selection.json") << json{{"scene", dir.file("test.ada.json")},
                                                           {"patch", "left"},
                                                           {"edge", std::move(edge)},
                                                           {"uv", {1.0, 0.4}},
                                                           {"point", {240.0, 350.0, 780.0}},
                                                           {"time", "2026-10-26T20:00:00Z"}}
                                                          .dump();
    };
    pick("UMax");
    const json edge = ok(server, "get_selection");
    CHECK(edge.at("edge") == "left.UMax");
    CHECK(edge.at("touching") == json::array({"right.UMin"}));
    REQUIRE(edge.at("links").size() == 1);
    CHECK(edge.at("links")[0].at("edge") == "right.UMin");
    CHECK(edge.at("links")[0].at("continuity") == "G2");
    CHECK(edge.at("on_centre_plane") == false);

    pick("UMin");
    CHECK(ok(server, "get_selection").at("on_centre_plane") == true);

    pick(nullptr);
    const json patch = ok(server, "get_selection");
    CHECK(patch.at("patch") == "left");
    CHECK(patch.at("edge").is_null());

    std::ofstream(dir.path / "selection.json")
        << json{{"scene", dir.file("other.ada.json")}, {"patch", "left"}, {"edge", nullptr}}.dump();
    CHECK(fails(server, "get_selection").at("code") == "stale_selection");
}

TEST_CASE("the L2 hood case can be built from scratch through tool calls alone",
          "[mcp][edit][exit]") {
    const std::filesystem::path authored_path =
        std::filesystem::path(ADA_SOURCE_DIR) / "cases" / "L2-hood-2x2-sym.ada.json";
    std::ifstream stream(authored_path);
    const json authored = json::parse(stream);

    const TempDir dir("hood");
    McpServer server({});
    static_cast<void>(server.handle({{"jsonrpc", "2.0"},
                                     {"id", 1},
                                     {"method", "initialize"},
                                     {"params", {{"protocolVersion", "2025-06-18"}}}}));
    ok(server,
       "scene_new",
       {{"path", dir.file("hood.ada.json")},
        {"description", authored.at("description")},
        {"budgets", authored.at("budgets")}});

    json curves = json::array();
    for (const json& c : authored.at("curves")) {
        curves.push_back(
            {{"id", c.at("id")}, {"points", c.at("through")}, {"degree", c.at("degree")}});
    }
    ok(server, "curve_from_points", {{"curves", curves}});

    // Row lines (constant v) hold the hood_u curves, column lines the hood_v.
    json along_u = json::array();
    json along_v = json::array();
    for (int k = 0; k < 3; ++k) {
        along_u.push_back({fmt_line("hood_u", k, 0), fmt_line("hood_u", k, 1)});
        along_v.push_back({fmt_line("hood_v", k, 0), fmt_line("hood_v", k, 1)});
    }
    ok(server,
       "network_from_curves",
       {{"prefix", "hood"}, {"along_u", along_u}, {"along_v", along_v}, {"continuity", "G2"}});
    ok(server, "symmetry_constrain", {{"edge", "hood_0_0.UMin"}, {"continuity", "G2"}});
    const json last =
        ok(server, "symmetry_constrain", {{"edge", "hood_1_0.UMin"}, {"continuity", "G2"}});
    CHECK(last.at("quality").at("pass") == true);

    // The same document as the authored case, and the same surfaces as
    // solving that case directly.
    ada::Scene expected = ada::read_scene(authored_path);
    ada::solve_scene(expected);
    const ada::Scene built = ada::read_scene(dir.file("hood.ada.json"));
    json a = ada::scene_to_json(expected);
    json b = ada::scene_to_json(built);
    for (json* doc : {&a, &b}) {
        for (json& p : doc->at("patches")) {
            p.erase("control_points");
            p.erase("degree");
        }
    }
    CHECK(a == b);
    REQUIRE(built.patches.size() == expected.patches.size());
    double worst = 0.0;
    for (std::size_t k = 0; k < built.patches.size(); ++k) {
        const auto& p = built.patches[k].patch.control_points();
        const auto& q = expected.patches[k].patch.control_points();
        REQUIRE(p.size() == q.size());
        for (std::size_t i = 0; i < p.size(); ++i) {
            worst = std::max(worst, (p[i] - q[i]).norm());
        }
    }
    CHECK(worst < kSameMm);
}
