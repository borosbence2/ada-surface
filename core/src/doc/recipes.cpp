#include "ada/doc/recipes.hpp"

#include "ada/solve/constraints.hpp"
#include "ada/tolerances.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace ada {

namespace {

double model_scale(const std::vector<const BezierCurve*>& curves) {
    double scale = 1.0;
    for (const BezierCurve* c : curves) {
        for (const Vec3& p : c->control_points()) {
            scale = std::max(scale, p.cwiseAbs().maxCoeff());
        }
    }
    return scale;
}

BezierCurve with_ends(const BezierCurve& curve, const Vec3& start, const Vec3& end) {
    std::vector<Vec3> points = curve.control_points();
    points.front() = start;
    points.back() = end;
    return BezierCurve(std::move(points));
}

BezierCurve raised_to(const BezierCurve& curve, int degree) {
    return curve.degree() < degree ? curve.elevated(degree - curve.degree()) : curve;
}

std::string edge_name(const EdgeRef& ref) {
    return fmt::format("{}.{}", ref.patch, to_string(ref.edge));
}

int order_of(Continuity c) {
    return c == Continuity::G0 ? 0 : c == Continuity::G1 ? 1 : 2;
}

/// Mean distance between two rows of points, the second possibly reversed.
double mean_gap(const std::vector<Vec3>& a, std::vector<Vec3> b, bool reversed) {
    if (reversed) {
        std::reverse(b.begin(), b.end());
    }
    double sum = 0.0;
    for (std::size_t k = 0; k < a.size(); ++k) {
        sum += (a[k] - b[k]).norm();
    }
    return sum / static_cast<double>(a.size());
}

/// Mean length of `degree_across * (row1 - row0)`: the size of the first
/// cross-boundary derivative along an edge.
double mean_cross_derivative(const BezierPatch& p, Edge e) {
    const std::vector<Vec3> r0 = p.edge_row(e, 0);
    const std::vector<Vec3> r1 = p.edge_row(e, 1);
    double sum = 0.0;
    for (std::size_t k = 0; k < r0.size(); ++k) {
        sum += (r1[k] - r0[k]).norm();
    }
    return p.degree_across(e) * sum / static_cast<double>(r0.size());
}

void fix_all(Constraints& c,
             std::size_t index,
             const BezierPatch& patch,
             const std::string& label) {
    for (std::size_t i = 0; i < patch.num_u(); ++i) {
        for (std::size_t j = 0; j < patch.num_v(); ++j) {
            add_fixed(c, {index, i, j}, patch.control_point(i, j), label);
        }
    }
}

void merge(SceneSolveSummary& summary, const SolveResult& result) {
    ++summary.stages;
    summary.unknowns += result.unknowns;
    summary.max_residual = std::max(summary.max_residual, result.max_residual);
    for (const ConstraintResidual& r : result.residuals) {
        summary.residuals.push_back(r);
    }
}

/// The patch the solve starts from: the stored control points, raised
/// exactly when the recipe now asks for higher degrees (so a least-change
/// solve keeps the surface it had); the recipe's starting geometry when there
/// are none or a degree went down.
BezierPatch starting_patch(const Scene& scene, const ScenePatch& p, int degree_u, int degree_v) {
    if (p.solved && p.patch.degree_u() <= degree_u && p.patch.degree_v() <= degree_v) {
        return p.patch.elevated_u(degree_u - p.patch.degree_u())
            .elevated_v(degree_v - p.patch.degree_v());
    }
    return initial_patch(scene, p);
}

int blend_degree(const Blend& b) {
    return b.degree_across.value_or(2 * order_of(b.continuity) + 1);
}

} // namespace

