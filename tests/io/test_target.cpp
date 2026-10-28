#include "ada/io/target.hpp"

#include <Eigen/Geometry>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using ada::TargetMesh;
using ada::Vec3;

namespace {

std::filesystem::path temp_file(const std::string& name, const std::string& contents) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("ada-target-" + name);
    std::ofstream(path, std::ios::binary) << contents;
    return path;
}

void put(std::string& out, const void* data, std::size_t size) {
    out.append(static_cast<const char*>(data), size);
}

template<typename T>
void put(std::string& out, T value) {
    put(out, &value, sizeof(T));
}

/// The unit test square, 100 x 100 mm in z = 0, as two triangles.
const std::vector<Vec3> kSquare{{0, 0, 0}, {100, 0, 0}, {100, 100, 0}, {0, 100, 0}};

void check_square(const TargetMesh& mesh) {
    REQUIRE(mesh.triangles.size() == 2);
    double area = 0.0;
    for (const auto& t : mesh.triangles) {
        const Vec3 a = mesh.vertices[t[0]];
        const Vec3 b = mesh.vertices[t[1]];
        const Vec3 c = mesh.vertices[t[2]];
        area += 0.5 * (b - a).cross(c - a).norm();
    }
    CHECK(std::abs(area - 10000.0) < 1e-9);
}

} // namespace

TEST_CASE("OBJ targets: vertices and faces, polygons fanned into triangles", "[io][target]") {
    const auto path = temp_file("square.obj",
                                "# a square\nv 0 0 0\nv 100 0 0\nv 100 100 0\nv 0 100 0\n"
                                "vn 0 0 1\nvt 0 0\nf 1/1/1 2/1/1 3/1/1 -1/1/1\n");
    const TargetMesh mesh = ada::read_target_mesh(path);
    CHECK(mesh.vertices.size() == 4);
    check_square(mesh);
    std::filesystem::remove(path);
}

TEST_CASE("STL targets, ASCII and binary", "[io][target]") {
    const auto ascii =
        temp_file("square-ascii.stl",
                  "solid square\n"
                  " facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 100 0 0\n"
                  "   vertex 100 100 0\n  endloop\n endfacet\n"
                  " facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 100 100 0\n"
                  "   vertex 0 100 0\n  endloop\n endfacet\nendsolid square\n");
    check_square(ada::read_target_mesh(ascii));
    std::filesystem::remove(ascii);

    // Binary STL whose 80-byte header happens to start with "solid", which
    // only the file size tells apart from ASCII.
    std::string binary = "solid but binary";
    binary.resize(80, ' ');
    put<std::uint32_t>(binary, 2);
    for (const auto& tri : std::vector<std::array<int, 3>>{{0, 1, 2}, {0, 2, 3}}) {
        for (const float f : {0.0f, 0.0f, 1.0f}) {
            put(binary, f);
        }
        for (const int k : tri) {
            for (int axis = 0; axis < 3; ++axis) {
                put(binary, static_cast<float>(kSquare[static_cast<std::size_t>(k)][axis]));
            }
        }
        put<std::uint16_t>(binary, 0);
    }
    const auto bin = temp_file("square-binary.stl", binary);
    check_square(ada::read_target_mesh(bin));
    std::filesystem::remove(bin);
}

TEST_CASE("PLY targets, ASCII and binary, skipping properties they do not need", "[io][target]") {
    const std::string header_ascii = "ply\nformat ascii 1.0\ncomment made by hand\n"
                                     "element vertex 4\nproperty float x\nproperty float y\n"
                                     "property float z\nproperty uchar red\n"
                                     "element face 1\nproperty list uchar int vertex_indices\n"
                                     "end_header\n";
    const auto ascii =
        temp_file("square-ascii.ply",
                  header_ascii + "0 0 0 9\n100 0 0 9\n100 100 0 9\n0 100 0 9\n4 0 1 2 3\n");
    check_square(ada::read_target_mesh(ascii));
    std::filesystem::remove(ascii);

    std::string binary = "ply\nformat binary_little_endian 1.0\nelement vertex 4\n"
                         "property double x\nproperty double y\nproperty double z\n"
                         "property float nx\nelement face 2\n"
                         "property list uchar uint vertex_indices\nelement edge 1\n"
                         "property int vertex1\nproperty int vertex2\nend_header\n";
    for (const Vec3& p : kSquare) {
        put(binary, p.x());
        put(binary, p.y());
        put(binary, p.z());
        put(binary, 1.0f);
    }
    for (const auto& tri : std::vector<std::array<std::uint32_t, 3>>{{0, 1, 2}, {0, 2, 3}}) {
        put<std::uint8_t>(binary, 3);
        for (const std::uint32_t k : tri) {
            put(binary, k);
        }
    }
    put<std::int32_t>(binary, 0);
    put<std::int32_t>(binary, 1);
    const auto bin = temp_file("square-binary.ply", binary);
    check_square(ada::read_target_mesh(bin));
    std::filesystem::remove(bin);

    const auto cloud =
        temp_file("cloud.ply",
                  "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\n"
                  "property float y\nproperty float z\nend_header\n0 0 0\n1 2 3\n4 5 6\n");
    const TargetMesh points = ada::read_target_mesh(cloud);
    CHECK(points.vertices.size() == 3);
    CHECK(points.triangles.empty());
    std::filesystem::remove(cloud);
}

