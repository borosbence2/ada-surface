# CLAUDE.md — ADA Surface

Intent-driven Class-A surfacing: an LLM agent drives a deterministic,
constraint-solving Bézier surface kernel through MCP tools, and ADA Studio
(Electron) shows the result live. The plan and the live task graph are in
`MILESTONES.md`. Read it before starting any task.

## How to work
- Work milestone by milestone, in plan order. Don't start Phase N+1 work before
  Phase N's exit criteria are met, unless the plan says so.
- One task = one focused commit. Plain descriptive subjects, no Conventional
  Commit prefixes. Committing and pushing to `origin/master` is allowed.
- Math code: write the test (analytic oracle) first, then the implementation.
- After each task, tick it in `MILESTONES.md` and add one line to `## Progress`,
  including any deviation from the plan.
- If a task is ambiguous or the plan looks wrong, stop and ask. Don't silently
  change scope. Decisions marked *proposed* may be challenged; *confirmed* ones
  may not.
- Ask before adding any dependency not listed below.

## Stack
- C++20, CMake ≥ 3.24 with presets, FetchContent at pinned tags, Ninja.
- Eigen 3.4, nlohmann/json, fmt, CLI11, Catch2 v3.
- NURBS basics vendored from nurbs2subd (`core/third_party/n2s/`, MIT notice kept).
- `studio/`: Electron (Forge + Vite plugin), TypeScript strict, React, three.js,
  official MCP TypeScript SDK (client), Vitest. The npm dependency list is just
  as fixed as the C++ one: ask before adding a package.
- Python only for analysis scripts and, from Phase 4, ML.

## Hard rules
- **The agent never writes control points.** No MCP tool sets raw CV
  coordinates. Geometry comes out of the solver from recipes + constraints;
  manual nudges enter the solver as soft targets.
- `core/` builds and tests with no graphics library and no network code.
- **One geometry source.** Studio never evaluates geometry; it renders the
  tessellations and fields that `ada-mcp` returns.
- Electron security: `contextIsolation` on, `nodeIntegration` off, renderer
  sandboxed, a narrow preload API. API keys live in `safeStorage`, never in
  the repo, scene files or logs.
- `double` everywhere in geometry code; millimetres; Z up; Y=0 is the vehicle
  centre plane.
- Tolerances and quality budgets are named constants (overridable per scene),
  never magic numbers.
- Every scalar visualization uses an explicit, displayed colour range. No
  auto-ranging.
- `ada bench cases/` must stay green; a change that worsens a case's scorecard
  needs a stated reason in the commit message.
- Warnings are errors (`/W4`, `-Wall -Wextra -Wpedantic`).
- Visual output can't be verified by tools: say so and ask the user to look.

## Commands (from M0 on)
```
cmake --preset dev && cmake --build --preset dev
ctest --preset dev --output-on-failure
./build/dev/bin/ada report cases/L0-roof-centre.ada.json
./build/dev/bin/ada bench cases/
npm --prefix studio ci
npm --prefix studio start          # expects ./build/dev/bin/ada-mcp
npm --prefix studio run typecheck && npm --prefix studio test
```
