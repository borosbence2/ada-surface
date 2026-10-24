#include "server.hpp"

#include "ada/build_info.hpp"

#include "tools.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace ada::mcp {

namespace {

// JSON-RPC 2.0 error codes.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;

// The first protocol revision with `structuredContent` in tool results.
constexpr const char* kStructuredContentSince = "2025-06-18";

constexpr const char* kInstructions =
    "ADA Surface kernel for Class-A surfacing. Open a scene with scene_open, inspect it with "
    "scene_summary, and run quality_report to check it against its budgets (continuity across "
    "linked edges, symmetry at Y = 0, highlight lines, control-net shape). solve turns the "
    "scene's recipes into control points and writes them back; geometry is never edited by "
    "writing control points directly.";

nlohmann::json error_response(const nlohmann::json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

nlohmann::json result_response(const nlohmann::json& id, nlohmann::json result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

/// An error the dispatcher turns into a JSON-RPC error response.
struct RpcError {
    int code;
    std::string message;
};

nlohmann::json empty_schema() {
    return {{"type", "object"}, {"properties", nlohmann::json::object()}};
}

} // namespace

void SceneSession::open(const std::filesystem::path& path) {
    std::optional<std::filesystem::path> previous = path_;
    path_ = std::filesystem::absolute(path);
    contents_.clear();
    scene_.reset();
    try {
        static_cast<void>(current());
    } catch (...) {
        path_ = previous;
        contents_.clear();
        scene_.reset();
        throw;
    }
}

const std::filesystem::path& SceneSession::path() const {
    if (!path_.has_value()) {
        throw ToolError("no scene is open; call scene_open with the path of a .ada.json file");
    }
    return *path_;
}

const Scene& SceneSession::current() {
    const std::filesystem::path& file = path();
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        throw ToolError(fmt::format("cannot read {}", file.string()));
    }
    std::string contents((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
    if (!scene_.has_value() || contents != contents_) {
        try {
            scene_ = scene_from_json(nlohmann::json::parse(contents));
        } catch (const std::exception& e) {
            scene_.reset();
            contents_.clear();
            throw ToolError(fmt::format("{}: {}", file.string(), e.what()));
        }
        contents_ = std::move(contents);
    }
    return *scene_;
}

McpServer::McpServer(ServerOptions options)
    : options_(std::move(options)),
      protocol_version_(kProtocolVersions.front()) {
    register_tools();
    if (options_.scene.has_value()) {
        session_.open(*options_.scene);
    }
}

void McpServer::register_tools() {
    tools_.push_back(
        {"scene_open",
         "Open a scene file (.ada.json) and make it the scene the other tools act on. "
         "Returns its summary.",
         {{"type", "object"},
          {"properties",
           {{"path", {{"type", "string"}, {"description", "Path to the scene file"}}}}},
          {"required", {"path"}}},
         false,
         true,
         [this](const nlohmann::json& args) { return scene_open(session_, args); }});
    tools_.push_back({"scene_summary",
                      "List the open scene's patches (degrees, control-point counts, bounding "
                      "boxes), linked edges with their required continuity, centre-plane "
                      "symmetry links and budgets.",
                      empty_schema(),
                      false,
                      true,
                      [this](const nlohmann::json&) { return scene_summary(session_); }});
    tools_.push_back({"quality_report",
                      "Check the open scene against its Class-A budgets: G0/G1/G2 across linked "
                      "edges, Y=0 symmetry, highlight-line inflections, control-net zig-zags, "
                      "curvature. Lists every failed check with its value and budget.",
                      empty_schema(),
                      false,
                      true,
                      [this](const nlohmann::json&) { return quality_report(session_); }});
    tools_.push_back(
        {"solve",
         "Solve the open scene: turn its recipes (from_boundaries, blend) into control "
         "points under its edge links, symmetry and pins, write them back to the scene "
         "file, and run the quality report. Reports how far any boundary curve had to "
         "move to meet the continuity asked for.",
         empty_schema(),
         false,
         false,
         [this](const nlohmann::json&) { return solve(session_); }});
    tools_.push_back({"tessellate",
                      "Studio: triangle meshes of every patch with normals, (u, v), Gaussian and "
                      "mean curvature and the highlight parameter per vertex.",
                      {{"type", "object"},
                       {"properties",
                        {{"grid",
                          {{"type", "integer"},
                           {"minimum", 2},
                           {"maximum", 257},
                           {"description", "Vertices per patch side (default 49)"}}}}}},
                      true,
                      true,
                      [this](const nlohmann::json& args) { return tessellate(session_, args); }});
    tools_.push_back({"cv_hulls",
                      "Studio: every patch's control net.",
                      empty_schema(),
                      true,
                      true,
                      [this](const nlohmann::json&) { return cv_hulls(session_); }});
    tools_.push_back(
        {"boundary_combs",
         "Studio: curvature combs along every patch boundary.",
         {{"type", "object"},
          {"properties",
           {{"samples",
             {{"type", "integer"},
              {"minimum", 2},
              {"maximum", 513},
              {"description", "Samples per edge (default 33)"}}}}}},
         true,
         true,
         [this](const nlohmann::json& args) { return boundary_combs(session_, args); }});
}

std::optional<std::string> McpServer::handle_line(const std::string& line) {
    nlohmann::json message;
    try {
        message = nlohmann::json::parse(line);
    } catch (const nlohmann::json::parse_error& e) {
        return error_response(nullptr, kParseError, fmt::format("Parse error: {}", e.what()))
            .dump();
    }
    const std::optional<nlohmann::json> response = handle(message);
    return response.has_value() ? std::optional<std::string>(response->dump()) : std::nullopt;
}

std::optional<nlohmann::json> McpServer::handle(const nlohmann::json& message) {
    if (!message.is_object()) {
        return error_response(
            nullptr, kInvalidRequest, "a message must be a JSON object; batches are not supported");
    }
    if (!message.contains("method")) {
        // A response to a request we never send, or garbage; nothing to answer.
        return std::nullopt;
    }
    const bool is_request = message.contains("id");
    const nlohmann::json id = is_request ? message.at("id") : nlohmann::json(nullptr);
    if (message.value("jsonrpc", "") != "2.0" || !message.at("method").is_string()) {
        return is_request ? std::optional(
                                error_response(id, kInvalidRequest, "not a JSON-RPC 2.0 request"))
                          : std::nullopt;
    }
    if (!is_request) {
        return std::nullopt; // notifications/initialized, notifications/cancelled, ...
    }

    const std::string method = message.at("method").get<std::string>();
    const nlohmann::json params = message.value("params", nlohmann::json::object());
    try {
        if (method == "initialize") {
            return result_response(id, initialize(params));
        }
        if (method == "ping") {
            return result_response(id, nlohmann::json::object());
        }
        if (method == "tools/list") {
            return result_response(id, list_tools());
        }
        if (method == "tools/call") {
            return result_response(id, call_tool(params));
        }
    } catch (const RpcError& e) {
        return error_response(id, e.code, e.message);
    }
    return error_response(id, kMethodNotFound, fmt::format("Method not found: {}", method));
}

nlohmann::json McpServer::initialize(const nlohmann::json& params) {
    const std::string requested = params.value("protocolVersion", "");
    const bool known = std::find(kProtocolVersions.begin(), kProtocolVersions.end(), requested) !=
                       kProtocolVersions.end();
    protocol_version_ = known ? requested : kProtocolVersions.front();
    return {
        {"protocolVersion", protocol_version_},
        {"capabilities", {{"tools", {{"listChanged", false}}}}},
        {"serverInfo",
         {{"name", "ada-mcp"},
          {"title", "ADA Surface kernel"},
          {"version", std::string{build_info().version}}}},
        {"instructions", kInstructions},
    };
}

nlohmann::json McpServer::list_tools() const {
    nlohmann::json tools = nlohmann::json::array();
    for (const Tool& tool : tools_) {
        if (tool.ui_only && !options_.ui_tools) {
            continue;
        }
        tools.push_back({{"name", tool.name},
                         {"description", tool.description},
                         {"inputSchema", tool.input_schema},
                         {"annotations",
                          {{"readOnlyHint", tool.read_only},
                           {"idempotentHint", true},
                           {"openWorldHint", false}}}});
    }
    return {{"tools", std::move(tools)}};
}

bool McpServer::structured_content() const {
    // Revisions are ISO dates, so they compare as strings.
    return protocol_version_ >= kStructuredContentSince;
}

nlohmann::json McpServer::call_tool(const nlohmann::json& params) {
    if (!params.is_object() || !params.contains("name") || !params.at("name").is_string()) {
        throw RpcError{kInvalidParams, "tools/call needs a tool name"};
    }
    const std::string name = params.at("name").get<std::string>();
    const auto tool = std::find_if(tools_.begin(), tools_.end(), [&](const Tool& t) {
        return t.name == name && (!t.ui_only || options_.ui_tools);
    });
    if (tool == tools_.end()) {
        throw RpcError{kInvalidParams, fmt::format("Unknown tool: {}", name)};
    }
    const nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

    ToolResult result;
    try {
        if (!arguments.is_object()) {
            throw ToolError("arguments must be a JSON object");
        }
        result = tool->run(arguments);
    } catch (const std::exception& e) {
        result = ToolResult{e.what(), nullptr, true};
    }

    nlohmann::json out = {{"isError", result.is_error}};
    if (structured_content() && result.structured.is_object()) {
        out["structuredContent"] = result.structured;
        out["content"] = nlohmann::json::array({{{"type", "text"}, {"text", result.text}}});
    } else {
        // Older clients get the data as JSON text, which is what the spec
        // asks servers to do alongside structured content anyway.
        std::string text = result.text;
        if (result.structured.is_object()) {
            text += "\n" + result.structured.dump();
        }
        out["content"] = nlohmann::json::array({{{"type", "text"}, {"text", text}}});
    }
    return out;
}

} // namespace ada::mcp
