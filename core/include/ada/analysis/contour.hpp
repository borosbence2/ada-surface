#pragma once

#include <Eigen/Core>

#include <cstddef>
#include <functional>
#include <vector>

namespace ada {

/// A scalar field over the unit parameter square. Returns NaN where the field
/// is undefined (for example the highlight parameter where a surface normal
/// lies parallel to the light plane).
using ScalarField = std::function<double(double u, double v)>;

/// Samples of a field on the uniform grid `u_i = i / (nu - 1)`,
/// `v_j = j / (nv - 1)`, stored at `i * nv + j`.
struct ScalarGrid {
    std::size_t nu = 0;
    std::size_t nv = 0;
    std::vector<double> values;

    double at(std::size_t i, std::size_t j) const { return values[i * nv + j]; }
};

/// Throws `std::invalid_argument` unless both counts are at least 2.
ScalarGrid sample_field(const ScalarField& field, std::size_t nu, std::size_t nv);

/// One connected piece of a level set, as a polyline in (u, v).
struct ContourLine {
    double level = 0.0;
    std::vector<Eigen::Vector2d> uv;
    bool closed = false; ///< The last point connects back to the first.
};

/// Level-set polylines by marching squares over `grid`. Every crossing of a
/// grid edge is refined on the exact `field` (Illinois regula falsi), so the
/// points lie on the true level set to `tol::kContourCrossing`, not on the
/// piecewise-linear interpolant. Saddle cells are resolved by the field value
/// at the cell centre. Cells with an undefined (NaN) corner are skipped, which
/// splits any contour passing through them.
std::vector<ContourLine> extract_contours(const ScalarGrid& grid,
                                          const ScalarField& field,
                                          const std::vector<double>& levels);

} // namespace ada
