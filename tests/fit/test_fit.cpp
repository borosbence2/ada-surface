#include "ada/analysis/differential.hpp"
#include "ada/fit/fit.hpp"
#include "ada/geom/patch_evaluator.hpp"

#include "support/panels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using ada::BezierPatch;
using ada::FitAssignment;
using ada::Vec3;

namespace {

constexpr double kOffsetMm = 2.0;
constexpr double kSameParameter = 1e-8;
constexpr double kSameMm = 1e-8;

struct Sample {
    std::size_t patch;
    double u;
    double v;
    double offset;
};

} // namespace

TEST_CASE("target points go to the patch they lie on, with their footpoint", "[fit]") {
    const auto [front, rear] = ada::test::hood_pair();
    const std::vector<BezierPatch> patches{front, rear};
    std::vector<Sample> samples;
    std::vector<Vec3> points;
    for (std::size_t p = 0; p < 2; ++p) {
        const ada::PatchEvaluator evaluator(patches[p]);
        for (const double u : {0.2, 0.5, 0.8}) {
            for (const double v : {0.1, 0.5, 0.95}) {
                const double offset = (u < 0.5 ? 1.0 : -1.0) * kOffsetMm;
                const ada::PatchDerivatives d = evaluator.derivatives(u, v);
                points.push_back(d.position() + offset * *ada::unit_normal(d));
                samples.push_back({p, u, v, offset});
            }
        }
    }
    // Far in front of the hood: on no patch.
    points.emplace_back(-300.0, 300.0, 800.0);

    const std::vector<FitAssignment> first = ada::assign_points(patches, points);
    REQUIRE(first.size() == points.size());
    for (std::size_t k = 0; k < samples.size(); ++k) {
        INFO("sample " << k);
        CHECK_FALSE(first[k].outside);
        CHECK(first[k].patch == samples[k].patch);
        CHECK(std::abs(first[k].u - samples[k].u) < kSameParameter);
        CHECK(std::abs(first[k].v - samples[k].v) < kSameParameter);
        CHECK(std::abs(first[k].signed_distance - samples[k].offset) < kSameMm);
    }
    CHECK(first.back().outside);

    // Starting from the last assignment changes nothing for unchanged patches.
    const std::vector<FitAssignment> again = ada::assign_points(patches, points, &first);
    for (std::size_t k = 0; k < samples.size(); ++k) {
        CHECK(again[k].patch == first[k].patch);
        CHECK(std::abs(again[k].u - first[k].u) < kSameParameter);
    }
}

TEST_CASE("fit statistics: max, RMS and 95th percentile over the points inside", "[fit]") {
    std::vector<FitAssignment> a;
    for (int k = 1; k <= 20; ++k) {
        a.push_back({0, 0.5, 0.5, Vec3::Zero(), static_cast<double>(k) / 10.0, false});
    }
    a.push_back({0, 1.0, 0.5, Vec3::Zero(), 99.0, true}); // outside: not counted
    const ada::FitStats s = ada::fit_stats(a);
    CHECK(s.points == 20);
    CHECK(s.outside == 1);
    CHECK(std::abs(s.max_mm - 2.0) < 1e-12);
    // RMS of 0.1 .. 2.0: sqrt(sum k^2 / 20) / 10 = sqrt(143.5) / 10.
    CHECK(std::abs(s.rms_mm - std::sqrt(143.5) / 10.0) < 1e-12);
    // 19 of 20 values are at or below 1.9.
    CHECK(std::abs(s.p95_mm - 1.9) < 1e-12);
    CHECK(ada::fit_stats({}).points == 0);
}
