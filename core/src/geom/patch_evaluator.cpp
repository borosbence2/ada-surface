#include "ada/geom/patch_evaluator.hpp"

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <stdexcept>

namespace ada {

namespace {

constexpr int kMaxOrder = PatchDerivatives::kMaxOrder;

// Hodographs are stored k-major for k + l <= 3: (0,0..3), (1,0..2), (2,0..1),
// (3,0). kOffset[k] is where row k starts.
constexpr std::array<std::size_t, kMaxOrder + 1> kOffset{0, 4, 7, 9};

std::size_t slot(int du_order, int dv_order) {
    if (du_order < 0 || dv_order < 0 || du_order + dv_order > kMaxOrder) {
        throw std::out_of_range(fmt::format(
            "derivative ({}, {}) is outside total order <= {}", du_order, dv_order, kMaxOrder));
    }
    return kOffset[static_cast<std::size_t>(du_order)] + static_cast<std::size_t>(dv_order);
}

} // namespace

PatchEvaluator::PatchEvaluator(const BezierPatch& patch) {
    hodographs_.reserve(kOffset.back() + 1);
    for (int k = 0; k <= kMaxOrder; ++k) {
        for (int l = 0; k + l <= kMaxOrder; ++l) {
            hodographs_.push_back(patch.hodograph(k, l));
        }
    }
}

Vec3 PatchEvaluator::evaluate(double u, double v) const {
    return hodographs_.front().evaluate(u, v);
}

Vec3 PatchEvaluator::derivative(double u, double v, int du_order, int dv_order) const {
    return hodographs_[slot(du_order, dv_order)].evaluate(u, v);
}

PatchDerivatives PatchEvaluator::derivatives(double u, double v) const {
    PatchDerivatives table;
    for (int k = 0; k <= kMaxOrder; ++k) {
        for (int l = 0; k + l <= kMaxOrder; ++l) {
            table(k, l) = derivative(u, v, k, l);
        }
    }
    return table;
}

} // namespace ada
