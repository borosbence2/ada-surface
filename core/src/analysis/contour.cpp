#include "ada/analysis/contour.hpp"

#include "ada/analysis/sampling.hpp"
#include "ada/tolerances.hpp"

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace ada {

namespace {

/// Regula falsi stalls on one side for convex functions; Illinois halves the
/// stale endpoint's value to force progress. Converges superlinearly, so the
/// cap is only a guard.
constexpr int kMaxCrossingIterations = 100;

using Point = Eigen::Vector2d;

/// Root of `field - level` on the segment a -> b, given values of opposite sign
/// at the ends.
Point refine_crossing(
    const ScalarField& field, double level, Point a, double fa, Point b, double fb) {
    int stale = 0; // which end kept its value last step: -1 = a, +1 = b
    for (int iteration = 0; iteration < kMaxCrossingIterations; ++iteration) {
        if ((b - a).norm() < tol::kContourCrossing) {
            break;
        }
        const double t = fa / (fa - fb);
        const Point m = a + t * (b - a);
        const double fm = field(m.x(), m.y()) - level;
        if (fm == 0.0 || std::isnan(fm)) {
            return m;
        }
        if ((fm < 0.0) == (fa < 0.0)) {
            a = m;
            fa = fm;
            if (stale == -1) {
                fb *= 0.5;
            }
            stale = -1;
        } else {
            b = m;
            fb = fm;
            if (stale == 1) {
                fa *= 0.5;
            }
            stale = 1;
        }
    }
    const double t = fa / (fa - fb);
    return a + t * (b - a);
}

/// Contour construction for one level: crossings are shared between the two
/// cells either side of a grid edge through a key per edge, and segments link
/// crossings; chaining the segments gives the polylines.
class LevelBuilder {
public:
    LevelBuilder(const ScalarGrid& grid, const ScalarField& field, double level)
        : grid_(grid),
          field_(field),
          level_(level),
          u_(uniform_parameters(grid.nu)),
          v_(uniform_parameters(grid.nv)) {}

    std::vector<ContourLine> build() {
        for (std::size_t i = 0; i + 1 < grid_.nu; ++i) {
            for (std::size_t j = 0; j + 1 < grid_.nv; ++j) {
                add_cell(i, j);
            }
        }
        return chain();
    }

private:
    // Grid edge keys: edges along u (between (i, j) and (i+1, j)) first, then
    // edges along v (between (i, j) and (i, j+1)).
    std::size_t key_along_u(std::size_t i, std::size_t j) const { return i * grid_.nv + j; }

    std::size_t key_along_v(std::size_t i, std::size_t j) const {
        return (grid_.nu - 1) * grid_.nv + i * (grid_.nv - 1) + j;
    }

    double value(std::size_t i, std::size_t j) const { return grid_.at(i, j) - level_; }

    Point node(std::size_t i, std::size_t j) const { return {u_[i], v_[j]}; }

    std::size_t
    crossing(std::size_t key, std::size_t i0, std::size_t j0, std::size_t i1, std::size_t j1) {
        const auto found = index_.find(key);
        if (found != index_.end()) {
            return found->second;
        }
        const Point p = refine_crossing(
            field_, level_, node(i0, j0), value(i0, j0), node(i1, j1), value(i1, j1));
        points_.push_back(p);
        links_.emplace_back();
        index_.emplace(key, points_.size() - 1);
        return points_.size() - 1;
    }

    void link(std::size_t a, std::size_t b) {
        links_[a].push_back(b);
        links_[b].push_back(a);
    }

    void add_cell(std::size_t i, std::size_t j) {
        // Corners counter-clockwise in (u, v): c0 (i, j), c1 (i+1, j),
        // c2 (i+1, j+1), c3 (i, j+1). Edge k joins corner k to corner k+1.
        const std::array<std::pair<std::size_t, std::size_t>, 4> c{
            {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}}};
        std::array<bool, 4> above{};
        for (std::size_t k = 0; k < 4; ++k) {
            const double f = value(c[k].first, c[k].second);
            if (std::isnan(f)) {
                return;
            }
            above[k] = f > 0.0;
        }
        const std::array<std::size_t, 4> keys{
            key_along_u(i, j), key_along_v(i + 1, j), key_along_u(i, j + 1), key_along_v(i, j)};
        std::array<std::size_t, 4> cross{};
        std::array<bool, 4> crossed{};
        int count = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const std::size_t n = (k + 1) % 4;
            if (above[k] != above[n]) {
                crossed[k] = true;
                cross[k] = crossing(keys[k], c[k].first, c[k].second, c[n].first, c[n].second);
                ++count;
            }
        }
        if (count == 2) {
            std::array<std::size_t, 2> ends{};
            std::size_t e = 0;
            for (std::size_t k = 0; k < 4; ++k) {
                if (crossed[k]) {
                    ends[e++] = cross[k];
                }
            }
            link(ends[0], ends[1]);
        } else if (count == 4) {
            // Saddle: the centre decides which pair of opposite corners is
            // connected through the cell.
            const double centre =
                field_(0.5 * (u_[i] + u_[i + 1]), 0.5 * (v_[j] + v_[j + 1])) - level_;
            if ((centre > 0.0) == above[0]) {
                link(cross[0], cross[1]);
                link(cross[2], cross[3]);
            } else {
                link(cross[3], cross[0]);
                link(cross[1], cross[2]);
            }
        }
    }

    std::vector<ContourLine> chain() {
        std::vector<ContourLine> lines;
        std::vector<bool> used(points_.size(), false);
        auto walk = [&](std::size_t start) {
            ContourLine line;
            line.level = level_;
            std::size_t previous = start;
            std::size_t current = start;
            used[start] = true;
            line.uv.push_back(points_[start]);
            while (true) {
                bool advanced = false;
                for (const std::size_t next : links_[current]) {
                    if (next == previous && links_[current].size() > 1) {
                        continue;
                    }
                    if (next == start && line.uv.size() > 2) {
                        line.closed = true;
                        return line;
                    }
                    if (used[next]) {
                        continue;
                    }
                    used[next] = true;
                    if ((points_[next] - line.uv.back()).norm() >= tol::kContourMinSegment) {
                        line.uv.push_back(points_[next]);
                    }
                    previous = current;
                    current = next;
                    advanced = true;
                    break;
                }
                if (!advanced) {
                    return line;
                }
            }
        };
        // Open lines start at a crossing with one link (a grid boundary or an
        // undefined cell); what is left after them are closed loops.
        for (std::size_t p = 0; p < points_.size(); ++p) {
            if (!used[p] && links_[p].size() == 1) {
                lines.push_back(walk(p));
            }
        }
        for (std::size_t p = 0; p < points_.size(); ++p) {
            if (!used[p] && !links_[p].empty()) {
                lines.push_back(walk(p));
            }
        }
        return lines;
    }

    const ScalarGrid& grid_;
    const ScalarField& field_;
    double level_;
    std::vector<double> u_;
    std::vector<double> v_;
    std::vector<Point> points_;
    std::vector<std::vector<std::size_t>> links_;
    std::unordered_map<std::size_t, std::size_t> index_;
};

} // namespace

ScalarGrid sample_field(const ScalarField& field, std::size_t nu, std::size_t nv) {
    if (nu < 2 || nv < 2) {
        throw std::invalid_argument(
            fmt::format("a sampling grid needs at least 2 x 2 nodes, got {} x {}", nu, nv));
    }
    const std::vector<double> u = uniform_parameters(nu);
    const std::vector<double> v = uniform_parameters(nv);
    ScalarGrid grid{nu, nv, {}};
    grid.values.reserve(nu * nv);
    for (const double ui : u) {
        for (const double vj : v) {
            grid.values.push_back(field(ui, vj));
        }
    }
    return grid;
}

std::vector<ContourLine> extract_contours(const ScalarGrid& grid,
                                          const ScalarField& field,
                                          const std::vector<double>& levels) {
    std::vector<ContourLine> lines;
    for (const double level : levels) {
        std::vector<ContourLine> at_level = LevelBuilder(grid, field, level).build();
        for (ContourLine& line : at_level) {
            if (line.uv.size() >= 2) {
                lines.push_back(std::move(line));
            }
        }
    }
    return lines;
}

} // namespace ada