Boundaries
resolve_boundaries(const Scene& scene, const FromBoundaries& recipe, const std::string& patch_id) {
    const auto& ids = recipe.curves;
    BezierCurve umin = scene.curve(ids[0]).curve;
    BezierCurve umax = scene.curve(ids[1]).curve;
    BezierCurve vmin = scene.curve(ids[2]).curve;
    BezierCurve vmax = scene.curve(ids[3]).curve;
    const double tolerance = tol::kCurveCornerGap * model_scale({&umin, &umax, &vmin, &vmax});

    auto meet = [&](const std::string& a_id,
                    const std::string& b_id,
                    const Vec3& a_end,
                    const Vec3& b_end,
                    const char* corner) {
        const double gap = (a_end - b_end).norm();
        if (gap > tolerance) {
            throw std::invalid_argument(fmt::format(
                "patch '{}': curves '{}' and '{}' do not meet at corner {} ({:.4g} mm apart)",
                patch_id,
                a_id,
                b_id,
                corner,
                gap));
        }
    };

    // Corner (0, 0) is where UMin and VMin meet; orient both to start there.
    {
        const std::array<std::pair<bool, bool>, 4> options{
            {{false, false}, {false, true}, {true, false}, {true, true}}};
        double best = std::numeric_limits<double>::infinity();
        std::pair<bool, bool> flip{false, false};
        for (const auto& [ru, rv] : options) {
            const Vec3 a = ru ? umin.control_points().back() : umin.control_points().front();
            const Vec3 b = rv ? vmin.control_points().back() : vmin.control_points().front();
            const double d = (a - b).norm();
            if (d < best) {
                best = d;
                flip = {ru, rv};
            }
        }
        if (flip.first)
            umin = umin.reversed();
        if (flip.second)
            vmin = vmin.reversed();
    }
    // VMax starts at UMin's far end (corner (0, 1)); UMax at VMin's far end.
    auto orient_from = [](BezierCurve c, const Vec3& start) {
        const double forward = (c.control_points().front() - start).norm();
        const double backward = (c.control_points().back() - start).norm();
        return backward < forward ? c.reversed() : c;
    };
    vmax = orient_from(vmax, umin.control_points().back());
    umax = orient_from(umax, vmin.control_points().back());

    const Vec3 c00 = umin.control_points().front();
    const Vec3 c01 = umin.control_points().back();
    const Vec3 c10 = vmin.control_points().back();
    const Vec3 c11 = umax.control_points().back();
    meet(ids[0], ids[2], c00, vmin.control_points().front(), "(0, 0)");
    meet(ids[0], ids[3], c01, vmax.control_points().front(), "(0, 1)");
    meet(ids[2], ids[1], c10, umax.control_points().front(), "(1, 0)");
    meet(ids[1], ids[3], c11, vmax.control_points().back(), "(1, 1)");

    // Snap each corner to the mean of its two curve ends, so the fixed rows
    // agree exactly where they share a control point.
    const Vec3 s00 = 0.5 * (c00 + vmin.control_points().front());
    const Vec3 s01 = 0.5 * (c01 + vmax.control_points().front());
    const Vec3 s10 = 0.5 * (c10 + umax.control_points().front());
    const Vec3 s11 = 0.5 * (c11 + vmax.control_points().back());
    umin = with_ends(umin, s00, s01);
    umax = with_ends(umax, s10, s11);
    vmin = with_ends(vmin, s00, s10);
    vmax = with_ends(vmax, s01, s11);

    auto requested = [&](const std::optional<int>& degree, int curves, const char* name) {
        if (degree.has_value() && *degree < curves) {
            throw std::invalid_argument(
                fmt::format("patch '{}': {} {} is below its curves' degree {} in that direction",
                            patch_id,
                            name,
                            *degree,
                            curves));
        }
        return degree.value_or(curves);
    };
    const int du = requested(recipe.degree_u, std::max(vmin.degree(), vmax.degree()), "degree_u");
    const int dv = requested(recipe.degree_v, std::max(umin.degree(), umax.degree()), "degree_v");
    return {raised_to(umin, dv), raised_to(umax, dv), raised_to(vmin, du), raised_to(vmax, du)};
}

BezierPatch initial_patch(const Scene& scene, const ScenePatch& patch) {
    if (!patch.recipe.has_value()) {
        return patch.patch;
    }
    if (const auto* fb = std::get_if<FromBoundaries>(&*patch.recipe)) {
        const Boundaries b = resolve_boundaries(scene, *fb, patch.id);
        const int n = b.degree_u();
        const int m = b.degree_v();
        const auto nu = static_cast<std::size_t>(n) + 1;
        const auto nv = static_cast<std::size_t>(m) + 1;
        std::vector<Vec3> points(nu * nv);
        auto at = [&](std::size_t i, std::size_t j) -> Vec3& { return points[i * nv + j]; };
        for (std::size_t i = 0; i < nu; ++i) {
            at(i, 0) = b.vmin.control_point(i);
            at(i, nv - 1) = b.vmax.control_point(i);
        }
        for (std::size_t j = 0; j < nv; ++j) {
            at(0, j) = b.umin.control_point(j);
            at(nu - 1, j) = b.umax.control_point(j);
        }
        // Discrete Coons: blend opposite boundaries linearly, subtract the
        // bilinear corner term counted twice.
        for (std::size_t i = 1; i + 1 < nu; ++i) {
            for (std::size_t j = 1; j + 1 < nv; ++j) {
                const double s = static_cast<double>(i) / n;
                const double t = static_cast<double>(j) / m;
                at(i, j) = (1 - s) * at(0, j) + s * at(nu - 1, j) + (1 - t) * at(i, 0) +
                           t * at(i, nv - 1) -
                           ((1 - s) * (1 - t) * at(0, 0) + s * (1 - t) * at(nu - 1, 0) +
                            (1 - s) * t * at(0, nv - 1) + s * t * at(nu - 1, nv - 1));
            }
        }
        return BezierPatch(n, m, std::move(points));
    }
    const auto& blend = std::get<Blend>(*patch.recipe);
    const BezierPatch& a = scene.patch(blend.from.patch).patch;
    const BezierPatch& b = scene.patch(blend.to.patch).patch;
    std::vector<Vec3> ra = a.edge_row(blend.from.edge, 0);
    std::vector<Vec3> rb = b.edge_row(blend.to.edge, 0);
    if (ra.size() != rb.size()) {
        throw std::invalid_argument(
            fmt::format("blend '{}': {} and {} have different degrees along the edge ({} vs {})",
                        patch.id,
                        edge_name(blend.from),
                        edge_name(blend.to),
                        ra.size() - 1,
                        rb.size() - 1));
    }
    if (edges_reversed(a, blend.from.edge, b, blend.to.edge)) {
        std::reverse(rb.begin(), rb.end());
    }
    const int m = blend_degree(blend);
    const auto nv = static_cast<std::size_t>(m) + 1;
    std::vector<Vec3> points(ra.size() * nv);
    for (std::size_t i = 0; i < ra.size(); ++i) {
        for (std::size_t j = 0; j < nv; ++j) {
            const double t = static_cast<double>(j) / m;
            points[i * nv + j] = (1 - t) * ra[i] + t * rb[i];
        }
    }
    return BezierPatch(static_cast<int>(ra.size()) - 1, m, std::move(points));
}

std::vector<EdgeLink> all_edge_links(const Scene& scene) {
    std::vector<EdgeLink> links = scene.edges;
    auto same = [](const EdgeRef& x, const EdgeRef& y) {
        return x.patch == y.patch && x.edge == y.edge;
    };
    auto add = [&](const EdgeLink& link) {
        for (const EdgeLink& l : links) {
            if ((same(l.a, link.a) && same(l.b, link.b)) ||
                (same(l.a, link.b) && same(l.b, link.a))) {
                return;
            }
        }
        links.push_back(link);
    };
    for (const ScenePatch& p : scene.patches) {
        if (p.recipe.has_value()) {
            if (const auto* blend = std::get_if<Blend>(&*p.recipe)) {
                add({blend->from, {p.id, Edge::VMin}, blend->continuity});
                add({{p.id, Edge::VMax}, blend->to, blend->continuity});
            }
        }
    }
    return links;
}

