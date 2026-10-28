#pragma once

// The pipeline every editing tool goes through (edit_tools.cpp): apply one
// edit to a copy of the open scene, solve it, and only then write it back;
// an edit whose scene does not solve changes nothing.

#include "ada/doc/history.hpp"
#include "ada/doc/scene.hpp"

#include "server.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace ada::mcp {

/// What an edit did, for the agent: a sentence, and the same as data.
struct EditOutcome {
    std::string text;
    nlohmann::json data = nlohmann::json::object();
};

using Edit = std::function<EditOutcome(Scene&)>;

/// Applies `edit` to a copy of the open scene, solves it (with CV economy
/// when the scene's fit asks for it) and writes it back, or changes nothing
/// and says why. The result carries the outcome, the solve, the fit and a
/// short quality summary.
ToolResult apply_edit(SceneSession& session, History& history, const Edit& edit);

/// The error for a new item whose id is in use.
ToolError id_taken(const std::string& kind, const std::string& id);

} // namespace ada::mcp
