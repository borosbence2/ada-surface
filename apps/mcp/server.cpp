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
    "ADA Surface: an exact Bezier surface kernel for Class-A surfacing. You describe surfaces "
    "with curves through points and recipes; the kernel solves for the control points (no tool "
    "sets them) and checks the result against the scene's quality budgets.\n"
    "Workflow: scene_new or scene_open; curve_from_points for the feature lines; "
    "patch_from_boundaries, network_from_curves or patch_blend for the surfaces; "
    "edge_constrain and symmetry_constrain for continuity; then read the quality summary every "
    "edit returns (quality_report for the full report) and adjust: refit curves (replace: "
    "true), patch_set_degree, fairness_set. export_iges writes the result for CAD.\n"
    "Every edit solves the scene and writes the file, which ADA Studio shows live. An edit that "
    "cannot be solved is refused with an error code, the reason and what to try, and changes "
    "nothing. undo and redo step through edits. get_selection returns what the user picked in "
    "ADA Studio, for requests like 'make this edge G2'.\n"
    "Conventions: millimetres, Z up, Y = 0 is the vehicle centre plane. A patch's edges are "
    "UMin and UMax (lines of constant u, running along v) and VMin and VMax (constant v); "
    "write an edge as 'patch.Edge', e.g. 'hood_0_0.UMax'. Continuity is G0 (position), G1 "
    "(tangent plane) or G2 (curvature).";

constexpr const char* kEdgePattern = "^.+\\.(UMin|UMax|VMin|VMax)$";

nlohmann::json edge_schema(const char* description) {
    return {{"type", "string"}, {"pattern", kEdgePattern}, {"description", description}};
}

nlohmann::json continuity_schema(bool with_none, const char* description) {
    nlohmann::json values = {"G0", "G1", "G2"};
    if (with_none) {
        values.push_back("none");
    }
    return {{"type", "string"}, {"enum", values}, {"description", description}};
}

nlohmann::json degree_schema(const char* description) {
    return {{"type", "integer"},
            {"minimum", 1},
            {"maximum", kMaxRecipeDegree},
            {"description", description}};
}

nlohmann::json object_schema(nlohmann::json properties, std::vector<std::string> required = {}) {
    nlohmann::json schema = {{"type", "object"}, {"properties", std::move(properties)}};
    if (!required.empty()) {
        schema["required"] = std::move(required);
    }
    return schema;
}

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

ToolResult error_result(const ToolError& e) {
    std::string text = e.what();
    if (!e.what_to_try.empty()) {
        text += "\ntry: " + e.what_to_try;
    }
    nlohmann::json error = {{"code", e.code}, {"message", e.what()}};
    if (!e.what_to_try.empty()) {
        error["try"] = e.what_to_try;
    }
    if (!e.details.is_null()) {
        error["details"] = e.details;
    }
    return {std::move(text), {{"error", std::move(error)}}, true};
}

} // namespace

void SceneSession::open(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw ToolError(fmt::format("there is no file {}", path.generic_string()),
                        "no_such_file",
                        "check the path, or make a new scene with scene_new");
    }
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
        throw ToolError("no scene is open",
                        "no_scene",
                        "call scene_open with the path of a .ada.json file, or scene_new");
    }
    return *path_;
}

