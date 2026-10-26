# NX verification (M6)

ADA measures its own continuity. This check has an independent tool, NX,
measure the same surfaces, so a shared mistake in ADA's surfaces and its
measurements cannot hide.

Without NX access, M6 was closed with OpenCASCADE instead
([occt-verification.md](occt-verification.md)), which agrees with `ada report`
on every case. The NX check is now optional. It would add what OpenCASCADE
cannot: a Class-A tool's view of the surfaces, with NX's tolerances and its
reflection analysis. Rhino 8 (a 90-day evaluation; `EdgeContinuity`, `Zebra`)
can stand in for it.

## 1. Make the files

From the repository root, after building:

```
mkdir -p results/iges
for scene in cases/*.ada.json cases/examples/*.ada.json; do
  ./build/release/bin/ada export "$scene" --solve -o "results/iges/$(basename "$scene" .ada.json).igs"
done
```

(CI does the same on Windows and uploads the files as the `case-iges` artifact
of every run, so they can also be downloaded from GitHub Actions.)

Every file holds one IGES entity 128 per patch, in millimetres. The entity
label is the patch id, cut to eight characters.

For each case, also run `ada report` on its solved form and keep the output
next to the NX numbers:

```
./build/release/bin/ada solve cases/L2-hood-2x2-sym.ada.json -o results/hood.ada.json
./build/release/bin/ada report results/hood.ada.json
```

## 2. In NX

1. **Import** the `.igs` file (File → Import → IGES). Check that the part is
   in millimetres and that each patch arrives as its own sheet body.
2. **Continuity:** run NX's surface continuity analysis (in recent versions:
   Analysis → Shape → Surface Continuity) across each shared edge listed in the
   case's report, for G0, G1 and G2. Note the maximum position gap (mm), the
   maximum angle (degrees) and the curvature deviation NX reports.
3. **Reflection:** switch on reflection (zebra) analysis on all sheets.
   Stripes should run straight through every G2 joint; on
   `hood-g1-only` they should kink at the front/rear joint.
4. **Symmetry** (L0s roof and L2 hood only): mirror the sheets in the XZ plane
   (Y = 0) and check continuity between each sheet and its mirror image along
   the centre line. G1 should hold, and by the argument in MILESTONES.md
   section 4 G2 should hold too, without ADA having asked for it.

## 3. What should come out

| File | Joint | ADA says | NX should show |
|---|---|---|---|
| hood | front ~ rear | G0, G1, G2 all ≈ 0 | G2 within tolerance |
| hood-g1-only | front ~ rear | G0, G1 ≈ 0; G2 off by 144 % | G1 within tolerance, G2 clearly not |
| hood-g2-broken | front ~ rear | G1 ≈ 0; G2 off by 17 % | G2 out of tolerance (smaller) |
| L1-g2-blend | inner ~ gap, gap ~ outer | G2 ≈ 0 both sides | G2 within tolerance both sides |
| L2-hood-2x2-sym | 4 internal joints, Y = 0 | G2 ≈ 0, mirror G1 ≈ 0 | G2 within tolerance, mirror G2 too |
| L2-panel-3x3 | 12 internal joints | G2 ≈ 0 | G2 within tolerance |

"Agrees" means: the same pass/fail verdict on every joint at the default
budgets (0.001 mm, 0.01°, 1 %), and, where NX gives numbers, values of the
same order. NX may express curvature deviation differently (absolute 1/mm,
radius, or percent); record whatever it shows.

## 4. Results

NX version: _______

| File | Joint | NX G0 (mm) | NX G1 (deg) | NX G2 | Zebra | Agrees with ADA? |
|---|---|---|---|---|---|---|
| hood | front ~ rear | | | | | |
| hood-g1-only | front ~ rear | | | | | |
| hood-g2-broken | front ~ rear | | | | | |
| L1-g2-blend | inner ~ gap | | | | | |
| L1-g2-blend | gap ~ outer | | | | | |
| L2-hood-2x2-sym | (worst of 4) | | | | | |
| L2-hood-2x2-sym | Y = 0 mirror | | | | | |
| L2-panel-3x3 | (worst of 12) | | | | | |

Notes:
