#pragma once

#include "ada/geom/bezier_curve.hpp"
#include "ada/geom/vec.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ada {

/// One side of the unit parameter square.
enum class Edge {
    UMin, ///< u = 0, runs along v.
    UMax, ///< u = 1, runs along v.
    VMin, ///< v = 0, runs along u.
    VMax, ///< v = 1, runs along u.
};

/// Mixed partials `d^(k+l) S / du^k dv^l` at one point, for `k + l <= 3`.
/// Third order is as far as Class-A analysis goes: the highlight-line
/// curvature and the E3 fairness energy both need it.
class PatchDerivatives {
public:
    static constexpr int kMaxOrder = 3;

    /// All entries zero.
    PatchDerivatives();

    /// Throws `std::out_of_range` unless `0 <= k`, `0 <= l` and `k + l <= kMaxOrder`.
    const Vec3& operator()(int du_order, int dv_order) const;
    Vec3& operator()(int du_order, int dv_order);

    const Vec3& position() const { return (*this)(0, 0); }

    const Vec3& du() const { return (*this)(1, 0); }

    const Vec3& dv() const { return (*this)(0, 1); }

private:
    static constexpr std::size_t kStride = kMaxOrder + 1;
    std::array<Vec3, kStride * kStride> values_;
};

/// Polynomial tensor-product Bézier patch on [0, 1]^2:
/// `S(u, v) = sum_ij B_{i,n}(u) B_{j,m}(v) P_ij`.
///
/// Control point `(i, j)` has `i` along u (0..degree_u) and `j` along v
/// (0..degree_v) and is stored at `i * num_v() + j`: the same layout as
/// `n2s::NurbsSurface`, so converting between the two is a copy.
///
/// Validity is a class invariant: the constructor throws
/// `std::invalid_argument` on a negative degree or a net of the wrong size.
class BezierPatch {
public:
    BezierPatch(int degree_u, int degree_v, std::vector<Vec3> control_points);

    int degree_u() const noexcept { return degree_u_; }

    int degree_v() const noexcept { return degree_v_; }

    std::size_t num_u() const noexcept { return static_cast<std::size_t>(degree_u_) + 1; }

    std::size_t num_v() const noexcept { return static_cast<std::size_t>(degree_v_) + 1; }

    const Vec3& control_point(std::size_t i, std::size_t j) const {
        return points_[i * num_v() + j];
    }

    Vec3& control_point(std::size_t i, std::size_t j) { return points_[i * num_v() + j]; }

    const std::vector<Vec3>& control_points() const noexcept { return points_; }

    /// Point at `(u, v)`, by tensor-product de Casteljau.
    Vec3 evaluate(double u, double v) const;

    /// One mixed partial; zero when an order exceeds the degree in its
    /// direction. Throws `std::invalid_argument` for a negative order.
    Vec3 derivative(double u, double v, int du_order, int dv_order) const;

    /// Every mixed partial up to total order 3.
    PatchDerivatives derivatives(double u, double v) const;

    /// The mixed hodograph: a patch of degree `(n - du_order, m - dv_order)`
    /// whose value is `d^(k+l) S / du^k dv^l` (degree 0 and zero control points
    /// in a direction whose order exceeds its degree).
    BezierPatch hodograph(int du_order, int dv_order) const;

    /// The same surface with the u (or v) degree raised `times` times.
    BezierPatch elevated_u(int times = 1) const;
    BezierPatch elevated_v(int times = 1) const;

    /// The pieces on either side of the given u (or v), each reparameterised to
    /// [0, 1]^2. Throws `std::invalid_argument` unless the split value lies
    /// strictly inside (0, 1).
    std::pair<BezierPatch, BezierPatch> split_u(double u) const;
    std::pair<BezierPatch, BezierPatch> split_v(double v) const;

    /// Degree in the parameter that runs along `edge`, and in the one across it.
    int degree_along(Edge edge) const noexcept;
    int degree_across(Edge edge) const noexcept;

    /// The control-point row parallel to `edge`, `depth` rows in from it
    /// (0 is the boundary row itself), ordered by the parameter running along
    /// the edge. Rows 0..k fix the cross-boundary derivatives up to order k,
    /// which is what the G1/G2 edge constraints act on. Throws
    /// `std::out_of_range` unless `0 <= depth <= degree_across(edge)`.
    std::vector<Vec3> edge_row(Edge edge, int depth) const;

    /// The (i, j) index of the `k`-th control point of `edge_row(edge, depth)`.
    /// Throws `std::out_of_range` on a depth or position outside the row.
    std::pair<std::size_t, std::size_t> edge_row_cv(Edge edge, int depth, std::size_t k) const;

    /// The boundary curve on `edge`, parameterised like `edge_row`.
    BezierCurve boundary(Edge edge) const;

private:
    int degree_u_;
    int degree_v_;
    std::vector<Vec3> points_;
};

/// `(u, v)` of the point at parameter `s` along `edge`, in the same direction
/// as `BezierPatch::edge_row`.
std::pair<double, double> edge_point(Edge edge, double s);

/// The first cross-boundary derivative at a point on `edge`, signed to point
/// into the patch: `Su` on UMin, `-Su` on UMax, `Sv` on VMin, `-Sv` on VMax.
Vec3 inward_derivative(const PatchDerivatives& d, Edge edge);

/// "UMin", "UMax", "VMin" or "VMax".
const char* to_string(Edge edge);

/// Inverse of `to_string`; throws `std::invalid_argument` on anything else.
Edge edge_from_string(const std::string& name);

} // namespace ada
