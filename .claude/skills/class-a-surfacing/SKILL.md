---
name: class-a-surfacing
description: How to build, change and judge Class-A automotive surfaces with the ADA Surface kernel (the `ada` MCP tools). Covers the working loop, ADA's conventions (axes, patch edges, curve networks), reading the quality report, what to try when a check fails, and when to stop and ask. Use whenever you work with ada tools or .ada.json scenes, or answer surface-quality questions about them.
---

# Class-A surfacing with ADA

Class-A surfaces are the visible exterior of a car. They are judged by their
reflections: highlight and zebra lines must flow without wobbles or kinks,
across every joint between patches. ADA splits the work. You decide *what*
the surface should be: the feature lines, the patch layout, the continuity,
the trade-offs. The kernel computes the control points exactly and reports
quality against budgets.

You never set control points, and you never edit `.ada.json` files by hand.
The tools validate every edit, solve it, write the file and record it for
undo; a hand edit skips all of that. If a tool you need is missing, say so.

## The loop

1. **Understand the intent.** Find out the part (hood, roof, door...), its
   rough size and position, whether it is symmetric at Y = 0, the continuity
   wanted (G2 unless the user says otherwise), and any hard points or
   feature lines that must not move. If size or symmetry is missing, ask. For
   smaller gaps, state your assumption and go on.
2. **Inspect** before changing anything: `scene_summary`, `quality_report`,
   and `get_selection` whenever the user says "this" or "here".
3. **Change one thing per edit**, so each result shows the effect of that one
   change.
4. **Read the result every edit returns** (see "Reading results"). An edit
   that cannot be solved is refused and changes nothing; read the error's
   `code`, `message` and `try`.
5. **Look at it** when ADA Studio is available (see "Looking"). A report can
   pass while something looks wrong, and the user judges with their eyes.
6. **Explain** in designer terms: which joint or patch, what changed, the
   numbers with units, and what you would do next.
7. **Stop** when the intent is met and the report passes. Do not keep
   polishing without being asked.

## Conventions

- Millimetres. Z up. Y = 0 is the vehicle centre plane. X runs along the
  car; the bundled cases put the front at x = 0 and increase x rearwards.
- For a symmetric part, model the half with **y ≥ 0** and join its centre
  edge to its mirror image with `symmetry_constrain`. **G1 there already
  gives G2**, because a mirror-G1 joint has no curvature jump.
- A patch is a Bézier surface over (u, v). **UMin and UMax are the edges at
  u = 0 and u = 1, so they run along v; VMin and VMax run along u.** Write
  an edge as `patch.Edge`, for example `hood_0_0.UMax`.
- Ids use letters, digits, `_` and `-`.
- Continuity: **G0** means the edges touch, **G1** means the tangent planes
  match (zebra stripes kink but do not break), and **G2** means the curvature
  matches too (stripes flow straight through). Visible Class-A joints are G2.
  G1 or G0 belong only where the design wants a crease or a break.

### Curve networks

`network_from_curves` turns a grid of curves into a grid of patches, with
every internal edge joined at the continuity given. Take the 2 × 2 hood (u
runs outwards in y, v rearwards in x):

```
along_u = [[u00, u01],    # line at the front (x station 0): y 0..350, then 350..700
           [u10, u11],    # line at x station 1
           [u20, u21]]    # line at the rear (x station 2)
along_v = [[v00, v01],    # line on the centre plane (y station 0): x 0..600, then 600..1200
           [v10, v11],    # line at y station 1
           [v20, v21]]    # outer line (y station 2)
```

`along_u` holds rows + 1 lines, each a list of one curve per column;
`along_v` holds cols + 1 lines, each a list of one curve per row. Patch
`<prefix>_<r>_<c>` is bounded by `along_u[r][c]` (VMin), `along_u[r+1][c]`
(VMax), `along_v[c][r]` (UMin) and `along_v[c+1][r]` (UMax). So the
patches `<prefix>_<r>_0` have their UMin edge on the first `along_v` line,
which for a half model is the centre plane: those are the edges for
`symmetry_constrain`.

## Building geometry

