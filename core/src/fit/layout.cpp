#include "ada/fit/layout.hpp"

#include <Eigen/Geometry>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace ada {

namespace {

using Polyline = std::vector<Vec3>;

double degrees_between(const Vec3& a, const Vec3& b) {
    return std::atan2(a.cross(b).norm(), a.dot(b)) * 180.0 / std::numbers::pi;
}

std::string point_text(const Vec3& p) {
    return fmt::format("({:.1f}, {:.1f}, {:.1f})", p.x(), p.y(), p.z());
}

/// The vertices from `from` to `to` (both included), stepping by `step`
/// (+1 or -1) round a loop of `n`.
Polyline walk(const Polyline& loop, std::size_t from, std::size_t to, int step) {
    const std::size_t n = loop.size();
    Polyline out{loop[from]};
    for (std::size_t k = from; k != to;) {
        k = step > 0 ? (k + 1) % n : (k + n - 1) % n;
        out.push_back(loop[k]);
    }
    return out;
}

/// Indices cutting a polyline into `parts` pieces at the vertices nearest
/// to equal lengths: parts + 1 strictly increasing indices, first and last
/// vertex included.
std::vector<std::size_t> equal_cuts(const Polyline& line, int parts, const std::string& name) {
    std::vector<double> lengths{0.0};
    for (std::size_t k = 1; k < line.size(); ++k) {
        lengths.push_back(lengths.back() + (line[k] - line[k - 1]).norm());
    }
    std::vector<std::size_t> cuts{0};
    for (int q = 1; q < parts; ++q) {
        const double target = lengths.back() * q / parts;
        const auto nearest =
            std::min_element(lengths.begin(), lengths.end(), [&](double a, double b) {
                return std::abs(a - target) < std::abs(b - target);
            });
        cuts.push_back(static_cast<std::size_t>(nearest - lengths.begin()));
    }
    cuts.push_back(line.size() - 1);
    for (std::size_t k = 1; k < cuts.size(); ++k) {
        if (cuts[k] <= cuts[k - 1]) {
            throw std::invalid_argument(
                fmt::format("the {} has {} outline vertices, too few to cut into {} pieces",
                            name,
                            line.size(),
                            parts));
        }
    }
    return cuts;
}

Polyline piece(const Polyline& line, std::size_t from, std::size_t to) {
    return {line.begin() + static_cast<std::ptrdiff_t>(from),
            line.begin() + static_cast<std::ptrdiff_t>(to) + 1};
}

} // namespace

std::vector<TargetOutline> target_outlines(const TargetMesh& mesh) {
    std::vector<TargetOutline> out;
    for (Polyline& loop : mesh_outline(mesh)) {
        TargetOutline outline;
        const std::size_t n = loop.size();
        for (std::size_t k = 0; k < n; ++k) {
            const Vec3& previous = loop[(k + n - 1) % n];
            const Vec3& next = loop[(k + 1) % n];
            outline.length += (next - loop[k]).norm();
            const double turn = degrees_between(loop[k] - previous, next - loop[k]);
            if (n >= 3 && turn > kOutlineCornerDegrees) {
                outline.corners.push_back({k, turn});
            }
        }
        outline.points = std::move(loop);
        out.push_back(std::move(outline));
    }
    return out;
}

int facing_axis(const TargetMesh& mesh) {
    if (mesh.triangles.empty()) {
        throw std::invalid_argument("the target has no triangles, so no surface to face a way");
    }
    Vec3 area = Vec3::Zero();
    for (const auto& t : mesh.triangles) {
        const Vec3 n = (mesh.vertices[t[1]] - mesh.vertices[t[0]])
                           .cross(mesh.vertices[t[2]] - mesh.vertices[t[0]]);
        area += n.cwiseAbs();
    }
    int axis = 0;
    area.maxCoeff(&axis);
    return axis;
}

