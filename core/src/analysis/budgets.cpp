#include "ada/analysis/budgets.hpp"

#include <fmt/format.h>

#include <stdexcept>
#include <string>

namespace ada {

namespace {

template<typename T>
nlohmann::json optional_json(const std::optional<T>& value) {
    return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

} // namespace

Budgets budgets_from_json(const nlohmann::json& overrides) {
    Budgets budgets;
    if (overrides.is_null()) {
        return budgets;
    }
    if (!overrides.is_object()) {
        throw std::invalid_argument("budgets must be a JSON object");
    }
    for (const auto& [key, value] : overrides.items()) {
        try {
            if (key == "g0_gap_mm") {
                budgets.g0_gap_mm = value.get<double>();
            } else if (key == "g1_angle_deg") {
                budgets.g1_angle_deg = value.get<double>();
            } else if (key == "g2_relative") {
                budgets.g2_relative = value.get<double>();
            } else if (key == "max_zigzag_rows") {
                budgets.max_zigzag_rows = value.get<std::size_t>();
            } else if (key == "max_highlight_inflections") {
                budgets.max_highlight_inflections = value.get<std::size_t>();
            } else if (key == "max_highlight_curvature_rate") {
                budgets.max_highlight_curvature_rate = value.get<double>();
            } else if (key == "allow_curvature_reversal") {
                budgets.allow_curvature_reversal = value.get<bool>();
            } else if (key == "max_degree") {
                budgets.max_degree = value.get<int>();
            } else {
                throw std::invalid_argument(fmt::format("unknown budget '{}'", key));
            }
        } catch (const nlohmann::json::exception& e) {
            throw std::invalid_argument(fmt::format("budget '{}': {}", key, e.what()));
        }
    }
    return budgets;
}

nlohmann::json to_json(const Budgets& budgets) {
    return {
        {"g0_gap_mm", budgets.g0_gap_mm},
        {"g1_angle_deg", budgets.g1_angle_deg},
        {"g2_relative", budgets.g2_relative},
        {"max_zigzag_rows", budgets.max_zigzag_rows},
        {"max_highlight_inflections", optional_json(budgets.max_highlight_inflections)},
        {"max_highlight_curvature_rate", optional_json(budgets.max_highlight_curvature_rate)},
        {"allow_curvature_reversal", optional_json(budgets.allow_curvature_reversal)},
        {"max_degree", optional_json(budgets.max_degree)},
    };
}

} // namespace ada