**Curves through points** (`curve_from_points`). Degree 3 is the default and
almost always right. With exactly degree + 1 points (4 for a cubic) the
curve passes through every point. With more, it is a least-squares fit
through the first and last point, and the result's `fit_mm` says how far it
misses the others. Give points in order along the line, spread over its
whole length.

**Gentle crowns.** Parameters follow chord length, so a cubic through points
sampled from a simple crown (a parabola) is close to it but not exactly it.
On a nearly flat panel that difference can show up as a highlight
inflection. If a gentle panel shows one, fit its crown curves at degree 2
through three points before reaching for anything else (in the eval, four
evenly spread points at degree 3 did not remove it; degree 2 did).

**Corners must be shared exactly.** Curves that meet at a corner of a patch
must have the identical end point. Compute each node once and reuse its
coordinates; do not recompute or round them separately per curve.

**Lines through nodes.** A feature line crossing several patches is given as
one curve per patch. Where the curve segments meet, G1 or G2 across the
joint needs them to continue each other's tangent and curvature. If they do
not, the solve makes them give way, and the result reports it as
`solve.curves_moved` (the largest distance between a curve as given and
the patch edge solved on it). The bundled networks move theirs up to 0.07 mm
(2 x 2 hood) and 0.11 mm (3 x 3 panel), which is normal for segments fitted
one by one. Tell the user when it is more than about 0.1 mm, because their
lines changed. If those lines matter (hard points, feature lines), sample all
segments of a line from one smooth function so they already continue each
other, or relax that joint.

**Patch layout.** Use four-sided patches only. Prefer few, large patches, and
put patch boundaries where the shape changes character: a feature line, the
start of a strong turn. Do not split just to get more freedom; raising the
degree does that.

**Blends** (`patch_blend`) fill a gap between two existing edges, joined to
both sides. Degree across is 2k + 1 for Gk (5 for G2). The two edges need the
same degree along them.

## Fitting to a target

