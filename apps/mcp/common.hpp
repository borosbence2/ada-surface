#pragma once

// Shared by the tools: reading arguments and naming things. Every argument
// failure is a ToolError that names the argument and says what it should
// have been, so an agent can fix its call.

#include "ada/doc/scene.hpp"

#include "server.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace ada::mcp {

/// Curve adjustments and fit errors smaller than this are rounding, not
/// worth a line in a tool's text.
inline constexpr double kReportedDeviationMm = 1e-6;

std::string edge_name(const EdgeRef& ref);

std::string string_argument(const nlohmann::json& args, const char* name);

bool bool_argument(const nlohmann::json& args, const char* name, bool fallback);

std::optional<double> optional_number(const nlohmann::json& args, const char* name);

int integer_argument(const nlohmann::json& args, const char* name, int fallback, int lo, int hi);

std::optional<int> optional_integer(const nlohmann::json& args, const char* name, int lo, int hi);

/// An id for a new curve or patch: letters, digits, '_' and '-', so that
/// "patch.Edge" references stay unambiguous. `what` names it in the error.
std::string checked_id(const std::string& id, const std::string& what);

/// "patch.Edge" with Edge one of UMin, UMax, VMin, VMax.
EdgeRef parse_edge(const std::string& text, const std::string& what);

EdgeRef edge_argument(const nlohmann::json& args, const char* name);

/// "G0", "G1" or "G2"; `fallback` when absent.
Continuity continuity_argument(const nlohmann::json& args, const char* name, Continuity fallback);

/// "G0", "G1", "G2" or "none" (nullopt), required: what a constraint tool sets.
std::optional<Continuity> continuity_or_none(const nlohmann::json& args, const char* name);

/// A list of [x, y, z] points in millimetres.
std::vector<Vec3> points_argument(const nlohmann::json& value, const std::string& what);

/// Up to a screenful of ids, comma-separated, for "did you mean" lists.
std::string id_list(const std::vector<std::string>& ids);

} // namespace ada::mcp
