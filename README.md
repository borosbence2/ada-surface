# ADA Surface

**AI-Driven A-surfacing.** You describe the surface you want, with boundary
curves, a target shape and plain language, and an agent builds it. A
deterministic Bézier kernel holds the continuity, and every result comes with
a quality report.

> Status: M11. Scenes are written as curves through points plus recipes;
> `ada solve` turns them into G2-continuous Bézier surfaces, `ada bench`
> checks the case ladder against its budgets, and `ada export` writes IGES,
> which OpenCASCADE measures again independently. An agent builds and edits
> scenes through `ada-mcp`'s tools, guided by a Class-A craft guide, while
> Studio shows the result live; an intent eval scores it (12/12 at baseline).
> Surfaces can be fitted to a scan or mesh with as few control points as the
> tolerance allows (M11), and the agent rebuilds a real hood, the DrivAer's,
> from its CAD tessellation (M12, case L3). The chat inside Studio (M9) is on
> hold. See [MILESTONES.md](MILESTONES.md).

## Building

Kernel (C++20; CMake ≥ 3.24, Ninja, and GCC, Clang or MSVC):

```
cmake --preset dev && cmake --build --preset dev
ctest --preset dev
```

Solve a scene and check it from the command line (exit code 0 pass, 1 fail,
2 error). The files in `cases/` hold only curves through points and recipes:

```
./build/dev/bin/ada solve cases/L2-hood-2x2-sym.ada.json -o results/hood.ada.json
./build/dev/bin/ada report results/hood.ada.json
./build/dev/bin/ada bench cases                        # every case against its budgets
./build/dev/bin/ada export results/hood.ada.json -o results/hood.igs
```

A scene can also follow a **target**, a scan or CAD mesh (OBJ, STL or PLY)
next to it. Its from_boundaries patches then fit the target's points, the
report gives their max / RMS / 95th-percentile distance against the fit
budgets, and Studio colours the points by deviation. `--economy` starts at
the curves' degrees and raises patches only as far as the budgets need:

```
./build/release/bin/ada solve cases/examples/hood-fit.ada.json --economy -o results/fit.ada.json
```

`cases/L3-hood-from-target.ada.json` does this with real data: the DrivAer
hood (DrivAerML, CC BY-SA 4.0, see [cases/targets](cases/targets/README.md))
rebuilt as a 4 x 4 G2 network within 0.5 mm. It was built through the agent's
tools, the way an agent would build it.

[docs/occt-verification.md](docs/occt-verification.md) describes the
independent check: OpenCASCADE reads the exported IGES and measures every
joint again (`scripts/occt_check.py`, also run in CI). It needs a Python venv
with `pip install -r scripts/requirements-occt.txt`.
[docs/nx-verification.md](docs/nx-verification.md) describes the same check in
NX.

Studio (Node 24 LTS; build the kernel first, Studio starts it as `ada-mcp`):

```
cd studio
npm ci
npm start -- -- --scene=../cases/examples/hood-g1-only.ada.json
```

Open a file from `cases/` and press **Solve**. It writes the solved control
points back into that file; `git checkout cases/` returns it to the authored
version, which the tests check. Compare the two hood examples in `cases/examples/` in Zebra mode:
in `hood.ada.json` (G2 across the front/rear joint) the stripes flow through
the joint; in `hood-g1-only.ada.json` (G1 only) they kink there.

### Driving it from Claude Code

`ada-mcp` is an MCP server, so Claude Code (or any MCP client) can build and
change scenes through it:

```
cmake --preset release && cmake --build --preset release
cd <repo>
claude mcp add ada -- <repo>/build/release/bin/ada-mcp
claude mcp add ada -- <repo>/build/release/bin/ada-mcp --scene <repo>/my.ada.json   # with a scene open
```

Point it at the release build: a running server locks its executable on
Windows, which would otherwise block rebuilding `build/dev`. The server is
registered for the directory `claude mcp add` runs in, so start Claude Code
there. With only the VS Code extension installed, `claude` is not on the PATH;
the extension ships it as
`%USERPROFILE%\.vscode\extensionsnthropic.claude-code-<version>-win32-x64
esources
ative-binary\claude.exe`.

Then ask in plain language, for example "make a new scene for a hood, 1200 x
1400 mm, symmetric at Y = 0, as a 2 x 2 network of curves, G2 everywhere".
Open the same file in Studio to watch it change. The agent works with these
tools:

| Tool | What it does |
|---|---|
| `scene_new`, `scene_open`, `scene_summary` | create, open and list a scene |
| `curve_from_points` | feature lines through points (several at once; `replace` to change them) |
| `patch_from_boundaries`, `network_from_curves`, `patch_blend` | surfaces from four curves, from a curve grid, or filling a gap between two edges |
| `edge_constrain`, `symmetry_constrain` | G0/G1/G2 across an edge, or at Y = 0 against the mirror image |
| `patch_set_degree`, `fairness_set` | more freedom for the solver; fairest surface versus least change |
| `quality_report`, `get_selection` | the full report; what the user picked in Studio ("this edge") |
| `undo`, `redo`, `solve`, `export_iges` | history, re-solve a hand-edited file, IGES out |
| `target_add`, `target_info` | attach a scan or mesh to fit to; its outline, corners and sides |
| `network_from_target` | a curve network laid on a target: outline curves along its edge, sections across it |
| `fit_set`, `budgets_set` | make the patches follow the target (with CV economy); set the budgets they are judged by |

No tool sets control points. Every edit solves the scene and writes the file;
an edit that cannot be solved is refused with an error code, the reason and
what to try, and leaves the file as it was.

[docs/intent-eval.md](docs/intent-eval.md) is the agent's scorecard: thirteen
intent tasks run headless through Claude Code (`agent/eval/run_eval.py`),
each judged by the scene and quality report it leaves behind.

Claude Code started in this repository also loads the craft guide,
[`.claude/skills/class-a-surfacing`](.claude/skills/class-a-surfacing/SKILL.md):
how to work with these tools the way a Class-A modeller would, what every
quality check means, and what to try when one fails.

## Idea

Class-A surfacing time goes into patch-layout decisions and endless
highlight-line inspection loops. LLMs are good at translating intent and at
reading a report; they are bad at producing exact G2 geometry. So in ADA:

- the **agent** (Claude, via MCP) chooses recipes, constraints and weights;
- the **kernel** solves for the control points and enforces G0/G1/G2 and Y=0
  symmetry exactly;
- the **quality report** (continuity, curvature, highlight-line wiggle, CV-hull
  cleanliness) closes the loop. The agent reads it and explains it;
- **ADA Studio** (Electron) shows the result live with zebra, highlight-line
  and curvature diagnostics, hosts the chat, and lets you pick an edge and say
  "make *this* edge G2". Claude Code can drive the same kernel instead;
- **IGES export** takes the result into NX for verification and downstream work.

It starts with the easy cases (single patches, curvature-continuous blends,
small symmetric all-quad networks such as a hood or roof) and grows from there.

## Planned components

| Component | What it is |
|---|---|
| `ada_core` | Geometry kernel: Bézier surfaces, constraints, solver, analysis |
| `ada` | CLI: solve, report, bench, export |
| `ada-mcp` | MCP server exposing the kernel to an agent and to Studio |
| `studio/` | ADA Studio: Electron app (React + three.js) with viewport, diagnostics and chat |
