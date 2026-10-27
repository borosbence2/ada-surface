#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>

namespace ada {

// Default Class-A budgets. These are PLACEHOLDERS (MILESTONES.md section 4):
// OEMs set their own tolerances and keep them proprietary. Confirm realistic
// values with a practising Class-A modeller before reading anything into a
// pass or fail.
inline constexpr double kDefaultG0GapMm = 0.001;
inline constexpr double kDefaultG1AngleDeg = 0.01;
inline constexpr double kDefaultG2Relative = 0.01;
inline constexpr std::size_t kDefaultMaxZigzagRows = 0;

/// What a scene must satisfy. The first four always apply; the optional ones
/// are per-case (what counts as a clean highlight depends on the part) and are
/// only checked when a scene sets them.
struct Budgets {
    double g0_gap_mm = kDefaultG0GapMm;
    double g1_angle_deg = kDefaultG1AngleDeg;
    double g2_relative = kDefaultG2Relative;
    std::size_t max_zigzag_rows = kDefaultMaxZigzagRows;
    std::optional<std::size_t> max_highlight_inflections;
    std::optional<double> max_highlight_curvature_rate;
    /// Mean curvature changing sign inside a patch (an S-bend).
    std::optional<bool> allow_curvature_reversal;
    std::optional<int> max_degree;
    /// Fit to a target (L3): largest, RMS and 95th-percentile distance of the
    /// target points from the surface, mm. Unset: not checked.
    std::optional<double> fit_max_mm;
    std::optional<double> fit_rms_mm;
    std::optional<double> fit_p95_mm;
};

/// Defaults overridden by the keys present in `overrides` (same names as the
/// fields). Throws `std::invalid_argument` on an unknown key, so that a typo in
/// a scene file cannot silently leave a budget at its default.
Budgets budgets_from_json(const nlohmann::json& overrides);

/// Every field, optional ones as null when unset.
nlohmann::json to_json(const Budgets& budgets);

} // namespace ada
