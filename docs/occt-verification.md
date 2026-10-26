# OpenCASCADE verification (M6)

ADA measures its own continuity, so a mistake shared by its surfaces and its
measurements could hide. M6 therefore needs a second, independent tool to
measure the same surfaces. That was meant to be NX
([nx-verification.md](nx-verification.md)); without NX access it is
[OpenCASCADE](https://dev.opencascade.org/) 7.9.3, the open-source geometry
kernel behind FreeCAD, used through the CadQuery OCP Python bindings.
`scripts/occt_check.py` runs the check, and CI runs it on every push.

## What is independent

For every case and example, the script solves the scene with `ada solve`, takes
ADA's verdicts from `ada report --json`, and exports IGES with `ada export`.
From there on everything is OpenCASCADE's:

| Step | Done by |
|---|---|
| Reading the IGES file | OCCT's IGES reader (`IGESControl_Reader`), not ADA's test reader |
| Sheet validity | `BRepCheck_Analyzer` on every face |
| Finding the joints | geometry only: every pair of boundaries that lie on each other (within 1 mm), and every boundary on Y = 0. ADA's joint list is only used afterwards, to compare |
| Points, derivatives, normals, principal curvatures | OCCT's B-spline evaluator and `GeomLProp_SLProps` |
| Closest point on the neighbouring boundary | `GeomAPI_ProjectPointOnCurve` |
| A second opinion on G1/G2 | OCCT's own `LocalAnalysis_SurfaceContinuity` |

What is shared with ADA, on purpose: the *definitions* of the three numbers, so
they can be compared value for value. G0 is the largest distance from a
boundary to its neighbour's boundary (mm). G1 is the largest angle between
the normals (degrees). G2 is the largest relative difference in normal
curvature across the joint, `|k_a − k_b| / max(|k_a|, |k_b|, 1e-5 /mm)`. The
curvatures themselves come from OCCT's principal curvatures and directions,
not from ADA's fundamental forms. The script samples 65 points per joint, a
superset of ADA's 33.

Two further numbers have no counterpart in ADA:

- **G2 over all directions.** The largest normal-curvature difference over
  every tangent direction (the larger eigenvalue of the difference of the two
  second fundamental forms), relative to the largest principal curvature. ADA
  only compares the direction across the joint. Along a G1 joint the normal
  is shared, so the forms already agree in the along-edge direction and in
  the twist. This number checks that argument on real data.
- **Mirror G2.** At every Y = 0 edge, the sheet is compared with its own
  mirror image. ADA does not measure this: by MILESTONES.md §4, mirror G1
  implies mirror G2. OpenCASCADE now measures it.

The IGES file is also checked against the scene: one entity 128 per patch, in
millimetres, labels equal to the patch ids, non-rational, a single span on
[0, 1]², degrees as in the scene, and every pole equal to its control point
(within 1e-9 mm).

## Agreement rule

For every check `ada report` makes (G0, G1, fold and G2 on each joint; plane
distance and normal tilt on each Y = 0 edge), OpenCASCADE's value is judged
against the same budget. The two tools agree when the verdicts match and, where
the value fails, the two values are within 10 % of each other. In addition:

- every shared boundary OpenCASCADE finds must be a joint ADA reports, and
  every joint ADA reports must exist in the geometry;
- mirror G2 must be within the G2 budget wherever the scene asks for G2 at
  Y = 0.

OCCT's `LocalAnalysis` verdict is reported but not used in the agreement rule.
Its 7.9.3 G2 test is a heuristic (it averages the absolute components of the
principal directions) and ignores normal orientation, which the script
corrects by reversing the second sheet. In practice it agrees anyway; see
below.

## Running it

```
python -m venv .venv
.venv/Scripts/pip install -r scripts/requirements-occt.txt     # .venv/bin/pip on Linux
.venv/Scripts/python scripts/occt_check.py                     # every case and example
.venv/Scripts/python scripts/occt_check.py cases/examples/hood.ada.json
```

It uses the most recently built `ada` (`--ada` to choose one) and works in
`results/occt/`. The output is a Markdown report. The exit code is 0 when
OpenCASCADE agrees with ADA everywhere, 1 when it does not, and 2 on errors.
Pressing Solve in Studio writes control points into a case file, so the
check then runs on that state. `git checkout cases/` restores the authored
cases.

## Results (2026-10-26)

ADA `c31e12c` (Debug, GCC 14.2; a Release build solves the cases bit for bit
the same), OpenCASCADE 7.9.3 (`cadquery-ocp` 7.9.3.1.1), Python 3.10.11 on
Windows. **OpenCASCADE agrees with ADA on every check of every case.**

Worst values per case over its joints and Y = 0 edges, ADA / OpenCASCADE.
Budgets: 0.001 mm, 0.01°, 1 %.

| Case | Joints | Y = 0 edges | G0 (mm) | G1 (°) | G2 | Mirror G2 (OCCT) | Verdict ADA / OCCT |
|---|---:|---:|---|---|---|---|---|
| L0-roof-centre | 0 | 0 | – | – | – | – | pass / pass |
| L0s-roof-centre-sym | 0 | 1 | 0 / 0 | 4.0e-16 / 3.6e-16 | – | 4.2e-16 | pass / pass |
| L1-g2-blend | 2 | 0 | 1.1e-12 / 1.3e-12 | 1.1e-12 / 1.1e-12 | 1.1e-10 / 1.1e-10 | – | pass / pass |
| L2-hood-2x2-sym | 4 | 2 | 9.2e-13 / 8.1e-13 | 6.2e-13 / 6.2e-13 | 7.4e-13 / 7.4e-13 | 9.6e-16 | pass / pass |
| L2-panel-3x3 | 12 | 0 | 1.6e-12 / 1.8e-12 | 1.6e-12 / 1.6e-12 | 1.8e-12 / 1.7e-12 | – | pass / pass |
| hood | 1 | 2 | 0 / 0 | 0 / 0 | 1.1e-14 / 1.1e-14 | 5.9e-16 | pass / pass |
| hood-g1-only | 1 | 2 | 0 / 0 | 0 / 0 | **1.443 / 1.443** | 5.9e-16 | fail / fail |
| hood-g2-broken | 1 | 2 | 0 / 0 | 0 / 0 | **0.171 / 0.171** | 5.9e-16 | fail / fail |

- **The decisive pair.** On hood, OpenCASCADE measures the front/rear joint at
  1.1e-14 (G2). On hood-g1-only it measures the same 144 % as ADA, and on
  hood-g2-broken the same 17 %. OCCT's own `LocalAnalysis` calls the joint G2
  at 65 of 65 samples on hood, 0 of 65 on hood-g1-only, and 12 of 65 on
  hood-g2-broken, where the error is smaller and varies along the edge.
- **The IGES files.** Every file reads as one valid face per patch with the
  right label, degree and single span. All poles match the scene to within
  5.1e-13 mm, which is the rounding of the writer's 16 significant digits
  (`{:.15E}`).
- **G2 over all directions** stays at the noise level wherever the
  across-joint G2 does (at most 1.7e-11 on the passing joints). Across-joint
  G2 is therefore enough once G1 holds, as argued above.
- **Mirror G2** is below 1e-15 on every Y = 0 edge: OpenCASCADE confirms that
  mirror G1 brings G2 with it.
- **One finding outside ADA's report.** In L1-g2-blend, `inner.UMin` lies on
  Y = 0, but the scene has no symmetry link there. Mirrored, it would meet its
  mirror image at 0.044° (twice that as a crease). The case is a blend test,
  not a half model, so this is not a disagreement. A real part ending at the
  centre line would need the link.

### Checking the checker

Run on copies of hood with a defect introduced on purpose:

| Copy | ADA | OpenCASCADE | Agree |
|---|---|---|---|
| rear sheet lifted 0.01 mm (G0 break) | G0 1.00e-2 mm, fail | G0 1.00e-2 mm, fail | yes |
| rear's first inner row lifted 0.5 mm (G1 break) | G1 0.143°, G2 8.8 %, fail | G1 0.143°, G2 8.8 %, fail | yes |
| front/rear link deleted from the scene | no joint, pass | shared boundary that ADA does not report | **no** (exit 1) |

## What this does not cover

OpenCASCADE is a general-purpose kernel, not an OEM Class-A tool. This check
shows that the IGES files are right and that ADA's continuity numbers are right.
It does not show that the surfaces *look* right to a surfacing specialist, or
that NX's tolerances and its continuity analysis read them the same way. The
NX check in [nx-verification.md](nx-verification.md), or a Rhino check with
EdgeContinuity and Zebra, stays open as an optional extra for whenever access
exists.
