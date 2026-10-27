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
- Individual nurbs2subd files copied on demand into `core/third_party/n2s/`
  (MIT notice kept; every file and local change listed in `SOURCE.md`).
  nurbs2subd is never linked as a library.
- `studio/`: Node 24 LTS (`studio/.node-version`), Electron (Forge + Vite
  plugin), TypeScript strict, React, three.js, official MCP TypeScript SDK
  (client, from M3), Vitest, oxlint + oxfmt. The npm dependency list is just as
  fixed as the C++ one: ask before adding a package. Keep `npm audit` clean.
- Python only for analysis scripts and, from Phase 4, ML. Validation:
  `cadquery-ocp` (OpenCASCADE) for `scripts/occt_check.py`, pinned in
  `scripts/requirements-occt.txt`, installed in `.venv`.

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
- Look at visual output before handing it over: Studio can capture itself
  (`npm start -- -- --scene=<file> --view=zebra --screenshot=<png>` opens the
  scene in that mode, saves the window and quits). Interaction (orbit, picking,
  hot reload) still needs the user: say so and ask.

## Commands
```
cmake --preset dev && cmake --build --preset dev
ctest --preset dev --output-on-failure
./build/dev/bin/ada --version
./build/dev/bin/ada report cases/examples/hood.ada.json      # exit 0 pass, 1 fail, 2 error
./build/dev/bin/ada solve cases/L2-hood-2x2-sym.ada.json -o results/hood.ada.json
./build/dev/bin/ada bench cases                              # scorecard; exit 0 only if every case passes
./build/dev/bin/ada export cases/L2-hood-2x2-sym.ada.json --solve -o results/hood.igs
./build/release/bin/ada solve cases/examples/hood-fit.ada.json --economy -o results/fit.ada.json   # fit to a target, CV economy
.venv/Scripts/python scripts/occt_check.py                   # OpenCASCADE re-measures every case; exit 0 = agrees
python agent/eval/run_eval.py                                # intent eval via claude -p (uses Claude Code usage)

npm --prefix studio ci
npm --prefix studio start          # needs a built kernel (build/release or build/dev)
npm --prefix studio run lint && npm --prefix studio run typecheck && npm --prefix studio test
npm --prefix studio run package    # unpacked app in studio/out/
```
