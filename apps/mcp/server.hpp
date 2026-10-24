#pragma once

// The MCP server: JSON-RPC 2.0 messages in, responses out. Transport-free so
// that tests drive it directly; main.cpp wraps it in the newline-delimited
// stdio transport MCP specifies.

#include "ada/doc/scene.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ada::mcp {

/// Protocol revisions this server speaks, newest first. A client asking for
/// one of these gets it; any other request is answered with the newest, and
/// the client decides whether it can work with that.
inline const std::vector<std::string> kProtocolVersions{
    "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};

struct ServerOptions {
    /// Also offer the tools only Studio uses (tessellation, CV hulls, combs).
    /// Agents do not get them: they would only fill the context window.
    bool ui_tools = false;
    std::optional<std::filesystem::path> scene;
};

/// What a tool hands back: text for any client, and the same data as a JSON
/// object for clients that negotiated structured content.
struct ToolResult {
    std::string text;
    nlohmann::json structured = nullptr;
    bool is_error = false;
};

/// A tool's failure the caller can act on (bad arguments, no scene open). It
/// becomes a tool result with `isError`, not a JSON-RPC error, so an agent
/// sees the message and can correct itself.
struct ToolError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// The open scene. Re-reads the file whenever its contents change, so edits
/// from an editor, another process or Studio are picked up on the next call.
class SceneSession {
public:
    void open(const std::filesystem::path& path);

    bool is_open() const noexcept { return path_.has_value(); }

    /// Throws `ToolError` when no scene is open or the file no longer parses.
    const Scene& current();

    const std::filesystem::path& path() const;

private:
    std::optional<std::filesystem::path> path_;
    std::string contents_;
    std::optional<Scene> scene_;
};

class McpServer {
public:
    explicit McpServer(ServerOptions options);

    /// One JSON-RPC message in; the response out, or nullopt for a
    /// notification (which never gets one).
    std::optional<nlohmann::json> handle(const nlohmann::json& message);

    /// One line of the stdio transport; parse errors get a JSON-RPC error.
    std::optional<std::string> handle_line(const std::string& line);

private:
    struct Tool {
        std::string name;
        std::string description;
        nlohmann::json input_schema;
        bool ui_only;
        bool read_only;
        std::function<ToolResult(const nlohmann::json& arguments)> run;
    };

    nlohmann::json initialize(const nlohmann::json& params);
    nlohmann::json list_tools() const;
    nlohmann::json call_tool(const nlohmann::json& params);
    bool structured_content() const;
    void register_tools();

    ServerOptions options_;
    SceneSession session_;
    std::string protocol_version_;
    std::vector<Tool> tools_;
};

} // namespace ada::mcp