SceneSolveSummary solve_scene(Scene& scene) {
    const auto t0 = std::chrono::steady_clock::now();
    SceneSolveSummary summary;
    const SolveWeights weights{scene.solve.fairness, scene.solve.order, scene.solve.move};

    auto is_blend = [](const ScenePatch& p) {
        return p.recipe.has_value() && std::holds_alternative<Blend>(*p.recipe);
    };
    auto is_boundary = [](const ScenePatch& p) {
        return p.recipe.has_value() && std::holds_alternative<FromBoundaries>(*p.recipe);
    };

    for (const SurfacePin& pin : scene.pins) {
        if (!scene.patch(pin.patch).recipe.has_value()) {
            throw std::invalid_argument(fmt::format(
                "pin on '{}': a patch without a recipe is fixed and cannot follow a pin",
                pin.patch));
        }
    }

    // Stage 1: from_boundaries patches, jointly, with the fixed patches they
    // are linked to.
    std::map<std::string, std::size_t> index; // scene patch id -> problem index
    SolveProblem stage1;
    stage1.weights = weights;
    std::vector<std::size_t> scene_of; // problem index -> scene patch index
    std::map<std::string, Boundaries> boundaries;
    for (std::size_t k = 0; k < scene.patches.size(); ++k) {
        const ScenePatch& p = scene.patches[k];
        if (is_boundary(p)) {
            const Boundaries b =
                resolve_boundaries(scene, std::get<FromBoundaries>(*p.recipe), p.id);
            index[p.id] = stage1.patches.size();
            scene_of.push_back(k);
            stage1.patches.push_back(starting_patch(scene, p, b.degree_u(), b.degree_v()));
            boundaries.emplace(p.id, b);
        }
    }
    if (!stage1.patches.empty()) {
        // Fixed patches linked to a stage-1 patch join the problem, held still.
        for (const EdgeLink& link : scene.edges) {
            for (const EdgeRef* end : {&link.a, &link.b}) {
                const EdgeRef* other = end == &link.a ? &link.b : &link.a;
                const ScenePatch& p = scene.patch(end->patch);
                if (!p.recipe.has_value() && index.count(p.id) == 0 &&
                    index.count(other->patch) > 0) {
                    index[p.id] = stage1.patches.size();
                    scene_of.push_back(scene.patch_index(p.id));
                    stage1.patches.push_back(p.patch);
                    fix_all(
                        stage1.constraints, index[p.id], p.patch, fmt::format("fixed {}", p.id));
                }
            }
        }

        // Continuity first, so we know which curve control points it acts on:
        // the ends of rows 1..k next to every G1/G2-joined or symmetric edge.
        std::set<std::tuple<std::size_t, std::size_t, std::size_t>> yielding;
        auto mark_rows = [&](std::size_t k, Edge edge, int order) {
            const BezierPatch& patch = stage1.patches[k];
            const auto last = static_cast<std::size_t>(patch.degree_along(edge));
            for (int depth = 1; depth <= order; ++depth) {
                for (const std::size_t pos : {std::size_t{0}, last}) {
                    const auto [i, j] = patch.edge_row_cv(edge, depth, pos);
                    yielding.insert({k, i, j});
                }
            }
        };
        for (const EdgeLink& link : scene.edges) {
            if (index.count(link.a.patch) > 0 && index.count(link.b.patch) > 0) {
                const std::size_t ka = index[link.a.patch];
                const std::size_t kb = index[link.b.patch];
                add_edge_join(stage1.constraints,
                              stage1.patches,
                              {ka, link.a.edge, kb, link.b.edge, link.continuity},
                              fmt::format("{} {} ~ {}",
                                          to_string(link.continuity),
                                          edge_name(link.a),
                                          edge_name(link.b)));
                mark_rows(ka, link.a.edge, order_of(link.continuity));
                mark_rows(kb, link.b.edge, order_of(link.continuity));
            }
        }
        for (const SymmetryLink& link : scene.symmetry) {
            if (index.count(link.edge.patch) > 0 && is_boundary(scene.patch(link.edge.patch))) {
                const std::size_t k = index[link.edge.patch];
                add_symmetry(stage1.constraints,
                             k,
                             stage1.patches[k],
                             link.edge.edge,
                             link.continuity,
                             fmt::format("Y=0 {}", edge_name(link.edge)));
                mark_rows(k, link.edge.edge, link.continuity == Continuity::G0 ? 0 : 1);
            }
        }
        for (const SurfacePin& pin : scene.pins) {
            if (index.count(pin.patch) > 0 && is_boundary(scene.patch(pin.patch))) {
                const std::size_t k = index[pin.patch];
                add_surface_point(stage1.constraints,
                                  k,
                                  stage1.patches[k],
                                  pin.u,
                                  pin.v,
                                  pin.at,
                                  fmt::format("pin {} ({:.3g}, {:.3g})", pin.patch, pin.u, pin.v));
            }
        }

        // Boundary rows on the curves: exact where nothing else acts on the
        // control point, a strong pull where continuity does.
        for (const auto& [id, b] : boundaries) {
            const std::size_t k = index[id];
            const BezierPatch& start = stage1.patches[k];
            auto hold = [&](const BezierCurve& curve, Edge edge, const char* name) {
                for (std::size_t m = 0; m < curve.num_control_points(); ++m) {
                    const auto [i, j] = start.edge_row_cv(edge, 0, m);
                    if (yielding.count({k, i, j}) > 0) {
                        stage1.targets.push_back(
                            {{k, i, j}, curve.control_point(m), tol::kCurveTargetWeight});
                    } else {
                        add_fixed(stage1.constraints,
                                  {k, i, j},
                                  curve.control_point(m),
                                  fmt::format("{} boundary {}", id, name));
                    }
                }
            };
            hold(b.umin, Edge::UMin, "UMin");
            hold(b.umax, Edge::UMax, "UMax");
            hold(b.vmin, Edge::VMin, "VMin");
            hold(b.vmax, Edge::VMax, "VMax");
        }

        const SolveResult result = solve(stage1);
        merge(summary, result);
        for (std::size_t k = 0; k < scene_of.size(); ++k) {
            ScenePatch& p = scene.patches[scene_of[k]];
            if (is_boundary(p)) {
                p.patch = result.patches[k];
                p.solved = true;
            }
        }

        // How far each curve gave way, over every patch edge that uses it.
        for (const SceneCurve& curve : scene.curves) {
            double worst = -1.0;
            for (const auto& [id, b] : boundaries) {
                const auto& ids = std::get<FromBoundaries>(*scene.patch(id).recipe).curves;
                const std::array<std::pair<const BezierCurve*, Edge>, 4> sides{
                    {{&b.umin, Edge::UMin},
                     {&b.umax, Edge::UMax},
                     {&b.vmin, Edge::VMin},
                     {&b.vmax, Edge::VMax}}};
                for (std::size_t s = 0; s < 4; ++s) {
                    if (ids[s] != curve.id) {
                        continue;
                    }
                    const std::vector<Vec3> row =
                        scene.patch(id).patch.edge_row(sides[s].second, 0);
                    worst = std::max(worst, 0.0);
                    for (std::size_t m = 0; m < row.size(); ++m) {
                        worst = std::max(worst, (row[m] - sides[s].first->control_point(m)).norm());
                    }
                }
            }
            if (worst >= 0.0) {
                summary.curves.push_back({curve.id, worst});
            }
        }
    }

    // Blends, each once its sources are solved, with the sources held fixed.
    std::set<std::string> done;
    for (const ScenePatch& p : scene.patches) {
        if (!is_blend(p)) {
            done.insert(p.id);
        }
    }
    bool progress = true;
    while (progress) {
        progress = false;
        for (ScenePatch& p : scene.patches) {
            if (!is_blend(p) || done.count(p.id) > 0) {
                continue;
            }
            const Blend& blend = std::get<Blend>(*p.recipe);
            if (done.count(blend.from.patch) == 0 || done.count(blend.to.patch) == 0) {
                continue;
            }
            const BezierPatch& a = scene.patch(blend.from.patch).patch;
            const BezierPatch& b = scene.patch(blend.to.patch).patch;
            const int k = order_of(blend.continuity);
            if (blend_degree(blend) < 2 * k + 1) {
                throw std::invalid_argument(fmt::format(
                    "blend '{}': degree across {} is too low for {} on both sides (needs {})",
                    p.id,
                    blend_degree(blend),
                    to_string(blend.continuity),
                    2 * k + 1));
            }
            SolveProblem problem;
            problem.weights = weights;
            problem.patches = {
                a,
                b,
                starting_patch(scene, p, a.degree_along(blend.from.edge), blend_degree(blend))};
            fix_all(problem.constraints, 0, a, fmt::format("fixed {}", blend.from.patch));
            fix_all(problem.constraints, 1, b, fmt::format("fixed {}", blend.to.patch));

            // Derivative scales that make the blend cross the gap at an even
            // pace: its first row sits about 1/m of the gap from each side.
            const double gap = mean_gap(a.edge_row(blend.from.edge, 0),
                                        b.edge_row(blend.to.edge, 0),
                                        edges_reversed(a, blend.from.edge, b, blend.to.edge));
            double alpha_from = 1.0;
            double alpha_to = 1.0;
            if (k >= 1 && gap > 0.0) {
                alpha_from = gap / mean_cross_derivative(a, blend.from.edge);
                alpha_to = mean_cross_derivative(b, blend.to.edge) / gap;
            }
            add_edge_join(
                problem.constraints,
                problem.patches,
                {0, blend.from.edge, 2, Edge::VMin, blend.continuity, alpha_from},
                fmt::format(
                    "{} {} ~ {}.VMin", to_string(blend.continuity), edge_name(blend.from), p.id));
            add_edge_join(
                problem.constraints,
                problem.patches,
                {2, Edge::VMax, 1, blend.to.edge, blend.continuity, alpha_to},
                fmt::format(
                    "{} {}.VMax ~ {}", to_string(blend.continuity), p.id, edge_name(blend.to)));
            for (const SymmetryLink& link : scene.symmetry) {
                if (link.edge.patch == p.id) {
                    add_symmetry(problem.constraints,
                                 2,
                                 problem.patches[2],
                                 link.edge.edge,
                                 link.continuity,
                                 fmt::format("Y=0 {}", edge_name(link.edge)));
                }
            }
            for (const SurfacePin& pin : scene.pins) {
                if (pin.patch == p.id) {
                    add_surface_point(
                        problem.constraints,
                        2,
                        problem.patches[2],
                        pin.u,
                        pin.v,
                        pin.at,
                        fmt::format("pin {} ({:.3g}, {:.3g})", pin.patch, pin.u, pin.v));
                }
            }
            const SolveResult result = solve(problem);
            merge(summary, result);
            p.patch = result.patches[2];
            p.solved = true;
            done.insert(p.id);
            progress = true;
        }
    }
    for (const ScenePatch& p : scene.patches) {
        if (done.count(p.id) == 0) {
            throw std::invalid_argument(fmt::format("blend '{}' depends, through its sources, on "
                                                    "itself; blends must form a chain, not a cycle",
                                                    p.id));
        }
    }

    summary.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return summary;
}

