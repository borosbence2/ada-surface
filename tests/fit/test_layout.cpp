#include "ada/fit/layout.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

using ada::NetworkLayout;
using ada::TargetMesh;
using ada::Vec3;

namespace {

constexpr double kGrid = 25.0;   // mm between mesh vertices
constexpr double kWidth = 300.0; // x
constexpr double kDepth = 200.0; // y

/// The oracle: a tilted plane, so every point the layout puts on the mesh
/// can be checked exactly.
double plane_z(double x, double y) {
    return 50.0 + 0.1 * x + 0.2 * y;
}

/// The plane over [0, 300] x [0, 200], meshed at 25 mm, without the cells
/// listed in `holes` (as (i, j) cell indices).
TargetMesh plane_mesh(const std::set<std::pair<int, int>>& holes = {}) {
    const int nx = static_cast<int>(kWidth / kGrid);
    const int ny = static_cast<int>(kDepth / kGrid);
    TargetMesh mesh;
    for (int j = 0; j <= ny; ++j) {
        for (int i = 0; i <= nx; ++i) {
            const double x = kGrid * i;
            const double y = kGrid * j;
            mesh.vertices.emplace_back(x, y, plane_z(x, y));
        }
    }
    auto at = [&](int i, int j) { return static_cast<std::uint32_t>((nx + 1) * j + i); };
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            if (holes.count({i, j}) > 0) {
                continue;
            }
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    }
    return mesh;
}

Vec3 on_plane(double x, double y) {
    return {x, y, plane_z(x, y)};
}

/// Corners in the order (0, 0), (300, 0), (300, 200), (0, 200).
std::array<Vec3, 4> plane_corners() {
    return {on_plane(0, 0), on_plane(kWidth, 0), on_plane(kWidth, kDepth), on_plane(0, kDepth)};
}

bool on_the_plane(const Vec3& p) {
    return std::abs(p.z() - plane_z(p.x(), p.y())) < 1e-9;
}

} // namespace

TEST_CASE("a target's outline has its corners where it turns sharply", "[fit][layout]") {
    const auto outlines = ada::target_outlines(plane_mesh());
    REQUIRE(outlines.size() == 1);
    const ada::TargetOutline& outline = outlines[0];
    CHECK(outline.points.size() == 2 * (12 + 8));
    const double expected = 2.0 * (kWidth * std::sqrt(1.0 + 0.01) + kDepth * std::sqrt(1.0 + 0.04));
    CHECK(std::abs(outline.length - expected) < 1e-9);
    REQUIRE(outline.corners.size() == 4);
    std::set<std::pair<double, double>> found;
    for (const ada::OutlineCorner& c : outline.corners) {
        CHECK(c.turn_degrees > 80.0);
        CHECK(c.turn_degrees < 100.0);
        found.insert({outline.points[c.index].x(), outline.points[c.index].y()});
    }
    CHECK(found == std::set<std::pair<double, double>>{
                       {0.0, 0.0}, {kWidth, 0.0}, {kWidth, kDepth}, {0.0, kDepth}});
    for (std::size_t k = 1; k < outline.corners.size(); ++k) {
        CHECK(outline.corners[k].index > outline.corners[k - 1].index);
    }
}

TEST_CASE("a target faces the axis it shows the most area along", "[fit][layout]") {
    TargetMesh mesh = plane_mesh();
    CHECK(ada::facing_axis(mesh) == 2);
    for (Vec3& v : mesh.vertices) {
        v = Vec3(v.x(), v.z(), v.y()); // stand it up, facing y
    }
    CHECK(ada::facing_axis(mesh) == 1);
    CHECK_THROWS_AS(ada::facing_axis(TargetMesh{}), std::invalid_argument);
}

