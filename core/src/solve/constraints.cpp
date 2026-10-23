#include "ada/solve/constraints.hpp"

#include <fmt/format.h>

#include <stdexcept>

namespace ada {

namespace {

constexpr std::array<bool, 3> kX{true, false, false};
constexpr std::array<bool, 3> kY{false, true, false};
constexpr std::array<bool, 3> kZ{false, false, true};

CvRef row_cv(
    std::size_t patch_index, const BezierPatch& patch, Edge edge, int depth, std::size_t k) {
    const auto [i, j] = patch.edge_row_cv(edge, depth, k);
    return {patch_index, i, j};
}

void require_depth(const BezierPatch& patch, Edge edge, int depth, const std::string& label) {
    if (patch.degree_across(edge) < depth) {
        throw std::invalid_argument(fmt::format(
            "{}: the patch has degree {} across {}, too low for this continuity (needs {})",
            label,
            patch.degree_across(edge),
            to_string(edge),
            depth));
    }
}

} // namespace

void add_fixed(Constraints& out, CvRef cv, const Vec3& position, const std::string& label) {
    out.push_back({{{cv, 1.0}}, position, {true, true, true}, label});
}

void add_fixed_row(Constraints& out,
                   std::size_t patch_index,
                   const BezierPatch& patch,
                   Edge edge,
                   int depth,
                   const std::string& label) {
    const auto count = static_cast<std::size_t>(patch.degree_along(edge)) + 1;
    for (std::size_t k = 0; k < count; ++k) {
        const CvRef cv = row_cv(patch_index, patch, edge, depth, k);
        add_fixed(out, cv, patch.control_point(cv.i, cv.j), label);
    }
}

bool edges_reversed(const BezierPatch& a, Edge edge_a, const BezierPatch& b, Edge edge_b) {
    const std::vector<Vec3> ra = a.edge_row(edge_a, 0);
    const std::vector<Vec3> rb = b.edge_row(edge_b, 0);
    const double same = (ra.front() - rb.front()).norm() + (ra.back() - rb.back()).norm();
    const double opposite = (ra.front() - rb.back()).norm() + (ra.back() - rb.front()).norm();
    return opposite < same;
}

void add_edge_join(Constraints& out,
                   const std::vector<BezierPatch>& patches,
                   const EdgeJoin& join,
                   const std::string& label) {
    const BezierPatch& a = patches.at(join.a);
    const BezierPatch& b = patches.at(join.b);
    if (a.degree_along(join.edge_a) != b.degree_along(join.edge_b)) {
        throw std::invalid_argument(fmt::format("{}: degrees along the edges differ ({} vs {}); "
                                                "only matched edges can be joined before L4",
                                                label,
                                                a.degree_along(join.edge_a),
                                                b.degree_along(join.edge_b)));
    }
    if (!(join.alpha > 0.0)) {
        throw std::invalid_argument(
            fmt::format("{}: alpha must be positive, got {}", label, join.alpha));
    }
    const int order = join.continuity == Continuity::G0   ? 0
                      : join.continuity == Continuity::G1 ? 1
                                                          : 2;
    require_depth(a, join.edge_a, order, label);
    require_depth(b, join.edge_b, order, label);

    const bool reversed = edges_reversed(a, join.edge_a, b, join.edge_b);
    const auto count = static_cast<std::size_t>(a.degree_along(join.edge_a)) + 1;
    const double n = a.degree_across(join.edge_a);
    const double m = b.degree_across(join.edge_b);
    const double alpha = join.alpha;
    const double gamma = join.gamma;

    for (std::size_t k = 0; k < count; ++k) {
        const std::size_t kb = reversed ? count - 1 - k : k;
        auto ra = [&](int depth) { return row_cv(join.a, a, join.edge_a, depth, k); };
        auto rb = [&](int depth) { return row_cv(join.b, b, join.edge_b, depth, kb); };

        // G0: the boundary rows coincide.
        out.push_back({{{rb(0), 1.0}, {ra(0), -1.0}}, Vec3::Zero(), {true, true, true}, label});
        if (order >= 1) {
            // B's derivative into B equals alpha times A's derivative out of A:
            // m (b1 - b0) = alpha n (a0 - a1).
            out.push_back({{{rb(1), m}, {rb(0), -m}, {ra(1), alpha * n}, {ra(0), -alpha * n}},
                           Vec3::Zero(),
                           {true, true, true},
                           label});
        }
        if (order >= 2) {
            // m(m-1)(b2 - 2 b1 + b0) = alpha^2 n(n-1)(a2 - 2 a1 + a0) + gamma n (a0 - a1).
            // The second difference reads the same in either direction, so
            // the outward/inward distinction only matters for the gamma term.
            const double bb = m * (m - 1.0);
            const double aa = alpha * alpha * n * (n - 1.0);
            out.push_back({{{rb(2), bb},
                            {rb(1), -2.0 * bb},
                            {rb(0), bb},
                            {ra(2), -aa},
                            {ra(1), 2.0 * aa + gamma * n},
                            {ra(0), -aa - gamma * n}},
                           Vec3::Zero(),
                           {true, true, true},
                           label});
        }
    }
}

void add_symmetry(Constraints& out,
                  std::size_t patch_index,
                  const BezierPatch& patch,
                  Edge edge,
                  Continuity continuity,
                  const std::string& label) {
    const bool g1 = continuity != Continuity::G0;
    if (g1) {
        require_depth(patch, edge, 1, label);
    }
    const auto count = static_cast<std::size_t>(patch.degree_along(edge)) + 1;
    for (std::size_t k = 0; k < count; ++k) {
        const CvRef r0 = row_cv(patch_index, patch, edge, 0, k);
        out.push_back({{{r0, 1.0}}, Vec3::Zero(), kY, label});
        if (g1) {
            const CvRef r1 = row_cv(patch_index, patch, edge, 1, k);
            out.push_back({{{r1, 1.0}, {r0, -1.0}}, Vec3::Zero(), kX, label});
            out.push_back({{{r1, 1.0}, {r0, -1.0}}, Vec3::Zero(), kZ, label});
        }
    }
}

} // namespace ada
