#pragma once

#include "ada/analysis/contour.hpp"
#include "ada/geom/patch_evaluator.hpp"
#include "ada/geom/vec.hpp"

#include <cstddef>
#include <vector>

namespace ada {

/// A family of parallel light lines in a plane (Beier & Chen 1994, "Highlight-
/// line algorithm for realtime surface-quality assessment"). A surface point
/// lies on a highlight line when its normal line meets one of the light lines.
struct LightPlane {
    Vec3 origin;    ///< A point on the plane; light line 0 passes through it.
    Vec3 normal;    ///< Unit normal of the plane.
    Vec3 direction; ///< Unit direction of the light lines, in the plane.
    Vec3 across;    ///< Unit `normal x direction`: the axis that counts lines.
    double spacing; ///< Distance between neighbouring light lines, mm.
};

/// Orthonormalises the frame. Throws `std::invalid_argument` if `direction` is
/// parallel to `normal` or `spacing` is not positive.
LightPlane
make_light_plane(const Vec3& origin, const Vec3& normal, const Vec3& direction, double spacing);

/// The default studio light for parts inside the box [lo, hi] whose surfaces
/// mostly face along the unit axis `facing` (+Z for a hood or roof, +-Y for a
/// door): a plane perpendicular to that axis, one box diagonal beyond the box,
/// light lines along vehicle X (along Z when the part faces X), 24 lines
/// across the box diagonal.
LightPlane default_light_plane(const Vec3& lo, const Vec3& hi, const Vec3& facing);

/// The highlight parameter `s`: where the normal line through `point` meets
/// the light plane, measured along `across` from `origin`. Highlight lines are
/// the level sets `s = k * spacing`. NaN where the normal line runs parallel to
/// the plane. Independent of the view, which is what lets the kernel compute
/// and test it without a renderer.
double highlight_parameter(const LightPlane& light, const Vec3& point, const Vec3& unit_normal);

/// `highlight_parameter` over a patch; NaN also where the normal is undefined.
ScalarField highlight_field(const PatchEvaluator& patch, const LightPlane& light);

/// Light lines a highlight analysis considers: as many as any real light
/// source has, and a bound on the work when a surface turns edge-on to the
/// light plane.
inline constexpr int kMaxHighlightLines = 200;

/// Isophote field `N . L` for a light direction L; level sets are isophotes.
ScalarField isophote_field(const PatchEvaluator& patch, const Vec3& light_direction);

/// The highlight lines on a patch: every level `k * spacing` the field reaches,
/// at most `kMaxHighlightLines` of them around the light's origin. Where a
/// normal runs nearly parallel to the light plane the parameter grows without
/// bound, so the reachable levels can be unbounded too.
std::vector<ContourLine>
highlight_lines(const PatchEvaluator& patch, const LightPlane& light, std::size_t grid);

/// Shape of one highlight line on the surface. Curvature is the line's
/// geodesic curvature (bending within the surface), signed by the surface
/// normal, estimated at each interior polyline vertex.
struct HighlightLineShape {
    double level = 0.0;
    std::size_t points = 0;
    /// Sign changes of the curvature, ignoring magnitudes below
    /// `tol::kHighlightCurvatureNoise`. Not every inflection is a defect: a
    /// planar light over a perfect dome already gives bell-shaped lines with
    /// two. That is why the budget on this is set per case; what the metric is
    /// good at is a part going from N inflections to more after an edit.
    std::size_t inflections = 0;
    double max_abs_curvature = 0.0;  ///< 1/mm
    double max_curvature_rate = 0.0; ///< |d curvature / ds|, 1/mm^2
};

HighlightLineShape analyze_highlight_line(const PatchEvaluator& patch, const ContourLine& line);

struct HighlightSummary {
    std::size_t lines = 0;
    std::size_t max_inflections = 0;
    double max_curvature_rate = 0.0;
    std::vector<HighlightLineShape> shapes;
};

HighlightSummary
analyze_highlights(const PatchEvaluator& patch, const LightPlane& light, std::size_t grid);

} // namespace ada
