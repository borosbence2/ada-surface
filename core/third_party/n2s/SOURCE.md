# Files copied from nurbs2subd

nurbs2subd is never linked as a library (decision D4). Individual files are
copied here when a milestone needs them, keep the `n2s` namespace, and are
built as the `ada_n2s` static library. Licence: MIT, see `LICENSE`.

Source repository: `github.com/borosbence2/nurbs2subd`

| File (relative to this directory) | Upstream path | Upstream commit | Copied for | Local changes |
|---|---|---|---|---|
| `include/n2s/tolerances.hpp` | `core/include/n2s/tolerances.hpp` | `dea5d2e` | M1 | none |
| `include/n2s/nurbs/knot_vector.hpp` | `core/include/n2s/nurbs/knot_vector.hpp` | `dea5d2e` | M1 | none |
| `include/n2s/nurbs/basis.hpp` | `core/include/n2s/nurbs/basis.hpp` | `dea5d2e` | M1 | none |
| `include/n2s/nurbs/surface.hpp` | `core/include/n2s/nurbs/surface.hpp` | `dea5d2e` | M1 | none |
| `include/n2s/nurbs/detail/binomial.hpp` | `core/include/n2s/nurbs/detail/binomial.hpp` | `dea5d2e` | M1 | none |
| `src/nurbs/knot_vector.cpp` | `core/src/nurbs/knot_vector.cpp` | `dea5d2e` | M1 | none |
| `include/n2s/nurbs/differential.hpp` | `core/include/n2s/nurbs/differential.hpp` | `dea5d2e` | M2 | none |
| `src/nurbs/differential.cpp` | `core/src/nurbs/differential.cpp` | `dea5d2e` | M2 | none |
| `src/nurbs/basis.cpp` | `core/src/nurbs/basis.cpp` | `dea5d2e` | M1 | none |
| `src/nurbs/surface.cpp` | `core/src/nurbs/surface.cpp` | `dea5d2e` | M1 | none |
| `include/n2s/nurbs/projection.hpp` | `core/include/n2s/nurbs/projection.hpp` | `dea5d2e` | M11 | `ProjectionOptions::start`: an optional warm start that skips the grid search |
| `src/nurbs/projection.cpp` | `core/src/nurbs/projection.cpp` | `dea5d2e` | M11 | uses `ProjectionOptions::start` when set |
| `tests/third_party/n2s/test_knot_vector.cpp` (repo root) | `tests/nurbs/test_knot_vector.cpp` | `dea5d2e` | M1 | none |
| `tests/third_party/n2s/test_basis.cpp` (repo root) | `tests/nurbs/test_basis.cpp` | `dea5d2e` | M1 | none |

Upstream commit in full: `dea5d2e669df688012888efd5e07d43d8cd4b358`.

**Not copied:** upstream `tests/nurbs/test_surface.cpp`, because it depends on
`tests/support/analytic_nurbs` and through that on `n2s/nurbs/curve.hpp`.
`NurbsSurface` is instead checked against ADA's Bézier patch in
`tests/geom/test_convert.cpp`. Evaluation and all mixed partials up to third
order must agree, and the Bézier code has its own closed-form oracles.
Upstream `tests/nurbs/test_differential.cpp` is not copied for the same reason.
`tests/analysis/test_differential.cpp` covers `curvature_from_derivatives`
with an analytic sphere and exact polynomial paraboloids.

Upstream `tests/nurbs/test_projection.cpp` is not copied either (it needs
`analytic_nurbs`); `tests/geom/test_projection.cpp` projects onto ADA Bézier
patches converted with `to_nurbs`, where points are built as footpoint plus a
normal offset, so the answer is known.

When a later milestone needs another file, copy it from the current upstream
commit, add a row above, and note any local change in the last column.
