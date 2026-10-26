#include "common.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace ada::mcp {

namespace {

/// Ids listed when one is not found; enough to spot the intended one.
constexpr std::size_t kListedIds = 20;

const nlohmann::json& required(const nlohmann::json& args, const char* name) {
    if (!args.contains(name)) {
        throw ToolError(fmt::format("'{}' is required", name));
    }
    return args.at(name);
}

} // namespace

std::string edge_name(const EdgeRef& ref) {
    return fmt::format("{}.{}", ref.patch, to_string(ref.edge));
}

std::string string_argument(const nlohmann::json& args, const char* name) {
    const nlohmann::json& value = required(args, name);
    if (!value.is_string() || value.get<std::string>().empty()) {
        throw ToolError(fmt::format("'{}' must be a non-empty string", name));
    }
    return value.get<std::string>();
}

bool bool_argument(const nlohmann::json& args, const char* name, bool fallback) {
    if (!args.contains(name)) {
        return fallback;
    }
    if (!args.at(name).is_boolean()) {
        throw ToolError(fmt::format("'{}' must be true or false", name));
    }
    return args.at(name).get<bool>();
}

std::optional<double> optional_number(const nlohmann::json& args, const char* name) {
    if (!args.contains(name)) {
        return std::nullopt;
    }
    const nlohmann::json& value = args.at(name);
    if (!value.is_number() || !std::isfinite(value.get<double>())) {
        throw ToolError(fmt::format("'{}' must be a number", name));
    }
    return value.get<double>();
}

int integer_argument(const nlohmann::json& args, const char* name, int fallback, int lo, int hi) {
    return optional_integer(args, name, lo, hi).value_or(fallback);
}

std::optional<int> optional_integer(const nlohmann::json& args, const char* name, int lo, int hi) {
    if (!args.contains(name)) {
        return std::nullopt;
    }
    const nlohmann::json& value = args.at(name);
    if (!value.is_number_integer()) {
        throw ToolError(fmt::format("'{}' must be an integer", name));
    }
    const int n = value.get<int>();
    if (n < lo || n > hi) {
        throw ToolError(fmt::format("'{}' must be between {} and {}, got {}", name, lo, hi, n));
    }
    return n;
}

std::string checked_id(const std::string& id, const std::string& what) {
    const bool valid = !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
    });
    if (!valid) {
        throw ToolError(fmt::format("{} '{}' may only use letters, digits, '_' and '-'", what, id));
    }
    return id;
}

EdgeRef parse_edge(const std::string& text, const std::string& what) {
    const std::size_t dot = text.rfind('.');
    try {
        if (dot == std::string::npos || dot == 0) {
            throw std::invalid_argument("no '.'");
        }
        return {text.substr(0, dot), edge_from_string(text.substr(dot + 1))};
    } catch (const std::invalid_argument&) {
        throw ToolError(fmt::format("{} '{}' is not an edge reference", what, text),
                        "invalid_arguments",
                        "write edges as 'patch.Edge' with Edge one of UMin, UMax, VMin, VMax, "
                        "for example 'hood_0_0.UMax'");
    }
}

EdgeRef edge_argument(const nlohmann::json& args, const char* name) {
    return parse_edge(string_argument(args, name), fmt::format("'{}'", name));
}

Continuity continuity_argument(const nlohmann::json& args, const char* name, Continuity fallback) {
    if (!args.contains(name)) {
        return fallback;
    }
    const std::optional<Continuity> c = continuity_or_none(args, name);
    if (!c.has_value()) {
        throw ToolError(fmt::format("'{}' must be G0, G1 or G2 here", name));
    }
    return *c;
}

std::optional<Continuity> continuity_or_none(const nlohmann::json& args, const char* name) {
    const std::string value = string_argument(args, name);
    if (value == "none") {
        return std::nullopt;
    }
    try {
        return continuity_from_string(value);
    } catch (const std::invalid_argument&) {
        throw ToolError(fmt::format("'{}' must be G0, G1, G2 or none, got '{}'", name, value));
    }
}

std::vector<Vec3> points_argument(const nlohmann::json& value, const std::string& what) {
    if (!value.is_array()) {
        throw ToolError(fmt::format("{} must be a list of [x, y, z] points", what));
    }
    std::vector<Vec3> points;
    for (std::size_t k = 0; k < value.size(); ++k) {
        const nlohmann::json& p = value[k];
        const bool valid = p.is_array() && p.size() == 3 &&
                           std::all_of(p.begin(), p.end(), [](const nlohmann::json& x) {
                               return x.is_number() && std::isfinite(x.get<double>());
                           });
        if (!valid) {
            throw ToolError(fmt::format("{}[{}] must be [x, y, z] in millimetres", what, k));
        }
        points.emplace_back(p[0].get<double>(), p[1].get<double>(), p[2].get<double>());
    }
    return points;
}

std::string id_list(const std::vector<std::string>& ids) {
    std::string out;
    for (std::size_t k = 0; k < std::min(ids.size(), kListedIds); ++k) {
        out += (k == 0 ? "" : ", ") + ids[k];
    }
    if (ids.size() > kListedIds) {
        out += fmt::format(" and {} more", ids.size() - kListedIds);
    }
    return out;
}

} // namespace ada::mcp