TEST_CASE("a network laid out on a target follows its outline and lies on it", "[fit][layout]") {
    const TargetMesh mesh = plane_mesh();
    std::array<Vec3, 4> corners = plane_corners();
    corners[0] += Vec3(1.0, -2.0, 0.0); // a corner given roughly is taken at the outline's
    const NetworkLayout layout = ada::layout_network(mesh, corners, 2, 3);

    CHECK(layout.axis == 2);
    CHECK(layout.corners[0] == on_plane(0, 0));
    CHECK(std::abs(layout.corner_offsets[0] - std::sqrt(5.0)) < 1e-12);
    CHECK(layout.corner_offsets[1] == 0.0);
    REQUIRE(layout.along_u.size() == 3);
    REQUIRE(layout.along_v.size() == 4);

    // Nodes: the sides cut into equal lengths, (100, 100) and (200, 100) inside.
    auto node = [&](std::size_t r, std::size_t c) {
        return c < 3 ? layout.along_u[r][c].front() : layout.along_u[r][2].back();
    };
    for (std::size_t r = 0; r <= 2; ++r) {
        REQUIRE(layout.along_u[r].size() == 3);
        for (std::size_t c = 0; c <= 3; ++c) {
            INFO("node (" << r << ", " << c << ")");
            const Vec3 expected =
                on_plane(100.0 * static_cast<double>(c), 100.0 * static_cast<double>(r));
            CHECK((node(r, c) - expected).norm() < 1e-9);
        }
    }
    // The lines meet exactly at the nodes.
    for (std::size_t c = 0; c <= 3; ++c) {
        REQUIRE(layout.along_v[c].size() == 2);
        for (std::size_t r = 0; r < 2; ++r) {
            CHECK(layout.along_v[c][r].front() == node(r, c));
            CHECK(layout.along_v[c][r].back() == node(r + 1, c));
        }
    }
    for (std::size_t r = 0; r <= 2; ++r) {
        for (std::size_t c = 0; c + 1 < 3; ++c) {
            CHECK(layout.along_u[r][c].back() == layout.along_u[r][c + 1].front());
        }
    }

    // Outer lines go through every outline vertex: four 25 mm steps per
    // 100 mm piece. Inner lines through nine points on the target.
    CHECK(layout.along_u[0][0].size() == 5);
    CHECK(layout.along_v[3][1].size() == 5);
    for (const Vec3& p : layout.along_u[2][1]) {
        CHECK(p.y() == kDepth);
    }
    CHECK(layout.along_u[1][0].size() == 9);
    CHECK(layout.along_v[1][0].size() == 9);
    for (const auto& lines : {layout.along_u, layout.along_v}) {
        for (const auto& line : lines) {
            for (const auto& curve : line) {
                for (const Vec3& p : curve) {
                    CHECK(on_the_plane(p));
                }
            }
        }
    }
}

TEST_CASE("corners may go round the outline either way, but in order", "[fit][layout]") {
    const TargetMesh mesh = plane_mesh();
    const std::array<Vec3, 4> c = plane_corners();
    // The other way round: the first row line now runs up x = 0.
    const NetworkLayout turned = ada::layout_network(mesh, {c[0], c[3], c[2], c[1]}, 3, 2);
    for (const Vec3& p : turned.along_u[0][0]) {
        CHECK(p.x() == 0.0);
    }
    CHECK(turned.along_u[0][1].back() == c[3]);

    CHECK_THROWS_AS(ada::layout_network(mesh, {c[0], c[2], c[1], c[3]}, 2, 2),
                    std::invalid_argument);
    CHECK_THROWS_AS(ada::layout_network(mesh, {c[0], c[0], c[2], c[3]}, 2, 2),
                    std::invalid_argument);
}

TEST_CASE("a layout the target cannot carry is refused", "[fit][layout]") {
    const TargetMesh mesh = plane_mesh();
    const std::array<Vec3, 4> c = plane_corners();
    // Eight 25 mm steps up the 200 mm sides cannot be cut into nine pieces.
    CHECK_THROWS_AS(ada::layout_network(mesh, c, 9, 2), std::invalid_argument);
    CHECK_THROWS_AS(ada::layout_network(mesh, c, 0, 2), std::invalid_argument);
    CHECK_THROWS_AS(ada::layout_network(mesh, c, 2, 2, 1), std::invalid_argument);

    // A hole where the line y = 100 crosses x 50..75 (the cells either side
    // of it): the section from node (1, 0) to (1, 1) leaves the target.
    const TargetMesh holed = plane_mesh({{2, 3}, {2, 4}});
    CHECK(ada::target_outlines(holed).size() == 2);
    CHECK_THROWS_AS(ada::layout_network(holed, c, 2, 3), std::invalid_argument);

    // Corners on two separate pieces.
    TargetMesh two = plane_mesh();
    const TargetMesh other = plane_mesh();
    const auto offset = static_cast<std::uint32_t>(two.vertices.size());
    for (const Vec3& v : other.vertices) {
        two.vertices.push_back(v + Vec3(1000.0, 0.0, 0.0));
    }
    for (auto t : other.triangles) {
        two.triangles.push_back({t[0] + offset, t[1] + offset, t[2] + offset});
    }
    const std::array<Vec3, 4> split{
        c[0], c[1], c[2] + Vec3(1000.0, 0.0, 0.0), c[3] + Vec3(1000.0, 0.0, 0.0)};
    CHECK_THROWS_AS(ada::layout_network(two, split, 2, 2), std::invalid_argument);
}
