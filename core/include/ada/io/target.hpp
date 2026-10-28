#pragma once

// Fit targets: the shape a fitted surface should follow, read from a mesh or
// point-cloud file and turned into points.

#include "ada/geom/vec.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace ada {

/// Vertices and triangles in the file's own units. A point cloud has no
/// triangles.
struct TargetMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

/// Reads OBJ (`v` and `f` records; polygons fanned into triangles; texture and
/// normal indices ignored), STL (ASCII or binary) and PLY (ASCII or binary
/// little-endian; vertex `x`, `y`, `z`; faces optional; other elements and
/// properties skipped). Throws `std::invalid_argument` naming the file and
/// the problem.
TargetMesh read_target_mesh(const std::filesystem::path& path);

/// The points a fit uses. For a point cloud, its vertices. For a mesh, one
/// point per small triangle when each triangle is cut evenly into small ones
/// whose edges are at most `spacing` long: an even density over the surface,
/// whatever the tessellation, and no point twice where triangles share an
/// edge. Throws `std::invalid_argument` unless `spacing > 0`.
std::vector<Vec3> sample_target(const TargetMesh& mesh, double spacing);

/// The mesh's boundary loops, each as an ordered list of vertices: edges used
/// by one triangle only. Vertices at the same position are merged first, so
/// STL (which repeats every corner) works too. Empty for a point cloud or a
/// closed mesh.
std::vector<std::vector<Vec3>> mesh_outline(const TargetMesh& mesh);

/// Where the line through (a, b) along `axis` (0 x, 1 y, 2 z; a and b are the
/// other two coordinates in order) meets the mesh: that coordinate, the
/// largest if it meets the mesh more than once (the top of a hood, the
/// outside of a door). Nullopt where it misses.
std::optional<double> mesh_height(const TargetMesh& mesh, int axis, double a, double b);

} // namespace ada