std::vector<std::string> add_network(Scene& scene, const NetworkSpec& spec) {
    const std::size_t rows = spec.along_u.empty() ? 0 : spec.along_u.size() - 1;
    const std::size_t cols = spec.along_v.empty() ? 0 : spec.along_v.size() - 1;
    if (rows == 0 || cols == 0) {
        throw std::invalid_argument("a network needs at least two curve lines in each direction");
    }
    for (const auto& line : spec.along_u) {
        if (line.size() != cols) {
            throw std::invalid_argument(
                fmt::format("every along-u line needs {} curves (one per column)", cols));
        }
    }
    for (const auto& line : spec.along_v) {
        if (line.size() != rows) {
            throw std::invalid_argument(
                fmt::format("every along-v line needs {} curves (one per row)", rows));
        }
    }
    auto id_of = [&](std::size_t r, std::size_t c) {
        return fmt::format("{}_{}_{}", spec.prefix, r, c);
    };
    std::vector<std::string> ids;
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            const std::string id = id_of(r, c);
            for (const ScenePatch& p : scene.patches) {
                if (p.id == id) {
                    throw std::invalid_argument(fmt::format("patch id '{}' is already taken", id));
                }
            }
            ScenePatch patch{id,
                             BezierPatch(1, 1, std::vector<Vec3>(4, Vec3::Zero())),
                             FromBoundaries{{spec.along_v[c][r],
                                             spec.along_v[c + 1][r],
                                             spec.along_u[r][c],
                                             spec.along_u[r + 1][c]},
                                            std::nullopt,
                                            std::nullopt},
                             false};
            patch.patch = initial_patch(scene, patch);
            scene.patches.push_back(std::move(patch));
            ids.push_back(id);
        }
    }
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            if (c + 1 < cols) {
                scene.edges.push_back(
                    {{id_of(r, c), Edge::UMax}, {id_of(r, c + 1), Edge::UMin}, spec.continuity});
            }
            if (r + 1 < rows) {
                scene.edges.push_back(
                    {{id_of(r, c), Edge::VMax}, {id_of(r + 1, c), Edge::VMin}, spec.continuity});
            }
        }
    }
    return ids;
}

} // namespace ada
