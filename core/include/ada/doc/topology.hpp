#pragma once

// Which patch edges meet, from the geometry alone. The scene's links say which
// edges must join; these functions say which ones do touch, so a tool can
// check a requested link before making it, or find the patch across an edge
// the user picked.

#include "ada/doc/scene.hpp"

#include <vector>

namespace ada {

/// The largest control-point coordinate magnitude over all patches (mm, at
/// least 1): the length that relative tolerances scale with.
double model_size(const Scene& scene);

/// How far apart two patch edges are: the largest distance from a sample of
/// either boundary to the other boundary curve (mm). Zero for edges that
/// coincide, whichever way they run. Throws `std::invalid_argument` for an
/// unknown patch.
double edge_gap(const Scene& scene, const EdgeRef& a, const EdgeRef& b);

/// The largest |y| along an edge (mm): how far it is from the centre plane.
double centre_plane_distance(const Scene& scene, const EdgeRef& edge);

/// Edges of other patches lying along `edge`, to within `tol::kEdgeContact`
/// of the model size.
std::vector<EdgeRef> touching_edges(const Scene& scene, const EdgeRef& edge);

/// Whether `edge` lies in Y = 0, to the same tolerance.
bool on_centre_plane(const Scene& scene, const EdgeRef& edge);

} // namespace ada
