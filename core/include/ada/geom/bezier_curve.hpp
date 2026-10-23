#pragma once

#include "ada/geom/vec.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace ada {

/// Polynomial Bézier curve on [0, 1]: `C(t) = sum_i B_{i,n}(t) P_i`.
///
/// Degree 0 (one control point, a constant curve) is allowed because repeated
/// hodographs end there. Validity is a class invariant: the constructor throws
/// `std::invalid_argument` on an empty control polygon.
class BezierCurve {
public:
    explicit BezierCurve(std::vector<Vec3> control_points);

    int degree() const noexcept { return static_cast<int>(points_.size()) - 1; }

    std::size_t num_control_points() const noexcept { return points_.size(); }

    const std::vector<Vec3>& control_points() const noexcept { return points_; }

    const Vec3& control_point(std::size_t i) const { return points_[i]; }

    /// Point at `t`, by de Casteljau. Values outside [0, 1] extrapolate.
    Vec3 evaluate(double t) const;

    /// The `order`-th derivative at `t`; zero for orders above the degree.
    /// Throws `std::invalid_argument` for a negative order.
    Vec3 derivative(double t, int order) const;

    /// The `order`-th hodograph, a Bézier curve of degree `n - order` (degree 0
    /// with a zero control point once `order` exceeds the degree).
    BezierCurve hodograph(int order) const;

    /// The same curve with its degree raised `times` times.
    BezierCurve elevated(int times = 1) const;

    /// The pieces on [0, t] and [t, 1], each reparameterised to [0, 1].
    /// Throws `std::invalid_argument` unless 0 < t < 1.
    std::pair<BezierCurve, BezierCurve> split(double t) const;

    /// The same curve traversed from t = 1 to t = 0.
    BezierCurve reversed() const;

private:
    std::vector<Vec3> points_;
};

} // namespace ada