const Scene& SceneSession::current() {
    const std::filesystem::path& file = path();
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        throw ToolError(fmt::format("cannot read {}", file.string()), "unreadable_scene");
    }
    std::string contents((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
    if (!scene_.has_value() || contents != contents_) {
        try {
            scene_ = scene_from_json(nlohmann::json::parse(contents));
        } catch (const std::exception& e) {
            scene_.reset();
            contents_.clear();
            throw ToolError(fmt::format("{}: {}", file.string(), e.what()),
                            "invalid_scene",
                            "fix the file, or undo the edit that broke it");
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

void McpServer::add_tool(std::string name,
                         std::string description,
                         nlohmann::json input_schema,
                         Kind kind,
                         bool idempotent,
                         std::function<ToolResult(const nlohmann::json&)> run) {
    tools_.push_back({std::move(name),
                      std::move(description),
                      std::move(input_schema),
                      kind,
                      idempotent,
                      std::move(run)});
}

void McpServer::register_tools() {
    // Reading.
    add_tool(
        "scene_open",
        "Open a scene file (.ada.json) and make it the scene the other tools act on. "
        "Returns its summary.",
        object_schema({{"path", {{"type", "string"}, {"description", "Path to the scene file"}}}},
                      {"path"}),
        Kind::Read,
        true,
        [this](const nlohmann::json& args) {
            ToolResult result = scene_open(session_, args);
            history_ = History{};
            return result;
        });
    add_tool("scene_summary",
             "List the open scene's curves (degree, end points), patches (recipe, degree, "
             "bounding box), linked edges with their required continuity, centre-plane symmetry "
             "links and budgets.",
             empty_schema(),
             Kind::Read,
             true,
             [this](const nlohmann::json&) { return scene_summary(session_); });
    add_tool("quality_report",
             "Check the open scene against its Class-A budgets: G0/G1/G2 across linked edges, Y=0 "
             "symmetry, highlight-line inflections, control-net zig-zags, curvature. Lists every "
             "failed check with its value and budget.",
             empty_schema(),
             Kind::Read,
             true,
             [this](const nlohmann::json&) { return quality_report(session_); });
    add_tool("get_selection",
             "What the user picked in ADA Studio: a patch (with the point and (u, v) clicked) or "
             "a patch edge. For an edge it also lists its links, its symmetry link, the edges of "
             "other patches lying along it and whether it lies in Y = 0, so 'make this edge G2' "
             "becomes an edge_constrain call.",
             empty_schema(),
             Kind::Read,
             true,
             [this](const nlohmann::json&) { return get_selection(session_); });

    // Editing. Each solves the scene and writes it back, or changes nothing.
    add_tool(
        "scene_new",
        "Create an empty scene file (and its folder) and open it. A relative path is taken "
        "relative to the "
        "open scene's directory, or the working directory when none is open.",
        object_schema(
            {{"path", {{"type", "string"}, {"description", "New file, ending in .ada.json"}}},
             {"description",
              {{"type", "string"}, {"description", "What the scene is, in a sentence"}}},
             {"budgets",
              {{"type", "object"},
               {"description",
                "Quality budgets to override, e.g. {\"max_degree\": 3, "
                "\"allow_curvature_reversal\": false, \"max_highlight_inflections\": 1}"}}},
             {"overwrite",
              {{"type", "boolean"}, {"description", "Replace an existing file (default false)"}}}},
            {"path"}),
        Kind::Write,
        false,
        [this](const nlohmann::json& args) {
            ToolResult result = scene_new(session_, args);
            history_ = History{};
            return result;
        });
    add_tool(
        "curve_from_points",
        "Add curves, each through points the way a designer gives a feature line: with exactly "
        "degree + 1 points the curve passes through all of them; with more, it is a least-squares "
        "fit through the first and last point (the result says how far it passes from the "
        "others). Curves are what patches are built on; curves that share a corner must share "
        "that end point. Several curves in one call change together, e.g. to move a point where "
        "curves meet.",
        object_schema(
            {{"curves",
              {{"type", "array"},
               {"minItems", 1},
               {"items",
                object_schema(
                    {{"id", {{"type", "string"}, {"description", "Letters, digits, _ and -"}}},
                     {"points",
                      {{"type", "array"},
                       {"minItems", 2},
                       {"items",
                        {{"type", "array"},
                         {"items", {{"type", "number"}}},
                         {"minItems", 3},
                         {"maxItems", 3}}},
                       {"description", "[x, y, z] in millimetres, in order along the curve"}}},
                     {"degree", degree_schema("Curve degree (default 3)")}},
                    {"id", "points"})}}},
             {"replace",
              {{"type", "boolean"},
               {"description", "Change curves that already exist (default false)"}}}},
            {"curves"}),
        Kind::Write,
        false,
        [this](const nlohmann::json& args) { return curve_from_points(session_, history_, args); });
    add_tool("patch_from_boundaries",
             "Add a patch bounded by four curves: UMin opposite UMax, VMin opposite VMax, each "
             "meeting its two neighbours at a corner (curve directions do not matter). The "
             "interior is the fairest surface the constraints allow.",
             object_schema({{"id", {{"type", "string"}, {"description", "New patch id"}}},
                            {"UMin", {{"type", "string"}, {"description", "Curve id"}}},
                            {"UMax", {{"type", "string"}, {"description", "Curve id"}}},
                            {"VMin", {{"type", "string"}, {"description", "Curve id"}}},
                            {"VMax", {{"type", "string"}, {"description", "Curve id"}}},
                            {"replace",
                             {{"type", "boolean"},
                              {"description", "Rebuild an existing patch (default false)"}}}},
                           {"id", "UMin", "UMax", "VMin", "VMax"}),
             Kind::Write,
             false,
             [this](const nlohmann::json& args) {
                 return patch_from_boundaries(session_, history_, args);
             });
    add_tool("network_from_curves",
             "Add a grid of patches over a network of curves, every internal edge joined with "
             "`continuity`, as for a hood or roof. along_u holds rows + 1 lines of constant v "
             "(each a list of one curve per column, in column order); along_v holds cols + 1 "
             "lines of constant u (one curve per row). Patch (r, c) is '<prefix>_<r>_<c>', bounded "
             "by along_u[r][c] (VMin), along_u[r + 1][c] (VMax), along_v[c][r] (UMin) and "
             "along_v[c + 1][r] (UMax).",
             object_schema(
                 {{"prefix", {{"type", "string"}, {"description", "Patch id prefix"}}},
                  {"along_u",
                   {{"type", "array"},
                    {"minItems", 2},
                    {"items", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                    {"description", "Lines of constant v, each a list of curve ids"}}},
                  {"along_v",
                   {{"type", "array"},
                    {"minItems", 2},
                    {"items", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                    {"description", "Lines of constant u, each a list of curve ids"}}},
                  {"continuity", continuity_schema(false, "Across internal edges (default G2)")}},
                 {"prefix", "along_u", "along_v"}),
             Kind::Write,
             false,
             [this](const nlohmann::json& args) {
                 return network_from_curves(session_, history_, args);
             });
    add_tool(
        "patch_blend",
        "Add a patch filling the gap between an edge of one patch and an edge of another, "
        "joined to both with `continuity`. The two edges must have the same degree along "
        "them. Its v runs from `from` (its VMin) to `to` (its VMax).",
        object_schema(
            {{"id", {{"type", "string"}, {"description", "New patch id"}}},
             {"from", edge_schema("Edge the blend starts on, 'patch.Edge'")},
             {"to", edge_schema("Edge the blend ends on, 'patch.Edge'")},
             {"continuity", continuity_schema(false, "On both sides (default G2)")},
             {"degree_across",
              degree_schema("Degree across the gap; default and minimum 2k + 1 for Gk")},
             {"replace",
              {{"type", "boolean"}, {"description", "Rebuild an existing patch (default false)"}}}},
            {"id", "from", "to"}),
        Kind::Write,
        false,
        [this](const nlohmann::json& args) { return patch_blend(session_, history_, args); });
    add_tool(
        "edge_constrain",
        "Require two patch edges that lie along each other to join with G0, G1 or G2, or "
        "remove that requirement with 'none'. Edges built on a shared curve lie along each "
        "other; get_selection lists which edges do.",
        object_schema({{"a", edge_schema("One edge, 'patch.Edge'")},
                       {"b", edge_schema("The other edge, 'patch.Edge'")},
                       {"continuity", continuity_schema(true, "Required continuity")}},
                      {"a", "b", "continuity"}),
        Kind::Write,
        true,
        [this](const nlohmann::json& args) { return edge_constrain(session_, history_, args); });
    add_tool("symmetry_constrain",
             "Require an edge lying in the centre plane Y = 0 to join its mirror image with G0, "
             "G1 or G2, or remove that with 'none'. G1 already brings G2 at the mirror.",
             object_schema({{"edge", edge_schema("Edge in Y = 0, 'patch.Edge'")},
                            {"continuity", continuity_schema(true, "Required continuity")}},
                           {"edge", "continuity"}),
             Kind::Write,
             true,
             [this](const nlohmann::json& args) {
                 return symmetry_constrain(session_, history_, args);
             });
    add_tool(
        "patch_set_degree",
        "Set a patch's degree in u and/or v. Higher degrees give the solver more freedom "
        "(for G2 on several sides, or a smoother interior) at the cost of a less economical "
        "surface; the budgets' max_degree says how high is acceptable. Edges joined to "
        "neighbours need equal degrees, so the change carries across linked edges. Raising "
        "keeps the current surface exactly as the solve's start.",
        object_schema({{"patch", {{"type", "string"}, {"description", "Patch id"}}},
                       {"degree_u", degree_schema("Degree in u")},
                       {"degree_v", degree_schema("Degree in v")}},
                      {"patch"}),
        Kind::Write,
        true,
        [this](const nlohmann::json& args) { return patch_set_degree(session_, history_, args); });
    add_tool(
        "fairness_set",
        "Set how the solver decides what the constraints leave free. fairness weighs the "
        "fairness energy (order E3, change of curvature, the default; or E2, bending); move pulls "
        "towards the current control points. fairness 1, move 0 (the default) gives the fairest "
        "surface; fairness 0, move 1 the least change from the current one, e.g. to make one edge "
        "G2 without reshaping the rest.",
        object_schema({{"order", {{"type", "string"}, {"enum", {"E2", "E3"}}}},
                       {"fairness", {{"type", "number"}, {"minimum", 0}}},
                       {"move", {{"type", "number"}, {"minimum", 0}}}}),
        Kind::Write,
        true,
        [this](const nlohmann::json& args) { return fairness_set(session_, history_, args); });
    add_tool("solve",
             "Solve the open scene again and write it back, with the full quality report. Edits "
             "already solve; this is for a scene file changed by hand or authored without control "
             "points.",
             empty_schema(),
             Kind::Write,
             true,
             [this](const nlohmann::json&) { return solve(session_, history_); });
    add_tool("undo",
             "Take back the last edit made through these tools since the scene was opened.",
             empty_schema(),
             Kind::Write,
             false,
             [this](const nlohmann::json&) { return undo(session_, history_); });
    add_tool("redo",
             "Make again the last edit that undo took back.",
             empty_schema(),
             Kind::Write,
             false,
             [this](const nlohmann::json&) { return redo(session_, history_); });
    add_tool("export_iges",
             "Write the open scene's patches to an IGES file (entity 128, millimetres) for NX, "
             "CATIA or Alias. A relative path is taken relative to the scene file.",
             object_schema({{"path", {{"type", "string"}, {"description", "IGES file to write"}}}},
                           {"path"}),
             Kind::Write,
             true,
             [this](const nlohmann::json& args) { return export_iges(session_, args); });

    // Studio.
    add_tool("tessellate",
             "Studio: triangle meshes of every patch with normals, (u, v), Gaussian and mean "
             "curvature and the highlight parameter per vertex.",
             object_schema({{"grid",
                             {{"type", "integer"},
                              {"minimum", 2},
                              {"maximum", 257},
                              {"description", "Vertices per patch side (default 49)"}}}}),
             Kind::Ui,
             true,
             [this](const nlohmann::json& args) { return tessellate(session_, args); });
    add_tool("cv_hulls",
             "Studio: every patch's control net.",
             empty_schema(),
             Kind::Ui,
             true,
             [this](const nlohmann::json&) { return cv_hulls(session_); });
    add_tool("boundary_combs",
             "Studio: curvature combs along every patch boundary.",
             object_schema({{"samples",
                             {{"type", "integer"},
                              {"minimum", 2},
                              {"maximum", 513},
                              {"description", "Samples per edge (default 33)"}}}}),
             Kind::Ui,
             true,
             [this](const nlohmann::json& args) { return boundary_combs(session_, args); });
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
        if (tool.kind == Kind::Ui && !options_.ui_tools) {
            continue;
        }
        nlohmann::json annotations = {{"readOnlyHint", tool.kind != Kind::Write},
                                      {"idempotentHint", tool.idempotent},
                                      {"openWorldHint", false}};
        if (tool.kind == Kind::Write) {
            // Edits change the scene file, but every one of them can be undone.
            annotations["destructiveHint"] = false;
        }
        tools.push_back({{"name", tool.name},
                         {"description", tool.description},
                         {"inputSchema", tool.input_schema},
                         {"annotations", std::move(annotations)}});
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
        return t.name == name && (t.kind != Kind::Ui || options_.ui_tools);
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
    } catch (const ToolError& e) {
        result = error_result(e);
    } catch (const std::exception& e) {
        result = error_result(ToolError(e.what(), "failed"));
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
