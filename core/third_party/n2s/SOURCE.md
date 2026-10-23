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
| `src/nurbs/basis.cpp` | `core/src/nurbs/basis.cpp` | `dea5d2e` | M1 | none |
| `src/nurbs/surface.cpp` | `core/src/nurbs/surface.cpp` | `dea5d2e` | M1 | none |
| `tests/third_party/n2s/test_knot_vector.cpp` (repo root) | `tests/nurbs/test_knot_vector.cpp` | `dea5d2e` | M1 | none |
| `tests/third_party/n2s/test_basis.cpp` (repo root) | `tests/nurbs/test_basis.cpp` | `dea5d2e` | M1 | none |

Upstream commit in full: `dea5d2e669df688012888efd5e07d43d8cd4b358`.

**Not copied:** upstream `tests/nurbs/test_surface.cpp`, because it depends on
`tests/support/analytic_nurbs` and through that on `n2s/nurbs/curve.hpp`.
`NurbsSurface` is instead checked against ADA's Bézier patch in
`tests/geom/test_convert.cpp`. Evaluation and all mixed partials up to third
order must agree, and the Bézier code has its own closed-form oracles.

When a later milestone needs another file (expected: `differential` for M2,
`projection` for M11), copy it from the current upstream commit, add a row
above, and note any local change in the last column.
