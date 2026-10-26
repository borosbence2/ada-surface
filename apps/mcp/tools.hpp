#pragma once

#include "server.hpp"

#include <nlohmann/json.hpp>

namespace ada::mcp {

// Agent tools.
ToolResult scene_open(SceneSession& session, const nlohmann::json& args);
ToolResult scene_summary(SceneSession& session);
ToolResult quality_report(SceneSession& session);

/// Solves the open scene's recipes, writes the result back to its file, and
/// returns the solve summary with the report.
ToolResult solve(SceneSession& session, History& history);

/// Writes the open scene's patches as IGES (entity 128). A relative path is
/// taken relative to the scene file's directory.
ToolResult export_iges(SceneSession& session, const nlohmann::json& args);

/// Reads selection.json, which Studio writes next to the scene when the user
/// picks a patch or edge, and says what lies along a picked edge.
ToolResult get_selection(SceneSession& session);

// Editing tools (edit_tools.cpp). Each applies one edit to the open scene,
// solves it and writes it back, returning what changed with a short quality
// summary. An edit whose scene does not solve is refused with the reason and
// changes nothing. `history` records every edit for undo.
ToolResult scene_new(SceneSession& session, const nlohmann::json& args);
ToolResult curve_from_points(SceneSession& session, History& history, const nlohmann::json& args);
ToolResult
patch_from_boundaries(SceneSession& session, History& history, const nlohmann::json& args);
ToolResult patch_blend(SceneSession& session, History& history, const nlohmann::json& args);
ToolResult network_from_curves(SceneSession& session, History& history, const nlohmann::json& args);
ToolResult edge_constrain(SceneSession& session, History& history, const nlohmann::json& args);
ToolResult symmetry_constrain(SceneSession& session, History& history, const nlohmann::json& args);
ToolResult patch_set_degree(SceneSession& session, History& history, const nlohmann::json& args);
ToolResult fairness_set(SceneSession& session, History& history, const nlohmann::json& args);
ToolResult undo(SceneSession& session, History& history);
ToolResult redo(SceneSession& session, History& history);

// Studio-only tools (--ui-tools). Bulk arrays travel as base64 little-endian
// float32/uint32 (see base64.hpp).
ToolResult tessellate(SceneSession& session, const nlohmann::json& args);
ToolResult cv_hulls(SceneSession& session);
ToolResult boundary_combs(SceneSession& session, const nlohmann::json& args);

} // namespace ada::mcp
