#include "ada/doc/topology.hpp"

#include "ada/analysis/sampling.hpp"
#include "ada/geom/closest_point.hpp"
#include "ada/tolerances.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace ada {

namespace {

/// Samples per boundary. These checks tell the right edge from the wrong one
/// (hundreds of millimetres apart); the quality report measures G0 properly.
constexpr std::size_t kSamples = 17;

constexpr std::array<Edge, 4> kEdges{Edge::UMin, Edge::UMax, Edge::VMin, Edge::VMax};

BezierCurve boundary_of(const Scene& scene, const EdgeRef& ref) {
    return scene.patch(ref.patch).patch.boundary(ref.edge);
}

/// Largest distance from a sample of `from` to the curve `to`.
double one_way(const BezierCurve& from, const BezierCurve& to) {
    double worst = 0.0;
    for (const double t : uniform_parameters(kSamples)) {
        worst = std::max(worst, closest_point(to, from.evaluate(t)).distance);
    }
    return worst;
}

} // namespace

double model_size(const Scene& scene) {
    double size = 1.0;
    for (const ScenePatch& p : scene.patches) {
        for (const Vec3& point : p.patch.control_points()) {
            size = std::max(size, point.cwiseAbs().maxCoeff());
        }
    }
    return size;
}

double edge_gap(const Scene& scene, const EdgeRef& a, const EdgeRef& b) {
    const BezierCurve ca = boundary_of(scene, a);
    const BezierCurve cb = boundary_of(scene, b);
    return std::max(one_way(ca, cb), one_way(cb, ca));
}

double centre_plane_distance(const Scene& scene, const EdgeRef& edge) {
    const BezierCurve curve = boundary_of(scene, edge);
    double worst = 0.0;
    for (const double t : uniform_parameters(kSamples)) {
        worst = std::max(worst, std::abs(curve.evaluate(t).y()));
    }
    return worst;
}

std::vector<EdgeRef> touching_edges(const Scene& scene, const EdgeRef& edge) {
    static_cast<void>(scene.patch(edge.patch)); // an unknown id throws even with no other patch
    const double tolerance = tol::kEdgeContact * model_size(scene);
    std::vector<EdgeRef> touching;
    for (const ScenePatch& p : scene.patches) {
        if (p.id == edge.patch) {
            continue;
        }
        for (const Edge e : kEdges) {
            if (edge_gap(scene, edge, {p.id, e}) < tolerance) {
                touching.push_back({p.id, e});
            }
        }
    }
    return touching;
}

bool on_centre_plane(const Scene& scene, const EdgeRef& edge) {
    return centre_plane_distance(scene, edge) < tol::kEdgeContact * model_size(scene);
}

} // namespace ada
