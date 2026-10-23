#include "geom/polygon_ops.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace ada::detail {

Vec3 de_casteljau(std::span<const Vec3> points, double t) {
    std::vector<Vec3> work(points.begin(), points.end());
    const double s = 1.0 - t;
    for (std::size_t level = work.size() - 1; level > 0; --level) {
        for (std::size_t i = 0; i < level; ++i) {
            work[i] = s * work[i] + t * work[i + 1];
        }
    }
    return work.front();
}

std::vector<Vec3> difference(std::span<const Vec3> points) {
    if (points.size() == 1) {
        return {Vec3::Zero()};
    }
    const std::size_t n = points.size() - 1;
    const double degree = static_cast<double>(n);
    std::vector<Vec3> result;
    result.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        result.emplace_back(degree * (points[i + 1] - points[i]));
    }
    return result;
}

std::vector<Vec3> elevate(std::span<const Vec3> points) {
    const std::size_t n = points.size() - 1;
    const double n_plus_1 = static_cast<double>(n + 1);
    std::vector<Vec3> result(n + 2);
    result.front() = points.front();
    result.back() = points.back();
    for (std::size_t i = 1; i <= n; ++i) {
        const double a = static_cast<double>(i) / n_plus_1;
        result[i] = a * points[i - 1] + (1.0 - a) * points[i];
    }
    return result;
}

std::pair<std::vector<Vec3>, std::vector<Vec3>> split(std::span<const Vec3> points, double t) {
    std::vector<Vec3> work(points.begin(), points.end());
    const std::size_t n = work.size() - 1;
    const double s = 1.0 - t;

    // After level r, work[0 .. n-r] holds the level-r points P^r_i. The left
    // piece's polygon is P^r_0 for r = 0..n, the right piece's is P^(n-k)_k.
    std::vector<Vec3> left(n + 1);
    std::vector<Vec3> right(n + 1);
    left.front() = work.front();
    right.back() = work.back();
    for (std::size_t level = 1; level <= n; ++level) {
        for (std::size_t i = 0; i + level <= n; ++i) {
            work[i] = s * work[i] + t * work[i + 1];
        }
        left[level] = work.front();
        right[n - level] = work[n - level];
    }
    return {std::move(left), std::move(right)};
}

} // namespace ada::detail
