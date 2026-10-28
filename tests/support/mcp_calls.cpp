#include "support/mcp_calls.hpp"

#include <catch2/catch_test_macros.hpp>

#include <utility>

namespace ada::test {

using nlohmann::json;

TempDir::TempDir(const std::string& name)
    : path(std::filesystem::temp_directory_path() / ("ada-mcp-" + name)) {
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
}

TempDir::~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

json call(mcp::McpServer& server, const std::string& tool, json arguments) {
    const auto response =
        server.handle({{"jsonrpc", "2.0"},
                       {"id", 7},
                       {"method", "tools/call"},
                       {"params", {{"name", tool}, {"arguments", std::move(arguments)}}}});
    REQUIRE(response.has_value());
    REQUIRE(response->contains("result"));
    return response->at("result");
}

json ok(mcp::McpServer& server, const std::string& tool, json arguments) {
    const json result = call(server, tool, std::move(arguments));
    INFO(tool << ": " << result.at("content")[0].at("text").get<std::string>());
    REQUIRE(result.at("isError") == false);
    return result.at("structuredContent");
}

json fails(mcp::McpServer& server, const std::string& tool, json arguments) {
    const json result = call(server, tool, std::move(arguments));
    INFO(tool << ": " << result.at("content")[0].at("text").get<std::string>());
    REQUIRE(result.at("isError") == true);
    return result.at("structuredContent").at("error");
}

mcp::McpServer started_server() {
    mcp::McpServer server({});
    static_cast<void>(server.handle({{"jsonrpc", "2.0"},
                                     {"id", 1},
                                     {"method", "initialize"},
                                     {"params", {{"protocolVersion", "2025-06-18"}}}}));
    return server;
}

mcp::McpServer server_with_new_scene(const TempDir& dir) {
    mcp::McpServer server = started_server();
    ok(server, "scene_new", {{"path", dir.file("test.ada.json")}});
    return server;
}

} // namespace ada::test
