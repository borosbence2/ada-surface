# ADA Surface

**AI-Driven A-surfacing.** You describe the surface you want, with boundary
curves, a target shape and plain language, and an agent builds it. A
deterministic Bézier kernel holds the continuity, and every result comes with
a quality report.

> Status: M3. The kernel checks scenes against Class-A budgets, and Studio
> shows them with zebra, highlight and curvature diagnostics. Nothing creates
> geometry yet (that is M4/M5). See [MILESTONES.md](MILESTONES.md).

## Building

Kernel (C++20; CMake ≥ 3.24, Ninja, and GCC, Clang or MSVC):

```
cmake --preset dev && cmake --build --preset dev
ctest --preset dev
```

Check a scene from the command line (exit code 0 pass, 1 fail, 2 error):

```
./build/dev/bin/ada report cases/examples/hood.ada.json
```

Studio (Node 24 LTS; build the kernel first, Studio starts it as `ada-mcp`):

```
cd studio
npm ci
npm start -- -- --scene=../cases/examples/hood-g1-only.ada.json
```

Compare the two hood examples in Zebra mode: in `hood.ada.json` (G2 across
the front/rear joint) the stripes flow through the joint; in
`hood-g1-only.ada.json` (G1 only) they kink there.

To drive the kernel from Claude Code instead (read-only tools for now):

```
claude mcp add ada -- <repo>/build/dev/bin/ada-mcp --scene <repo>/cases/examples/hood.ada.json
```

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
