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

## Reading results

Every edit returns the same structured fields:

- `changed`: what the edit did. `undo` and `redo` report what they moved over.
- `details`: per-tool extras, such as each curve's `fit_mm` or the patches a
  degree change reached.
- `solve`: `unknowns`, `milliseconds`, `max_residual_mm` (how exactly the
  constraints hold; around 1e-12 is exact), and `curves_moved`.
- `quality`: `pass`, and `failed`, a list of `{where, check, value, budget}`.

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

Budgets belong to the scene, and the user owns them. A budget that is not set
is not checked. Never relax a budget to make a report pass; propose it
instead, with the reason.

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
