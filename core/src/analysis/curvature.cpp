#include "ada/analysis/curvature.hpp"

#include "ada/analysis/differential.hpp"
#include "ada/analysis/sampling.hpp"
#include "ada/tolerances.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace ada {

namespace {

/// Tracks whether a quantity has taken clearly positive and clearly negative
/// values, ignoring magnitudes below the noise floor.
struct SignTracker {
    bool positive = false;
    bool negative = false;

    void add(double value, double noise) {
        positive = positive || value > noise;
        negative = negative || value < -noise;
    }

    bool both() const { return positive && negative; }
};

} // namespace

CurvatureSummary summarize_curvature(const PatchEvaluator& patch, std::size_t grid) {
    const std::vector<double> params = uniform_parameters(grid);
    CurvatureSummary summary;
    summary.gaussian_min = std::numeric_limits<double>::infinity();
    summary.gaussian_max = -std::numeric_limits<double>::infinity();
    summary.mean_min = std::numeric_limits<double>::infinity();
    summary.mean_max = -std::numeric_limits<double>::infinity();

    // Gaussian curvature is a product of two curvatures, so its noise floor is
    // the square of the curvature floor.
    const double gaussian_noise = tol::kCurvatureSignNoise * tol::kCurvatureSignNoise;
    SignTracker gaussian_sign;
    SignTracker mean_sign;
    std::size_t saddles = 0;

    for (const double u : params) {
        for (const double v : params) {
            ++summary.samples;
            const std::optional<n2s::SurfaceCurvature> k = curvature(patch.derivatives(u, v));
            if (!k.has_value()) {
                ++summary.undefined_samples;
                continue;
            }
            summary.gaussian_min = std::min(summary.gaussian_min, k->gaussian);
            summary.gaussian_max = std::max(summary.gaussian_max, k->gaussian);
            summary.mean_min = std::min(summary.mean_min, k->mean);
            summary.mean_max = std::max(summary.mean_max, k->mean);
            summary.max_abs_principal =
                std::max({summary.max_abs_principal, std::abs(k->k1), std::abs(k->k2)});
            gaussian_sign.add(k->gaussian, gaussian_noise);
            mean_sign.add(k->mean, tol::kCurvatureSignNoise);
            if (k->gaussian < -gaussian_noise) {
                ++saddles;
            }
        }
    }

    const std::size_t defined = summary.samples - summary.undefined_samples;
    if (defined == 0) {
        summary.gaussian_min = summary.gaussian_max = 0.0;
        summary.mean_min = summary.mean_max = 0.0;
        return summary;
    }
    summary.saddle_fraction = static_cast<double>(saddles) / static_cast<double>(defined);
    summary.gaussian_changes_sign = gaussian_sign.both();
    summary.mean_changes_sign = mean_sign.both();
    return summary;
}

} // namespace ada