When the user gives a shape to match (a scan, a CAD tessellation, "rebuild
this hood"), the target leads, and the network's job is to carry it.

1. **Attach it** with `target_add {id, file}`. Use `sample: "vertices"` for a
   CAD tessellation: its vertices lie on the CAD surface, while its flat
   facets cut below a curved one. Keep the default `"surface"` for a scan.
   `scale` converts the file's units (1000 for metres).
2. **Read its outline** with `target_info`: bounds, the axis it faces, and
   for each outline loop its corners (where it turns sharply) and the sides
   between them, with sides in Y = 0 flagged.
3. **Lay the network** with `network_from_target {prefix, corners, rows,
   cols}`. Give four corners in order round the outline. corners[0] →
   corners[1] becomes the first row line (u grows along it), and corners[0] →
   corners[3] the first column line (the first column's UMin). For a half
   model, corners = [front centre, front side, rear side, rear centre] gives u
   outwards and v rearwards, the layout of the bundled hoods, with the centre
   line on UMin. Outline lines pass through every outline point at degree 5,
   so the patch edges follow the target's edge. Inner lines are sections
   through the target at degree 3. The result lists the edges in Y = 0:
   `symmetry_constrain` each of them.
4. **Set the user's tolerance** as a budget with `budgets_set` ("within 0.5
   mm" is `{"fit_max_mm": 0.5}`). Then **set the fit last**, with `fit_set
   {target, economy: true}`. From then on every edit solves the fit, which
   takes seconds rather than milliseconds, so build and constrain first.
5. **Read the fit** in every result. `fit` has the max, RMS and p95 distance
   over the target's points, and `outside`: points beyond the patches, where
   the network does not cover the target. `economy` lists the patches CV
   economy raised and whether the fit budgets are met.

**Rows and columns.** Start with as few patches as the shape allows, and let
the fit say whether to add more. The DrivAer hood (about 1.1 × 0.7 m, half
model) holds 0.5 mm at 4 × 4, at degree 5; at 3 × 3 its patches had to rise
to degree 7. More patches cost joints to keep G2; more degree costs economy.

**Weight.** `weight` (default 1000) sets how strongly the data leads against
the fairness. On clean CAD data a low weight (10 for the DrivAer hood) lets
the fairness even out the control nets while the fit stays within budget. On
a noisy scan a lower weight keeps the noise out of the surface. Read the fit
after each change.

**CV economy decides the degrees** while it is on: each solve starts the
patches at their curves' degrees, so `patch_set_degree` does not last. Turn it
off (`fit_set {economy: false}`) to set degrees by hand.

**The target's own shape.** A real panel may bend the other way in places
(the DrivAer hood curls up at its rear outer corner), so a
`curvature_reversal` on a fitted panel can be the target, not a defect. Check
where the target itself bends before calling it either, and tell the user;
whether that budget moves is their call.

**Zig-zag rows on a fitted network.** G2 joints between patches of
different size space control points unevenly (the solve matches parameter
speed across a joint), so a fitted network can show `cv_zigzag_rows` with no
visible wobble. Check `highlight_inflections` and the zebra before calling it
a defect. Do not trade the fit for them: degree-3 outline curves halved the
DrivAer hood's zig-zag rows but doubled its deviation (0.21 to 0.41 mm) and
moved its outline by up to 3.5 mm. Keep the outline faithful and propose a
zig-zag budget with that reason instead (the L3 case allows 12 per patch).

## Reading results

Every edit returns the same structured fields:

- `changed`: what the edit did. `undo` and `redo` report what they moved over.
- `details`: per-tool extras, such as each curve's `fit_mm` or the patches a
  degree change reached.
- `solve`: `unknowns`, `milliseconds`, `max_residual_mm` (how exactly the
  constraints hold; around 1e-12 is exact), and `curves_moved`.
- `quality`: `pass`, and `failed`, a list of `{where, check, value, budget}`.
- `fit` and `economy`, when the scene is fitted to a target (see above).

`quality_report` gives everything: per joint the measured gap, angle and
curvature deviation; per patch curvature, highlight-line and control-net
statistics.

### The checks

| Check | Where | Meaning | Fails when |
|---|---|---|---|
| `g0_gap_mm` | joint | largest distance between the two edges | > budget (0.001 mm) |
| `g1_angle_deg` | joint | largest angle between the normals | > budget (0.01°) |
| `fold` | joint | both patches leave the edge to the same side | ever |
| `g2_relative` | joint | normal-curvature jump across the joint, relative | > budget (1 %) |
| `g0_plane_distance_mm` | Y = 0 edge | how far the edge is from the centre plane | > G0 budget |
| `g1_normal_tilt_deg` | Y = 0 edge | how far the normal tilts out of the plane | > G1 budget |
| `highlight_inflections` | patch | wiggles in a highlight line (most in any line) | > `max_highlight_inflections` |
| `highlight_curvature_rate` | patch | how fast highlight lines change bend | > `max_highlight_curvature_rate` |
| `curvature_reversal` | patch | mean curvature changes sign: an S-bend | budget forbids it |
| `cv_zigzag_rows` | patch | control-net rows that turn back and forth | > `max_zigzag_rows` (0) |
| `degree` | patch | the patch's highest degree | > `max_degree` |
| `degenerate_samples` | anywhere | points with no defined normal or curvature (collapsed edge) | ever |
| `unsolved` | patch | the stored geometry is only a starting guess | ever; edits solve it |
| `fit_max_mm` | fit | largest distance from a target point to the surface | > budget |
| `fit_rms_mm` | fit | RMS of those distances | > budget |
| `fit_p95_mm` | fit | 95th percentile of those distances | > budget |

Budgets belong to the scene, and the user owns them. A budget that is not set
is not checked. Set one with `budgets_set` when the user states it ("within
0.5 mm"). Never relax a budget to make a report pass; propose it instead, with
the reason, and change it only once the user agrees.

## When something fails

- **`g2_relative` on a joint,** or `constraints_contradict` naming G1/G2
  joints. A degree-3 patch has few free rows of control points; G2 on two
  opposite sides, plus symmetry, can use them all up. Raise the degree
  across those joints with `patch_set_degree` (4 or 5). It carries the change
  to linked neighbours and keeps the current surface as the start. Check that
  the joint is actually linked at G2.
- **`highlight_inflections` / wavy reflections.** The surface wobbles. Causes,
  most likely first: boundary curves with a wobble (too many points, noisy
  points, a fit pulled through a stray point); segments of one line that do
  not continue each other; a degree higher than the shape needs. Refit the
  curves with fewer, cleaner points (`replace: true`); keep fairness at E3.
  Do not raise the degree to fix waviness: it adds room to wobble.
- **`curvature_reversal`** where the budget forbids it. A boundary curve has an
  S in it, or two opposite boundaries bend opposite ways. Find which curve
  and refit it.
- **`cv_zigzag_rows`.** The control net folds back and forth: usually the
  degree is too high for the boundaries, or curves fight at a node. Lower the
  degree, or fix the curves.
- **`degree` over budget.** Lower it if the joints still hold; otherwise ask
  whether the budget can move.
- **`curves_do_not_meet`.** The four curves do not form a loop: one is on the
  wrong edge, or an end point differs. Compare the end points `scene_summary`
  lists for each curve.
- **`edges_do_not_meet` / `not_on_centre_plane`.** You named the wrong edge.
  The error's `try` names the edge that does lie there, and `get_selection`
  lists the edges along a picked one.
- **`degree_mismatch`** (from `patch_blend`). The two edges a blend joins
  need the same degree along them; raise the lower one. Linked edges of
  different degrees need nothing: the solve raises the lower side itself.
- **`constraints_contradict`.** Read `details.conflicts`: every constraint
  taking part, each with its residual in mm. Typical pairs: a symmetry link
  against a curve that is not exactly in Y = 0 (refit the curve with y = 0);
  G2 on several sides of a degree-3 patch (raise the degree); a joint to a
  fixed patch (only the other side can move).
- **A fit over budget** (`fit_max_mm`, `fit_rms_mm`, `fit_p95_mm`). With CV
  economy on, the result says whether raising degrees ran into the
  `max_degree` cap. If it did, the network needs more rows or columns: `undo`
  back past `network_from_target` and lay it again finer. If `fit.outside` is
  above 0, the network does not cover the whole target: check the corners.
- **`layout_failed`** (from `network_from_target`). The corners are not in
  order round one outline loop, a side has too few outline points for the
  rows or columns asked, or an inner line leaves the target (an outline that
  bends inwards): pick corners and counts the outline can carry.

**Degree or split?** Raise the degree when the shape is right but the
continuity needs more freedom. Split (rebuild the network with another row or
column of curves) when one patch tries to hold two different shapes, for
example a flat area running into a tight turn. There is no split tool yet,
so rebuilding is a layout change: ask the user first.

**Fairest or least change?** `fairness_set` decides what the constraints leave
free. The default (fairness 1, E3, move 0) gives the fairest surface for the
current curves and constraints. For a local change on a surface the user is
happy with ("make this edge G2 but keep the rest"), set fairness 0, move 1
first: the solve then moves as little as possible. The setting stays in the
scene, and switching back to fairness 1 re-fairs the whole surface, so tell
the user when you change it.

## Looking

ADA Studio shows the scene file live; the user may have it open. To look
yourself, let Studio capture a screenshot (it opens, renders, saves and quits):

```
npm --prefix studio start -- -- --scene=<scene file> --view=zebra --screenshot=<file.png>
```

Views: `shaded`, `zebra` (stripes flow across G2, kink at G1, break at G0),
`reflection`, `highlight` (the lines the report counts), `mean`, `gaussian`
(curvature, fixed colour ranges). Disk-cache errors from a second Studio
instance are harmless. Interaction (orbit, picking) needs the user.

## When to stop and ask

- The intent leaves out something that changes the result: size, position,
  symmetry, which edge "this" is when nothing is selected.
- A fix would change what the user gave you: their curves moving more than a
  little, a different patch layout, a relaxed budget, less continuity than
  they asked for.
- The same check still fails after two different attempts. Explain what you
  tried, what the numbers did, and offer options.
- The user says it looks wrong but the report passes. Ask them to pick the
  spot in Studio (`get_selection`) and look at it in `highlight` or `zebra`.

## Explaining results

Say what the user would see, then the numbers: "the zebra stripes now run
straight across the front/rear joint: the curvature jump there went from 11 %
to about 1e-12, effectively zero". Name joints and patches as the tools do, with
millimetres and degrees. Mention `curves_moved` when the user's lines changed.
When something passes only narrowly or depends on an assumption you made,
say so.
