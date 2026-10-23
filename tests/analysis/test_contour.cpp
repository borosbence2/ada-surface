#include "ada/analysis/contour.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using ada::ContourLine;
using ada::ScalarField;

namespace {

/// Crossings are refined on the exact field, so every contour point satisfies
/// `f = level` to rounding, not to the grid's linear interpolation error.
constexpr double kOnLevelSet = 1e-10;

bool all_on_level(const ContourLine& line, const ScalarField& f) {
    for (const Eigen::Vector2d& p : line.uv) {
        if (std::abs(f(p.x(), p.y()) - line.level) > kOnLevelSet) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("contours of a linear field are straight lines across the square",
          "[analysis][contour]") {
    const ScalarField f = [](double u, double) { return u; };
    const auto lines = ada::extract_contours(ada::sample_field(f, 9, 9), f, {0.3, 0.55});
    REQUIRE(lines.size() == 2);
    for (const ContourLine& line : lines) {
        CHECK_FALSE(line.closed);
        CHECK(line.uv.size() == 9); // one crossing per grid row
        CHECK(all_on_level(line, f));
        const double v0 = line.uv.front().y();
        const double v1 = line.uv.back().y();
        CHECK(std::min(v0, v1) == 0.0);
        CHECK(std::max(v0, v1) == 1.0);
    }
}

TEST_CASE("a circular level set comes back as one closed loop", "[analysis][contour]") {
    const ScalarField f = [](double u, double v) {
        return (u - 0.5) * (u - 0.5) + (v - 0.5) * (v - 0.5);
    };
    const auto lines = ada::extract_contours(ada::sample_field(f, 21, 21), f, {0.09});
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].closed);
    CHECK(lines[0].uv.size() > 20);
    CHECK(all_on_level(lines[0], f));
}

TEST_CASE("saddle cells are resolved by the centre value", "[analysis][contour]") {
    // (u - 1/2)(v - 1/2) = 0.001 has one branch in the lower-left quadrant and
    // one in the upper-right. On a 4 x 4 grid the middle cell is a saddle; the
    // wrong pairing would join the two branches through it.
    const ScalarField f = [](double u, double v) { return (u - 0.5) * (v - 0.5); };
    const auto lines = ada::extract_contours(ada::sample_field(f, 4, 4), f, {0.001});
    REQUIRE(lines.size() == 2);
    for (const ContourLine& line : lines) {
        CHECK(all_on_level(line, f));
        const bool lower_left = line.uv.front().x() < 0.5;
        for (const Eigen::Vector2d& p : line.uv) {
            CHECK((p.x() < 0.5) == lower_left);
            CHECK((p.y() < 0.5) == lower_left);
        }
    }
}

TEST_CASE("undefined samples split a contour", "[analysis][contour]") {
    const ScalarField f = [](double u, double v) {
        return u > 0.45 && u < 0.55 ? std::numeric_limits<double>::quiet_NaN() : v;
    };
    const auto lines = ada::extract_contours(ada::sample_field(f, 11, 11), f, {0.35});
    REQUIRE(lines.size() == 2);
    for (const ContourLine& line : lines) {
        CHECK(all_on_level(line, f));
    }
}
