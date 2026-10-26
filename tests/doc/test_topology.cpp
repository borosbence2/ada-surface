#include "ada/doc/topology.hpp"

#include "support/panels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using ada::Edge;
using ada::EdgeRef;
using ada::Scene;
using ada::Vec3;
using namespace ada::test;

namespace {

/// Edges that coincide measure as zero up to closest-point rounding.
constexpr double kCoincidentMm = 1e-9;

} // namespace

TEST_CASE("edge_gap is zero where two patches share an edge, whichever way they run",
          "[doc][topology]") {
    Scene scene = hood_scene();
    CHECK(ada::edge_gap(scene, {"front", Edge::VMax}, {"rear", Edge::VMin}) < kCoincidentMm);
    CHECK(ada::edge_gap(scene, {"rear", Edge::VMin}, {"front", Edge::VMax}) < kCoincidentMm);

    scene.patches[1].patch = flip_u(scene.patches[1].patch);
    CHECK(ada::edge_gap(scene, {"front", Edge::VMax}, {"rear", Edge::VMin}) < kCoincidentMm);
}

TEST_CASE("edge_gap measures how far apart two edges are", "[doc][topology]") {
    Scene scene = hood_scene();
    std::vector<Vec3> lifted = scene.patches[1].patch.control_points();
    for (Vec3& p : lifted) {
        p.z() += 5.0;
    }
    scene.patches[1].patch = ada::BezierPatch(3, 3, lifted);
    // The closest point on a lifted curve is at most the lift away, and close
    // to it on a gently curved edge.
    const double gap = ada::edge_gap(scene, {"front", Edge::VMax}, {"rear", Edge::VMin});
    CHECK(gap > 4.0);
    CHECK(gap <= 5.0 + kCoincidentMm);

    // The front's opposite edge is the length of the front patch away.
    CHECK(ada::edge_gap(scene, {"front", Edge::VMin}, {"rear", Edge::VMin}) > 500.0);
}

TEST_CASE("touching_edges finds the patch across an edge", "[doc][topology]") {
    const Scene scene = hood_scene();
    const std::vector<EdgeRef> across = ada::touching_edges(scene, {"front", Edge::VMax});
    REQUIRE(across.size() == 1);
    CHECK(across[0].patch == "rear");
    CHECK(across[0].edge == Edge::VMin);

    CHECK(ada::touching_edges(scene, {"front", Edge::UMax}).empty());
    CHECK_THROWS_AS(ada::touching_edges(scene, {"nope", Edge::UMax}), std::invalid_argument);
}

TEST_CASE("centre-plane distance is the largest |y| along an edge", "[doc][topology]") {
    const Scene scene = hood_scene();
    CHECK(ada::centre_plane_distance(scene, {"front", Edge::UMin}) < kCoincidentMm);
    CHECK(ada::on_centre_plane(scene, {"front", Edge::UMin}));
    CHECK(ada::centre_plane_distance(scene, {"front", Edge::UMax}) > 600.0);
    CHECK_FALSE(ada::on_centre_plane(scene, {"front", Edge::UMax}));
}

TEST_CASE("model size is the largest coordinate, at least 1 mm", "[doc][topology]") {
    CHECK(ada::model_size(Scene{}) == 1.0);
    const Scene scene = hood_scene();
    CHECK(ada::model_size(scene) > 1000.0);
}
