#include "ada/doc/scene.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "base64.hpp"
#include "server.hpp"
#include "support/panels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

using ada::mcp::McpServer;
using ada::mcp::ServerOptions;
using nlohmann::json;

namespace {

std::filesystem::path example(const char* name) {
    return std::filesystem::path(ADA_SOURCE_DIR) / "cases" / "examples" / name;
}

json request(int id, const std::string& method, json params = json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
}

json call(McpServer& server, const std::string& tool, json arguments = json::object()) {
    const auto response = server.handle(
        request(7, "tools/call", {{"name", tool}, {"arguments", std::move(arguments)}}));
    REQUIRE(response.has_value());
    REQUIRE(response->contains("result"));
    return response->at("result");
}

McpServer initialised(ServerOptions options, const std::string& version = "2025-06-18") {
    McpServer server(std::move(options));
    static_cast<void>(server.handle(request(1, "initialize", {{"protocolVersion", version}})));
    return server;
}

/// Decodes a float32 payload and widens it explicitly, so the comparisons
/// below run in double without implicit float promotion.
std::vector<double> floats(const json& base64) {
    const std::vector<std::byte> bytes = ada::mcp::base64_decode(base64.get<std::string>());
    std::vector<float> narrow(bytes.size() / sizeof(float));
    std::memcpy(narrow.data(), bytes.data(), narrow.size() * sizeof(float));
    std::vector<double> out;
    out.reserve(narrow.size());
    for (const float f : narrow) {
        out.push_back(static_cast<double>(f));
    }
    return out;
}

std::vector<std::uint32_t> uints(const json& base64) {
    const std::vector<std::byte> bytes = ada::mcp::base64_decode(base64.get<std::string>());
    std::vector<std::uint32_t> out(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(out.data(), bytes.data(), out.size() * sizeof(std::uint32_t));
    return out;
}

std::vector<std::string> tool_names(McpServer& server) {
    // Keep the response alive: a range-for over a reference into the temporary
    // optional would dangle (lifetime extension only reaches it in C++23).
    const json response = *server.handle(request(2, "tools/list"));
    std::vector<std::string> names;
    for (const json& tool : response.at("result").at("tools")) {
        names.push_back(tool.at("name").get<std::string>());
    }
    return names;
}

bool contains(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

TEST_CASE("base64 matches RFC 4648 test vectors", "[mcp][base64]") {
    auto encode = [](const std::string& s) {
        return ada::mcp::base64_encode(std::as_bytes(std::span<const char>(s.data(), s.size())));
    };
    CHECK(encode("") == "");
    CHECK(encode("f") == "Zg==");
    CHECK(encode("fo") == "Zm8=");
    CHECK(encode("foo") == "Zm9v");
    CHECK(encode("foobar") == "Zm9vYmFy");
    const std::vector<std::byte> back = ada::mcp::base64_decode("Zm9vYmE=");
    CHECK(std::string(reinterpret_cast<const char*>(back.data()), back.size()) == "fooba");
    CHECK_THROWS_AS(ada::mcp::base64_decode("Zm9"), std::invalid_argument);
}

TEST_CASE("initialize negotiates the protocol revision", "[mcp][protocol]") {
    McpServer server({});
    const json known =
        server.handle(request(1, "initialize", {{"protocolVersion", "2025-03-26"}}))->at("result");
    CHECK(known.at("protocolVersion") == "2025-03-26");
    CHECK(known.at("capabilities").contains("tools"));
    CHECK(known.at("serverInfo").at("name") == "ada-mcp");

    const json unknown =
        server.handle(request(2, "initialize", {{"protocolVersion", "1999-01-01"}}))->at("result");
    CHECK(unknown.at("protocolVersion") == ada::mcp::kProtocolVersions.front());
}

TEST_CASE("JSON-RPC framing errors get the specified codes", "[mcp][protocol]") {
    McpServer server({});
    const json parse_error = json::parse(*server.handle_line("{not json"));
    CHECK(parse_error.at("error").at("code") == -32700);
    CHECK(parse_error.at("id").is_null());

    CHECK(server.handle(request(3, "resources/list"))->at("error").at("code") == -32601);
    CHECK(server.handle(json::array())->at("error").at("code") == -32600);
    CHECK(server.handle(request(4, "tools/call", {{"name", "no_such_tool"}}))
              ->at("error")
              .at("code") == -32602);

    // Notifications never get a response, known or not.
    CHECK_FALSE(
        server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}).has_value());
    CHECK_FALSE(
        server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/whatever"}}).has_value());

    CHECK(server.handle(request(5, "ping"))->at("result") == json::object());
}

TEST_CASE("agents do not see Studio's tools", "[mcp][protocol]") {
    McpServer agent({});
    const std::vector<std::string> plain = tool_names(agent);
    CHECK(contains(plain, "quality_report"));
    CHECK_FALSE(contains(plain, "tessellate"));
    CHECK(
        agent.handle(request(6, "tools/call", {{"name", "tessellate"}}))->at("error").at("code") ==
        -32602);

    ServerOptions ui;
    ui.ui_tools = true;
    McpServer studio(ui);
    CHECK(contains(tool_names(studio), "tessellate"));
}

TEST_CASE("quality_report checks the open scene", "[mcp][tools]") {
    McpServer server = initialised({});

    const json before = call(server, "quality_report");
    CHECK(before.at("isError") == true);
    CHECK(before.at("content")[0].at("text").get<std::string>().find("scene_open") !=
          std::string::npos);

    CHECK(call(server, "scene_open", {{"path", example("hood.ada.json").string()}}).at("isError") ==
          false);
    const json good = call(server, "quality_report");
    CHECK(good.at("isError") == false);
    CHECK(good.at("structuredContent").at("pass") == true);

    call(server, "scene_open", {{"path", example("hood-g2-broken.ada.json").string()}});
    const json broken = call(server, "quality_report");
    CHECK(broken.at("structuredContent").at("pass") == false);
    CHECK(broken.at("content")[0].at("text").get<std::string>().find("g2_relative") !=
          std::string::npos);
}

TEST_CASE("a failed scene_open keeps the previous scene", "[mcp][tools]") {
    McpServer server = initialised({});
    call(server, "scene_open", {{"path", example("hood.ada.json").string()}});
    const json missing = call(server, "scene_open", {{"path", "no/such/scene.ada.json"}});
    CHECK(missing.at("isError") == true);
    CHECK(call(server, "quality_report").at("structuredContent").at("pass") == true);
}

TEST_CASE("older clients get the data as text", "[mcp][tools]") {
    ServerOptions options;
    options.scene = example("hood.ada.json");
    McpServer server = initialised(options, "2025-03-26");
    const json result = call(server, "scene_summary");
    CHECK_FALSE(result.contains("structuredContent"));
    CHECK(result.at("content")[0].at("text").get<std::string>().find("\"patches\"") !=
          std::string::npos);
}

TEST_CASE("edits to the scene file are picked up on the next call", "[mcp][tools]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ada-mcp-test-reload.ada.json";
    ada::write_scene(ada::test::hood_scene(), path);
    ServerOptions options;
    options.scene = path;
    McpServer server = initialised(options);
    CHECK(call(server, "quality_report").at("structuredContent").at("pass") == true);

    ada::write_scene(ada::test::hood_scene_g2_broken(), path);
    CHECK(call(server, "quality_report").at("structuredContent").at("pass") == false);
    std::filesystem::remove(path);
}

TEST_CASE("solve turns an authored case into geometry and writes it back", "[mcp][tools]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ada-mcp-test-solve.ada.json";
    std::filesystem::copy_file(std::filesystem::path(ADA_SOURCE_DIR) / "cases" /
                                   "L2-hood-2x2-sym.ada.json",
                               path,
                               std::filesystem::copy_options::overwrite_existing);
    ServerOptions options;
    options.scene = path;
    McpServer server = initialised(options);

    // Authored files hold recipes only: unsolved until solve runs.
    CHECK(call(server, "quality_report").at("structuredContent").at("pass") == false);
    CHECK(call(server, "scene_summary").at("structuredContent").at("patches")[0].at("solved") ==
          false);

    const json solved = call(server, "solve");
    REQUIRE(solved.at("isError") == false);
    CHECK(solved.at("structuredContent").at("report").at("pass") == true);
    CHECK(solved.at("structuredContent").at("curves").size() == 12);

    CHECK(call(server, "quality_report").at("structuredContent").at("pass") == true);
    CHECK(ada::read_scene(path).patches[0].solved);

    const json response = *server.handle(request(9, "tools/list"));
    for (const json& tool : response.at("result").at("tools")) {
        if (tool.at("name") == "solve") {
            CHECK(tool.at("annotations").at("readOnlyHint") == false);
        }
    }
    std::filesystem::remove(path);
}

TEST_CASE("export_iges writes next to the scene and refuses unsolved ones", "[mcp][tools]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ada-mcp-test-export";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::filesystem::copy_file(std::filesystem::path(ADA_SOURCE_DIR) / "cases" /
                                   "L2-hood-2x2-sym.ada.json",
                               dir / "hood.ada.json");
    ServerOptions options;
    options.scene = dir / "hood.ada.json";
    McpServer server = initialised(options);

    const json refused = call(server, "export_iges", {{"path", "hood.igs"}});
    CHECK(refused.at("isError") == true);
    CHECK(refused.at("content")[0].at("text").get<std::string>().find("unsolved") !=
          std::string::npos);

    call(server, "solve");
    const json exported = call(server, "export_iges", {{"path", "hood.igs"}});
    REQUIRE(exported.at("isError") == false);
    CHECK(exported.at("structuredContent").at("patches") == 4);
    REQUIRE(std::filesystem::exists(dir / "hood.igs"));
    std::ifstream file(dir / "hood.igs");
    std::string first;
    std::getline(file, first);
    CHECK(first.size() == 80);
    CHECK(first[72] == 'S');
    file.close();
    std::filesystem::remove_all(dir);
}

TEST_CASE("tessellate returns meshes that lie on the patches", "[mcp][tools]") {
    ServerOptions options;
    options.ui_tools = true;
    options.scene = example("hood.ada.json");
    McpServer server = initialised(options);

    constexpr int kGrid = 9;
    const json result = call(server, "tessellate", {{"grid", kGrid}}).at("structuredContent");
    const ada::Scene scene = ada::read_scene(example("hood.ada.json"));
    REQUIRE(result.at("patches").size() == scene.patches.size());

    const json& front = result.at("patches")[0];
    const std::vector<double> positions = floats(front.at("positions"));
    const std::vector<double> uvs = floats(front.at("uvs"));
    const std::vector<std::uint32_t> indices = uints(front.at("indices"));
    REQUIRE(positions.size() == 3 * kGrid * kGrid);
    REQUIRE(uvs.size() == 2 * kGrid * kGrid);
    CHECK(indices.size() == 6 * (kGrid - 1) * (kGrid - 1));
    CHECK(floats(front.at("mean")).size() == kGrid * kGrid);
    CHECK(floats(front.at("highlight")).size() == kGrid * kGrid);

    // float32 on a ~1 m part: a few hundredths of a millimetre at most.
    const ada::PatchEvaluator patch(scene.patches[0].patch);
    for (std::size_t k = 0; k < static_cast<std::size_t>(kGrid * kGrid); k += 7) {
        const ada::Vec3 expected = patch.evaluate(uvs[2 * k], uvs[2 * k + 1]);
        const ada::Vec3 actual(positions[3 * k], positions[3 * k + 1], positions[3 * k + 2]);
        CHECK((actual - expected).norm() < 1e-3);
    }

    CHECK(call(server, "tessellate", {{"grid", 1}}).at("isError") == true);
}

TEST_CASE("boundary combs follow the boundary curvature", "[mcp][tools]") {
    ServerOptions options;
    options.ui_tools = true;
    options.scene = example("hood.ada.json");
    McpServer server = initialised(options);
    const json result = call(server, "boundary_combs", {{"samples", 5}}).at("structuredContent");
    const json& edges = result.at("patches")[0].at("edges");
    REQUIRE(edges.size() == 4);
    for (const json& edge : edges) {
        CHECK(edge.at("points").size() == 15);
        CHECK(edge.at("directions").size() == 15);
        CHECK(edge.at("curvature").size() == 5);
    }
    // The front patch's UMin edge (on the centre line) is a curved crown line;
    // its curvature is not zero.
    double max_k = 0.0;
    for (const json& k : edges[0].at("curvature")) {
        max_k = std::max(max_k, k.get<double>());
    }
    CHECK(max_k > 1e-5);
}
