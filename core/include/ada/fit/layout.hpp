#pragma once

// Curve networks laid out on a fit target: the target's outline with its
// corners, and a grid of curves over it whose outer lines follow the outline
// and whose inner lines are sections through the target. The curves come out
// as points for `curve_through_points`; which corners and how many rows and
// columns stay the caller's choice.

#include "ada/geom/vec.hpp"
#include "ada/io/target.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace ada {

/// Where an outline turns by more than this, in degrees, `target_outlines`
/// marks a corner: a panel's corners turn by 60 to 120 degrees, a smooth
/// edge by a few degrees per tessellation vertex.
inline constexpr double kOutlineCornerDegrees = 45.0;

/// Points on each inner line of a network laid out on a target: as many as
/// the L3 hood's crown needs for a cubic to follow it.
inline constexpr int kDefaultSectionPoints = 9;

struct OutlineCorner {
    std::size_t index = 0;     ///< into `TargetOutline::points`
    double turn_degrees = 0.0; ///< between the outline before and after it
};

/// One boundary loop of a target mesh.
struct TargetOutline {
    std::vector<Vec3> points;           ///< in order around the loop, each once
    std::vector<OutlineCorner> corners; ///< in loop order
    double length = 0.0;                ///< mm, once around
};

/// The mesh's boundary loops (`mesh_outline`) with their corners.
std::vector<TargetOutline> target_outlines(const TargetMesh& mesh);

/// The axis (0 x, 1 y, 2 z) along which the mesh shows the largest projected
/// area: z for a hood or roof, y for a door. Throws `std::invalid_argument`
/// for a mesh without triangles.
int facing_axis(const TargetMesh& mesh);

/// A grid of curves over a target, in `NetworkSpec`'s layout: node (r, c)
/// for r = 0..rows, c = 0..cols.
struct NetworkLayout {
    /// Lines of constant v: `along_u[r][c]` runs from node (r, c) to (r, c + 1).
    std::vector<std::vector<std::vector<Vec3>>> along_u;
    /// Lines of constant u: `along_v[c][r]` runs from node (r, c) to (r + 1, c).
    std::vector<std::vector<std::vector<Vec3>>> along_v;
    /// The outline vertex taken for each given corner, and how far it lies
    /// from the point given, mm.
    std::array<Vec3, 4> corners{};
    std::array<double, 4> corner_offsets{};
    int axis = 2; ///< the plan view: `facing_axis`
};

/// Lays a rows x cols network over a target with one outline loop around
/// four corners, each taken as the outline vertex nearest the point given.
/// corners[0] -> corners[1] is the first row line (u grows along it),
/// corners[1] -> corners[2] the last column line, corners[3] -> corners[2] the
/// last row line and corners[0] -> corners[3] the first column line. These
/// outer lines pass through every outline vertex between their nodes, which
/// cut each side at the vertices nearest to equal lengths. Inner nodes are
/// placed in plan (seen along `facing_axis`) by Coons interpolation of the
/// outer ones and lie on the target; inner lines run straight in plan,
/// through `section_points` points on the target. Throws
/// `std::invalid_argument` when the corners are not on one outline loop in
/// order around it, a side has too few vertices for its pieces, or a line
/// leaves the target.
NetworkLayout layout_network(const TargetMesh& mesh,
                             const std::array<Vec3, 4>& corners,
                             int rows,
                             int cols,
                             int section_points = kDefaultSectionPoints);

} // namespace ada