NetworkLayout layout_network(const TargetMesh& mesh,
                             const std::array<Vec3, 4>& corners,
                             int rows,
                             int cols,
                             int section_points) {
    if (rows < 1 || cols < 1) {
        throw std::invalid_argument(
            fmt::format("rows and cols must be at least 1, got {} x {}", rows, cols));
    }
    if (section_points < 2) {
        throw std::invalid_argument(
            fmt::format("a section needs at least 2 points, got {}", section_points));
    }
    const std::vector<std::vector<Vec3>> loops = mesh_outline(mesh);
    if (loops.empty()) {
        throw std::invalid_argument("the target has no outline (a point cloud or a closed mesh)");
    }

    NetworkLayout layout;
    layout.axis = facing_axis(mesh);

    // Each corner at its nearest outline vertex, all on one loop.
    std::array<std::size_t, 4> loop_of{};
    std::array<std::size_t, 4> index{};
    for (std::size_t k = 0; k < 4; ++k) {
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t l = 0; l < loops.size(); ++l) {
            for (std::size_t i = 0; i < loops[l].size(); ++i) {
                const double d = (loops[l][i] - corners[k]).norm();
                if (d < best) {
                    best = d;
                    loop_of[k] = l;
                    index[k] = i;
                }
            }
        }
        layout.corners[k] = loops[loop_of[k]][index[k]];
        layout.corner_offsets[k] = best;
    }
    for (std::size_t k = 1; k < 4; ++k) {
        if (loop_of[k] != loop_of[0]) {
            throw std::invalid_argument(fmt::format(
                "the corners lie on different outline loops (the target has {}): corner 0 near "
                "{}, corner {} near {}",
                loops.size(),
                point_text(layout.corners[0]),
                k,
                point_text(layout.corners[k])));
        }
    }
    const Polyline& loop = loops[loop_of[0]];
    const std::size_t n = loop.size();
    auto ahead = [&](std::size_t from, std::size_t to) { return (to + n - from) % n; };
    const std::size_t d1 = ahead(index[0], index[1]);
    const std::size_t d2 = ahead(index[0], index[2]);
    const std::size_t d3 = ahead(index[0], index[3]);
    if (d1 == 0 || d2 == 0 || d3 == 0 || d1 == d2 || d2 == d3 || d1 == d3) {
        throw std::invalid_argument(
            "two corners fall on the same outline vertex; give four distinct corners");
    }
    int step = 0;
    if (d1 < d2 && d2 < d3) {
        step = 1;
    } else if (d3 < d2 && d2 < d1) {
        step = -1;
    } else {
        throw std::invalid_argument(
            "the corners are not in order around the outline: give them as you meet them going "
            "round it, either way");
    }

    // The outer lines, each running the way u or v grows.
    const Polyline first_row = walk(loop, index[0], index[1], step);
    const Polyline last_col = walk(loop, index[1], index[2], step);
    Polyline last_row = walk(loop, index[2], index[3], step);
    std::reverse(last_row.begin(), last_row.end());
    Polyline first_col = walk(loop, index[3], index[0], step);
    std::reverse(first_col.begin(), first_col.end());
    const auto first_row_cuts = equal_cuts(first_row, cols, "side from corner 0 to corner 1");
    const auto last_col_cuts = equal_cuts(last_col, rows, "side from corner 1 to corner 2");
    const auto last_row_cuts = equal_cuts(last_row, cols, "side from corner 3 to corner 2");
    const auto first_col_cuts = equal_cuts(first_col, rows, "side from corner 0 to corner 3");

    // Plan coordinates: the two axes other than the facing one, in order.
    const int axis = layout.axis;
    const int p = axis == 0 ? 1 : 0;
    const int q = axis == 2 ? 1 : 2;
    auto on_target = [&](double a, double b) -> std::optional<Vec3> {
        const std::optional<double> h = mesh_height(mesh, axis, a, b);
        if (!h.has_value()) {
            return std::nullopt;
        }
        Vec3 out;
        out[p] = a;
        out[q] = b;
        out[axis] = *h;
        return out;
    };

    const auto R = static_cast<std::size_t>(rows);
    const auto C = static_cast<std::size_t>(cols);
    std::vector<std::vector<Vec3>> node(R + 1, std::vector<Vec3>(C + 1));
    for (std::size_t c = 0; c <= C; ++c) {
        node[0][c] = first_row[first_row_cuts[c]];
        node[R][c] = last_row[last_row_cuts[c]];
    }
    for (std::size_t r = 0; r <= R; ++r) {
        node[r][0] = first_col[first_col_cuts[r]];
        node[r][C] = last_col[last_col_cuts[r]];
    }
    for (std::size_t r = 1; r < R; ++r) {
        for (std::size_t c = 1; c < C; ++c) {
            const double s = static_cast<double>(c) / cols;
            const double t = static_cast<double>(r) / rows;
            auto coons = [&](int k) {
                return (1 - t) * node[0][c][k] + t * node[R][c][k] + (1 - s) * node[r][0][k] +
                       s * node[r][C][k] -
                       ((1 - s) * (1 - t) * node[0][0][k] + s * (1 - t) * node[0][C][k] +
                        (1 - s) * t * node[R][0][k] + s * t * node[R][C][k]);
            };
            const std::optional<Vec3> at = on_target(coons(p), coons(q));
            if (!at.has_value()) {
                throw std::invalid_argument(
                    fmt::format("inner node ({}, {}) falls off the target", r, c));
            }
            node[r][c] = *at;
        }
    }

    auto section = [&](const Vec3& a, const Vec3& b) {
        Polyline out{a};
        for (int k = 1; k + 1 < section_points; ++k) {
            const double t = static_cast<double>(k) / (section_points - 1);
            const std::optional<Vec3> at =
                on_target(a[p] + t * (b[p] - a[p]), a[q] + t * (b[q] - a[q]));
            if (!at.has_value()) {
                throw std::invalid_argument(fmt::format(
                    "the line from {} to {} leaves the target", point_text(a), point_text(b)));
            }
            out.push_back(*at);
        }
        out.push_back(b);
        return out;
    };

    layout.along_u.assign(R + 1, {});
    for (std::size_t r = 0; r <= R; ++r) {
        for (std::size_t c = 0; c < C; ++c) {
            layout.along_u[r].push_back(
                r == 0   ? piece(first_row, first_row_cuts[c], first_row_cuts[c + 1])
                : r == R ? piece(last_row, last_row_cuts[c], last_row_cuts[c + 1])
                         : section(node[r][c], node[r][c + 1]));
        }
    }
    layout.along_v.assign(C + 1, {});
    for (std::size_t c = 0; c <= C; ++c) {
        for (std::size_t r = 0; r < R; ++r) {
            layout.along_v[c].push_back(
                c == 0   ? piece(first_col, first_col_cuts[r], first_col_cuts[r + 1])
                : c == C ? piece(last_col, last_col_cuts[r], last_col_cuts[r + 1])
                         : section(node[r][c], node[r + 1][c]));
        }
    }
    return layout;
}

} // namespace ada
