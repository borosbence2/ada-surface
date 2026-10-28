#pragma once

// Calling ada-mcp's tools in-process, as an agent would over stdio, for the
// MCP tests.

#include "server.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace ada::test {

/// A fresh, empty directory for one test, removed again when it ends.
struct TempDir {
    std::filesystem::path path;

    explicit TempDir(const std::string& name);
    ~TempDir();

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string file(const std::string& name) const { return (path / name).generic_string(); }
};

/// The tools/call result for one call.
nlohmann::json call(mcp::McpServer& server,
                    const std::string& tool,
                    nlohmann::json arguments = nlohmann::json::object());

/// Calls a tool that must succeed and returns its structured content.
nlohmann::json ok(mcp::McpServer& server,
                  const std::string& tool,
                  nlohmann::json arguments = nlohmann::json::object());

/// Calls a tool that must fail and returns its structured error.
nlohmann::json fails(mcp::McpServer& server,
                     const std::string& tool,
                     nlohmann::json arguments = nlohmann::json::object());

/// An initialised server.
mcp::McpServer started_server();

/// An initialised server with a new, empty scene `dir`/test.ada.json open.
mcp::McpServer server_with_new_scene(const TempDir& dir);

} // namespace ada::test
