#pragma once

#include "server.hpp"

#include <nlohmann/json.hpp>

namespace ada::mcp {

// Agent tools.
ToolResult scene_open(SceneSession& session, const nlohmann::json& args);
ToolResult scene_summary(SceneSession& session);
ToolResult quality_report(SceneSession& session);

// Studio-only tools (--ui-tools). Bulk arrays travel as base64 little-endian
// float32/uint32 (see base64.hpp).
ToolResult tessellate(SceneSession& session, const nlohmann::json& args);
ToolResult cv_hulls(SceneSession& session);
ToolResult boundary_combs(SceneSession& session, const nlohmann::json& args);

} // namespace ada::mcp
