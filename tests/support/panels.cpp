#include "support/panels.hpp"

#include "ada/analysis/differential.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace ada::test {

BezierPatch domed_panel() {
    std::vector<Vec3> points;
    for (int i = 0; i <= 3; ++i) {
        for (int j = 0; j <= 3; ++j) {
            const double x = 1000.0 * i / 3.0;
            const double y = 600.0 * j / 3.0;
            const double z = 40.0 * std::sin(3.0 * i / 3.0) + 25.0 * std::sin(2.5 * j / 3.0) +
                             (i == 1 && j == 2 ? 8.0 : 0.0);
            points.emplace_back(x, y, z);
        }
    }
    return BezierPatch(3, 3, std::move(points));
}

BezierPatch g2_neighbour(const BezierPatch& a, double alpha, double gamma, double sign) {
    std::vector<Vec3> points(16);
    auto b = [&points](std::size_t i, std::size_t j) -> Vec3& { return points[i * 4 + j]; };
    for (std::size_t i = 0; i <= 3; ++i) {
        const Vec3 a1 = a.control_point(i, 1);
        const Vec3 a2 = a.control_point(i, 2);
        const Vec3 a3 = a.control_point(i, 3);
        b(i, 0) = a3;
        b(i, 1) = b(i, 0) + sign * alpha * (a3 - a2);
        b(i, 2) = 2.0 * b(i, 1) - b(i, 0) + alpha * alpha * (a3 - 2.0 * a2 + a1) +
                  0.5 * gamma * (a3 - a2);
        b(i, 3) = 2.0 * b(i, 2) - b(i, 1) + Vec3(0, 0, 3.0 * static_cast<double>(i));
    }
    return BezierPatch(3, 3, std::move(points));
}

BezierPatch flip_u(const BezierPatch& p) {
    std::vector<Vec3> points;
    for (std::size_t i = p.num_u(); i-- > 0;) {
        for (std::size_t j = 0; j < p.num_v(); ++j) {
            points.push_back(p.control_point(i, j));
        }
    }
    return BezierPatch(p.degree_u(), p.degree_v(), std::move(points));
}

BezierPatch mirror_y(const BezierPatch& p) {
    std::vector<Vec3> points = p.control_points();
    for (Vec3& point : points) {
        point.y() = -point.y();
    }
    return BezierPatch(p.degree_u(), p.degree_v(), std::move(points));
}

BezierPatch nudge(BezierPatch p, std::size_t i, std::size_t j, double distance) {
    const PatchEvaluator e(p);
    const double u = static_cast<double>(i) / p.degree_u();
    const double v = static_cast<double>(j) / p.degree_v();
    p.control_point(i, j) += distance * *unit_normal(e.derivatives(u, v));
    return p;
}

BezierPatch symmetric_panel() {
    std::vector<Vec3> points;
    for (int i = 0; i <= 3; ++i) {
        const double x = 1000.0 * i / 3.0;
        const double z0 = 60.0 * std::sin(2.0 * i / 3.0);
        points.emplace_back(x, 0.0, z0);
        points.emplace_back(x, 200.0, z0);
        points.emplace_back(x + 15.0 * i, 400.0, z0 - 35.0 + 6.0 * i);
        points.emplace_back(x, 600.0, z0 - 90.0);
    }
    return BezierPatch(3, 3, std::move(points));
}

std::pair<BezierPatch, BezierPatch> hood_pair() {
    // u runs outwards from the centre line (y), v runs rearwards (x). Row i = 1
    // copies row 0's x and z, which is what makes UMin mirror-G1; the drop
    // towards the sides starts at row 2.
    constexpr std::array<double, 4> y{0.0, 230.0, 470.0, 700.0};
    constexpr std::array<double, 4> side_drop{0.0, 0.0, 22.0, 70.0};
    std::vector<Vec3> front;
    for (std::size_t i = 0; i <= 3; ++i) {
        for (std::size_t j = 0; j <= 3; ++j) {
            const double t = static_cast<double>(j) / 3.0;
            const double x = 600.0 * t;
            const double z =
                800.0 + 25.0 * t + 12.0 * std::sin(std::numbers::pi * t) - side_drop[i];
            front.emplace_back(x, y[i], z);
        }
    }
    const BezierPatch a(3, 3, std::move(front));

    // C2 continuation: B0 = A3, B1 = 2 A3 - A2, B2 = A1 - 4 A2 + 4 A3. The
    // coefficients in each row sum to 1, so rows 0 and 1 keep differing by the
    // same y offset only, and the rear patch is mirror-G1 too.
    std::vector<Vec3> rear(16);
    for (std::size_t i = 0; i <= 3; ++i) {
        const Vec3 a1 = a.control_point(i, 1);
        const Vec3 a2 = a.control_point(i, 2);
        const Vec3 a3 = a.control_point(i, 3);
        const Vec3 b1 = 2.0 * a3 - a2;
        const Vec3 b2 = a1 - 4.0 * a2 + 4.0 * a3;
        rear[i * 4 + 0] = a3;
        rear[i * 4 + 1] = b1;
        rear[i * 4 + 2] = b2;
        rear[i * 4 + 3] = 2.0 * b2 - b1 + Vec3(0.0, 0.0, -6.0);
    }
    return {a, BezierPatch(3, 3, std::move(rear))};
}

Scene hood_scene() {
    auto [front, rear] = hood_pair();
    Scene scene;
    scene.patches.push_back({"front", std::move(front)});
    scene.patches.push_back({"rear", std::move(rear)});
    scene.edges.push_back({{"front", Edge::VMax}, {"rear", Edge::VMin}, Continuity::G2});
    scene.symmetry.push_back({{"front", Edge::UMin}, Continuity::G2});
    scene.symmetry.push_back({{"rear", Edge::UMin}, Continuity::G2});
    return scene;
}

Scene hood_scene_g2_broken() {
    Scene scene = hood_scene();
    scene.patches[1].patch.control_point(2, 2).z() += 4.0;
    return scene;
}

Scene hood_scene_g1_only() {
    Scene scene = hood_scene();
    // Every row-2 point moves by the same amount, so rows 0 and 1 of the
    // centre-line edge still differ only in y: the mirror join stays G1.
    for (std::size_t i = 0; i <= 3; ++i) {
        scene.patches[1].patch.control_point(i, 2).z() += 15.0;
    }
    return scene;
}

} // namespace ada::test
