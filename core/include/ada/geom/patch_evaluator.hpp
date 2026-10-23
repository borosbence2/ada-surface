#pragma once

#include "ada/geom/bezier_patch.hpp"
#include "ada/geom/vec.hpp"

#include <vector>

namespace ada {

/// A patch with its hodographs up to total order 3 built once, so that each
/// derivative evaluation is a single tensor de Casteljau. Analysis samples
/// every patch thousands of times; rebuilding hodographs per sample would
/// dominate the cost.
class PatchEvaluator {
public:
    explicit PatchEvaluator(const BezierPatch& patch);

    const BezierPatch& patch() const noexcept { return hodographs_.front(); }

    Vec3 evaluate(double u, double v) const;

    /// Throws `std::out_of_range` unless `0 <= k`, `0 <= l`, `k + l <= 3`.
    Vec3 derivative(double u, double v, int du_order, int dv_order) const;

    PatchDerivatives derivatives(double u, double v) const;

private:
    std::vector<BezierPatch> hodographs_;
};

} // namespace ada
