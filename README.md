# ADA Surface

**AI-Driven A-surfacing.** You describe the surface you want, with boundary
curves, a target shape and plain language, and an agent builds it. A
deterministic Bézier kernel holds the continuity, and every result comes with
a quality report.

> Status: planning. Nothing builds yet. See [MILESTONES.md](MILESTONES.md).

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