TEST_CASE("unreadable targets say why", "[io][target]") {
    CHECK_THROWS_AS(ada::read_target_mesh("no/such/file.obj"), std::invalid_argument);
    const auto unknown = temp_file("target.xyz", "1 2 3\n");
    CHECK_THROWS_AS(ada::read_target_mesh(unknown), std::invalid_argument);
    std::filesystem::remove(unknown);
    const auto big_endian = temp_file("big.ply",
                                      "ply\nformat binary_big_endian 1.0\nelement vertex 0\n"
                                      "property float x\nend_header\n");
    CHECK_THROWS_AS(ada::read_target_mesh(big_endian), std::invalid_argument);
    std::filesystem::remove(big_endian);
}

TEST_CASE("sampling spreads points evenly over the triangles, without duplicates", "[io][target]") {
    TargetMesh mesh;
    mesh.vertices = kSquare;
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}};
    const std::vector<Vec3> points = ada::sample_target(mesh, 10.0);

    // Each triangle's longest edge (141 mm) is cut into 15, giving 225 small
    // triangles, each sampled once at its centroid.
    CHECK(points.size() == 2 * 15 * 15);
    std::set<std::tuple<double, double, double>> unique;
    for (const Vec3& p : points) {
        CHECK(p.x() > 0.0);
        CHECK(p.x() < 100.0);
        CHECK(p.y() > 0.0);
        CHECK(p.y() < 100.0);
        CHECK(p.z() == 0.0);
        unique.insert({p.x(), p.y(), p.z()});
    }
    CHECK(unique.size() == points.size());

    // A point cloud is used as it is.
    TargetMesh cloud;
    cloud.vertices = kSquare;
    CHECK(ada::sample_target(cloud, 10.0).size() == 4);
    CHECK_THROWS_AS(ada::sample_target(mesh, 0.0), std::invalid_argument);
}

namespace {

/// A 2 x 2 grid of quads (as triangles) on the plane z = 0.1 x + 0.2 y, over
/// [0, 100] x [0, 100]: nine shared vertices, an outline of eight.
TargetMesh tilted_grid() {
    TargetMesh mesh;
    for (int j = 0; j <= 2; ++j) {
        for (int i = 0; i <= 2; ++i) {
            const double x = 50.0 * i;
            const double y = 50.0 * j;
            mesh.vertices.emplace_back(x, y, 0.1 * x + 0.2 * y);
        }
    }
    auto at = [](int i, int j) { return static_cast<std::uint32_t>(3 * j + i); };
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    }
    return mesh;
}

} // namespace

TEST_CASE("a mesh's outline is its boundary loop, also with STL's repeated vertices",
          "[io][target]") {
    const TargetMesh mesh = tilted_grid();
    const auto loops = ada::mesh_outline(mesh);
    REQUIRE(loops.size() == 1);
    CHECK(loops[0].size() == 8);
    for (const Vec3& p : loops[0]) {
        CHECK((p.x() == 0.0 || p.x() == 100.0 || p.y() == 0.0 || p.y() == 100.0));
    }

    // The same triangles with every corner repeated per triangle, as STL has them.
    TargetMesh soup;
    for (const auto& t : mesh.triangles) {
        const auto first = static_cast<std::uint32_t>(soup.vertices.size());
        for (const std::uint32_t k : t) {
            soup.vertices.push_back(mesh.vertices[k]);
        }
        soup.triangles.push_back({first, first + 1, first + 2});
    }
    const auto merged = ada::mesh_outline(soup);
    REQUIRE(merged.size() == 1);
    CHECK(merged[0].size() == 8);

    CHECK(ada::mesh_outline(TargetMesh{}).empty());
}

TEST_CASE("heights along an axis are read off the mesh", "[io][target]") {
    const TargetMesh mesh = tilted_grid();
    const std::optional<double> z = ada::mesh_height(mesh, 2, 63.0, 21.0);
    REQUIRE(z.has_value());
    CHECK(std::abs(*z - (0.1 * 63.0 + 0.2 * 21.0)) < 1e-12);
    CHECK_FALSE(ada::mesh_height(mesh, 2, 150.0, 20.0).has_value());
    // Along x: where the line (y, z) = (50, 12) meets the plane, x = 20.
    const std::optional<double> x = ada::mesh_height(mesh, 0, 50.0, 12.0);
    REQUIRE(x.has_value());
    CHECK(std::abs(*x - 20.0) < 1e-12);
}
