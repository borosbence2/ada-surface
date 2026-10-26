#include "support/cases.hpp"

#include "ada/doc/recipes.hpp"
#include "ada/geom/curve_fit.hpp"

#include <cmath>
#include <functional>

namespace ada::test {

namespace {

/// Points per authored curve: enough for a cubic to follow a gentle shape.
constexpr int kPointsPerCurve = 5;

double round_01(double value) {
    return std::round(value * 10.0) / 10.0;
}

Vec3 rounded(const Vec3& p) {
    return {round_01(p.x()), round_01(p.y()), round_01(p.z())};
}

using Shape = std::function<Vec3(double a, double b)>;

/// Points along a straight line in the shape's parameter plane, rounded.
std::vector<Vec3> sample(const Shape& shape, double a0, double b0, double a1, double b1) {
    std::vector<Vec3> points;
    for (int k = 0; k < kPointsPerCurve; ++k) {
        const double s = static_cast<double>(k) / (kPointsPerCurve - 1);
        points.push_back(rounded(shape(a0 + s * (a1 - a0), b0 + s * (b1 - b0))));
    }
    return points;
}

std::string add_curve(Scene& scene, const std::string& id, std::vector<Vec3> points) {
    scene.curves.push_back({id, curve_through_points(points, 3), std::move(points)});
    return id;
}

/// A grid of curves over `shape` on the lines a = as[c], b = bs[r], wired up
/// as a network: u follows a, v follows b.
void network(Scene& scene,
             const std::string& prefix,
             const Shape& shape,
             const std::vector<double>& as,
             const std::vector<double>& bs) {
    NetworkSpec spec{prefix, {}, {}, Continuity::G2};
    for (std::size_t r = 0; r < bs.size(); ++r) {
        std::vector<std::string> line;
        for (std::size_t c = 0; c + 1 < as.size(); ++c) {
            line.push_back(add_curve(scene,
                                     prefix + "_u" + std::to_string(r) + std::to_string(c),
                                     sample(shape, as[c], bs[r], as[c + 1], bs[r])));
        }
        spec.along_u.push_back(line);
    }
    for (std::size_t c = 0; c < as.size(); ++c) {
        std::vector<std::string> line;
        for (std::size_t r = 0; r + 1 < bs.size(); ++r) {
            line.push_back(add_curve(scene,
                                     prefix + "_v" + std::to_string(c) + std::to_string(r),
                                     sample(shape, as[c], bs[r], as[c], bs[r + 1])));
        }
        spec.along_v.push_back(line);
    }
    add_network(scene, spec);
}

/// A single from_boundaries patch over `shape` on [a0, a1] x [b0, b1].
void single(Scene& scene,
            const std::string& id,
            const Shape& shape,
            double a0,
            double a1,
            double b0,
            double b1) {
    const std::string umin = add_curve(scene, id + "_umin", sample(shape, a0, b0, a0, b1));
    const std::string umax = add_curve(scene, id + "_umax", sample(shape, a1, b0, a1, b1));
    const std::string vmin = add_curve(scene, id + "_vmin", sample(shape, a0, b0, a1, b0));
    const std::string vmax = add_curve(scene, id + "_vmax", sample(shape, a0, b1, a1, b1));
    ScenePatch patch{id,
                     BezierPatch(1, 1, std::vector<Vec3>(4, Vec3::Zero())),
                     FromBoundaries{{umin, umax, vmin, vmax}, std::nullopt, std::nullopt},
                     false};
    patch.patch = initial_patch(scene, patch);
    scene.patches.push_back(std::move(patch));
}

/// Roof: a = y (outwards from the centre line), b = x (rearwards). Crown
/// along x, falling off towards the sides; even in y.
Vec3 roof(double y, double x) {
    const double s = x / 1400.0;
    const double t = y / 650.0;
    return {x, y, 1450.0 + 90.0 * s * (1.0 - s) - 55.0 * t * t};
}

/// Hood: a = y, b = x; rises towards the windscreen, falls to the sides.
Vec3 hood(double y, double x) {
    const double s = x / 1200.0;
    const double t = y / 700.0;
    return {x, y, 800.0 + 40.0 * s - 45.0 * s * s - 70.0 * t * t};
}

} // namespace

/// Budgets every panel case shares: one bicubic per patch (a higher degree
/// would mean something elevated it), and no S-bend anywhere on the panel.
void panel_budgets(Scene& scene) {
    scene.budgets.max_degree = 3;
    scene.budgets.allow_curvature_reversal = false;
}

AuthoredCase l0_roof_centre() {
    Scene scene;
    scene.description =
        "L0: full-width roof centre panel, one bicubic from four curves through points. Budgets: "
        "degree 3, no curvature reversal, and highlight lines without a single inflection: a "
        "crowned roof under an overhead light gives clean, gently bowed lines.";
    single(scene, "roof", roof, -650.0, 650.0, 0.0, 1400.0);
    panel_budgets(scene);
    scene.budgets.max_highlight_inflections = 0;
    return {"L0-roof-centre", std::move(scene)};
}

AuthoredCase l0s_roof_centre_sym() {
    Scene scene;
    scene.description =
        "L0s: the left half of the L0 roof, mirror-symmetric (G2) at Y = 0. Same budgets as L0; "
        "the boundary curves crossing Y = 0 give way about 0.1 mm to meet the plane at a right "
        "angle.";
    single(scene, "roof", roof, 0.0, 650.0, 0.0, 1400.0);
    scene.symmetry.push_back({{"roof", Edge::UMin}, Continuity::G2});
    panel_budgets(scene);
    scene.budgets.max_highlight_inflections = 0;
    return {"L0s-roof-centre-sym", std::move(scene)};
}

AuthoredCase l1_g2_blend() {
    // Two panels side by side in y with a 200 mm gap and a 60 mm step down,
    // bridged by a G2 blend. a = y, b = x for both.
    Scene scene;
    scene.description =
        "L1: two panels 200 mm apart with a 74 mm step, joined by a G2 blend (degree 5 across). "
        "The blend is an S by design, so curvature reversal is allowed and degree 5 is the cap. "
        "Highlight inflections: the S itself inflects once, and the nearly flat outer panel's "
        "lines are almost straight, so their curvature hovers around the 1 km noise floor; two "
        "is the budget.";
    scene.budgets.max_degree = 5;
    scene.budgets.allow_curvature_reversal = true;
    scene.budgets.max_highlight_inflections = 2;
    const Shape inner = [](double y, double x) { return hood(y, x); };
    const Shape outer = [](double y, double x) {
        const double s = x / 1200.0;
        const double t = (y - 700.0) / 300.0;
        return Vec3(x, y, 690.0 + 40.0 * s - 45.0 * s * s - 30.0 * t);
    };
    single(scene, "inner", inner, 0.0, 500.0, 0.0, 1200.0);
    single(scene, "outer", outer, 700.0, 1000.0, 0.0, 1200.0);
    ScenePatch blend{
        "gap",
        BezierPatch(1, 1, std::vector<Vec3>(4, Vec3::Zero())),
        Blend{{"inner", Edge::UMax}, {"outer", Edge::UMin}, Continuity::G2, std::nullopt},
        false};
    scene.patches.push_back(std::move(blend));
    return {"L1-g2-blend", std::move(scene)};
}

AuthoredCase l2_hood_2x2_sym() {
    Scene scene;
    scene.description =
        "L2: hood as a 2 x 2 network of curves through points, G2 on every internal edge, "
        "mirror-symmetric at Y = 0. Budgets: degree 3, no curvature reversal, at most one "
        "highlight inflection per line (the observed value; a regression guard rather than a "
        "Class-A rule).";
    network(scene, "hood", hood, {0.0, 350.0, 700.0}, {0.0, 600.0, 1200.0});
    panel_budgets(scene);
    scene.budgets.max_highlight_inflections = 1;
    scene.symmetry.push_back({{"hood_0_0", Edge::UMin}, Continuity::G2});
    scene.symmetry.push_back({{"hood_1_0", Edge::UMin}, Continuity::G2});
    return {"L2-hood-2x2-sym", std::move(scene)};
}

AuthoredCase l2_panel_3x3() {
    // A door skin standing in the x-z plane, bulging out in y: a = x, b = z.
    const Shape door = [](double x, double z) {
        const double s = x / 1050.0;
        const double t = (z - 400.0) / 650.0;
        return Vec3(x, 900.0 + 120.0 * s * (1.0 - s) + 80.0 * t * (1.0 - t) - 25.0 * t, z);
    };
    Scene scene;
    scene.description =
        "L2: door skin as a 3 x 3 network on a vertical panel, G2 on every internal edge. The "
        "default highlight light faces the panel from the side. Budgets: degree 3, no curvature "
        "reversal, at most one highlight inflection per line (the observed value; a regression "
        "guard).";
    network(scene, "door", door, {0.0, 350.0, 700.0, 1050.0}, {400.0, 620.0, 840.0, 1050.0});
    panel_budgets(scene);
    scene.budgets.max_highlight_inflections = 1;
    return {"L2-panel-3x3", std::move(scene)};
}

std::vector<AuthoredCase> all_cases() {
    return {
        l0_roof_centre(), l0s_roof_centre_sym(), l1_g2_blend(), l2_hood_2x2_sym(), l2_panel_3x3()};
}

} // namespace ada::test
