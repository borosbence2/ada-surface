# ADA Surface — plan and milestones

**ADA = AI-Driven A-surfacing.** An intent-driven ("vibe") Class-A surfacing
tool. The designer states intent with curves, a target shape and plain language.
An LLM agent turns that intent into operations on a **deterministic,
constraint-solving surface kernel**. The kernel guarantees the geometry, a
quality report checks it, and the designer watches the result live in
**ADA Studio** (an Electron app) and exports to NX for verification and
downstream work.

This file is the live task graph. Update the checkboxes and the `## Progress`
section as work lands, not at the end.

---

## 1. Thesis and design rule

Class-A surfacing time goes into two places: **layout decisions** (patch
boundaries, degrees, span counts) and **inspection loops** (highlight lines,
zebra stripes, curvature combs, nudging CVs, checking again). LLMs are good at
translating intent and at reasoning about a report. They are bad at producing
exact G2 geometry. So:

> **The agent never writes control points.** It picks recipes, constraints,
> weights and parameters. All geometry comes out of the solver. Every geometry
> change is followed by a quality report that the agent reads and explains.

What sets this apart from what is already out there: Alias Form Explorer
generates concept SubD, Vizcom does sketch to render or 3D, and Design Copilot
NX is a documentation assistant. None of them helps rebuild a concept as
production surfaces, which is the target here.

## 2. Scope: the case ladder

ADA is built bottom-up through difficulty levels. A level counts as done only
when its benchmark cases pass their quality budgets in CI **and** the exported
geometry checks out in NX.

| Level | Case | Typical part | Phase |
|---|---|---|---|
| **L0** | One untrimmed Bézier patch from 4 boundary curves, fair interior; optionally G1/G2 mirror-symmetric across Y=0 | Centre section of a roof or hood | 1 |
| **L1** | One patch with G1/G2 to fixed neighbours along full shared edges | Curvature-continuous blend between two fixed surfaces | 1 |
| **L2** | Small all-quad networks (2×1, 2×2, 3×3) with matched edges, internal G2 and Y=0 symmetry | Hood, roof, door skin without features | 1 |
| **L3** | L2 fitted to a target mesh or point cloud within a deviation tolerance, using as few CVs as possible | Hood rebuilt from a scan or a SubD concept | 3 |
| L4 | Character lines with controlled sharpness, feature lines that fade out | Shoulder line, fading crease | later |
| L5 | 3- and 5-sided corners, trimmed surfaces, set-back fillets, flanges | Most of a real body side | later |

**Why L0–L3 are "easy".** Patches are polynomial single-span Bézier patches,
and neighbours share degree and parameterization along shared edges. Under
those conditions the sufficient G1/G2 conditions are **linear in the CVs**
once the scale factors are fixed (section 4), so the whole problem becomes a
small linear least-squares problem with equality constraints. L4/L5 break that
assumption.

## 3. Architecture

```
core/          ada::core: geometry kernel. No graphics, no network.
  geom/        Bézier/B-spline curves and surfaces, degree elevation, split,
               boundary and cross-boundary CV rows (NURBS basics vendored
               from nurbs2subd, see D4)
  constraint/  fixed edges, G0–G2 edge pairs, Y=0 symmetry, pinned CVs
  solve/       fairness + fit + minimal-change energy, linear equality
               constraints, KKT solve (Eigen)
  analysis/    curvature, highlight/isophote fields + contour extraction,
               edge continuity measurement, CV-hull metrics, quality report
  fit/         target sampling, closest point, parameter correction (Phase 3)
  doc/         scene document: units, tolerances, entities, recipes,
               constraints, history; JSON IO
  io/          IGES 128 writer, OBJ/STL target reader
apps/
  ada          CLI: solve a scene, print/write the quality report, export, bench
  ada-mcp      MCP stdio server: kernel operations as tools over one scene
               file. `--ui-tools` adds viewer-only tools (tessellation, fields)
studio/        ADA Studio: Electron app (TypeScript, Vite, React, three.js)
  main/        starts ada-mcp as a sidecar process, MCP client, scene file
               watch, agent host (M9)
  preload/     narrow contextBridge API (the renderer has no Node access)
  renderer/    three.js viewport, diagnostic shaders, panels, chat (M9)
agent/         craft guide (Claude Code skill) + intent eval tasks
cases/         benchmark scenes L0–L3 with quality budgets
tests/         Catch2, mirrors core/
```

**One kernel, two kinds of client.** `ada-mcp` is the only thing that touches
geometry. Studio and the agent are both MCP clients of it. Studio never
evaluates geometry itself: it renders the tessellations and fields the kernel
returns.

**The agent loop** has two routes, and both work against the same scene file:
- **Route A, Claude Code (Phase 2, M8):** Claude Code runs its own `ada-mcp`.
  Each tool call loads `<name>.ada.json`, applies the operation, solves, writes
  the file and returns a short quality summary. Studio's own sidecar notices
  the file change and reloads within a second.
- **Route B, in Studio (M9):** Studio's main process hosts the agent, and the
  agent and the viewport share one sidecar. The chat lives in Studio's window.

Picking an edge or patch in Studio writes `selection.json`, and the agent reads
it through a tool. That makes requests like "make *this* edge G2" work on
either route. Undo is a snapshot stack in the document. The scene file is plain
JSON, so git history works as design history.

**The scene is recipe-based, not a pile of CVs.** A patch stores how it was made
(`from_boundaries`, `blend`, `network`, `fit_target`) plus its constraints.
`solve` recomputes the free CVs. The agent edits recipes and constraints, never
coordinates. Manual CV nudges (later, from Studio) enter the solver as soft
targets, so constraints still hold.

## 4. Geometry and quality definitions

Units: **millimetres**, `double`. Z points up and **Y=0 is the vehicle centre
plane**.

**Primitive.** Polynomial (non-rational) tensor-product Bézier patches, degree
3–7 in each direction, single span. Rational and multi-span geometry is only
for import and export.

**Continuity across a shared edge** (patch A at v=1, patch B at v=0, same
u-degree and parameterization; derivatives taken in v):
- G0: the boundary CV rows are identical (merged as one shared variable).
- G1 (sufficient, constant α > 0): `B_v(u,0) = α · A_v(u,1)`, i.e. CV rows 0–1
  of B are aligned with rows n−1..n of A at a fixed ratio.
- G2 (sufficient, constants α, γ): `B_vv(u,0) = α² · A_vv(u,1) + γ · A_v(u,1)`.
- With α and γ fixed, all of these are linear in the CVs. An optional outer
  loop optimises α and γ. The general forms α(u), β(u) (shear) are deferred
  to L4/L5.

**Y=0 mirror symmetry** (patch edge v=0 lying in y=0):
- G1: row 1 has the same x and z as row 0 (the cross-boundary derivative is
  perpendicular to the plane). Sufficient, and linear.
- **G2 needs no extra condition: mirror G1 along the whole edge implies mirror
  G2.** G1 means the normal has no y-component along the edge. Differentiating
  that along the edge gives `II(t, y) = 0`, so the second fundamental form is
  diagonal in the (edge, y) frame, and reflection maps it onto itself
  (confirmed numerically in `tests/analysis/test_continuity.cpp`). The
  condition `(B_{i,2} − 2B_{i,1} + B_{i,0}).y = 0` from the first draft of this
  plan is a parametric C2 choice about row 2's spacing, not a geometric
  requirement. M4 may use it to regularise row 2, but must not present it as
  "the G2 constraint".
- The first geometric condition beyond G1 at the plane is G3 (curvature with
  zero derivative across it); deferred to L4+.

**Fairness energies** are quadratic in the CVs and computed exactly through
Bernstein Gram matrices:
- E2 (thin plate): `∫ |S_uu|² + 2|S_uv|² + |S_vv|²`
- E3 (linearised curvature variation): `∫ |S_uuu|² + 3|S_uuv|² + 3|S_uvv|² + |S_vvv|²`

Both are parametric proxies, which is acceptable while parameterization is
controlled (L0–L3).

**Highlight field** (Beier & Chen 1994, plane-of-lights form). A light plane
holds parallel light lines with direction E and spacing d along D. Take the
surface normal line through P with direction N, and let Q be where it
intersects the light plane. Then `s(u,v) = D·(Q − A₀)` and the highlight lines
are the level sets `s = k·d`. The field does not depend on the view, so the
kernel computes it and the CLI can test it with no renderer; Studio draws its
isolines. The view-dependent **zebra and reflection lines** exist only as
Studio shaders.

**Quality report** (the contract; JSON, one per scene):

| Group | Measure | Default budget |
|---|---|---|
| Edge G0 | max position gap | 0.001 mm |
| Edge G1 | max normal angle deviation | 0.01° |
| Edge G2 | max relative cross-boundary normal-curvature deviation | 1 % |
| Symmetry | G0 distance from Y=0 and G1 normal tilt (G2 follows from G1) | as above |
| Surface | curvature extrema and sign changes in Gaussian/mean curvature over the patch | per case |
| Highlights | highlight-line wiggle: curvature sign changes and max \|dκ/ds\| along extracted contours | per case |
| CV hull | zig-zag count (sign changes of second differences along CV rows), max hull turning angle | 0 zig-zags |
| Economy | degree per direction, CV count | per case |
| Fit (L3) | max / RMS / 95th-percentile deviation to target | per case |

The default budgets are **placeholders**. OEMs set their own tolerances and
treat them as proprietary. Every budget is a named constant that a scene can
override. Confirm realistic values with a practising Class-A modeller (see
open questions).

## 5. Decisions

Status: **confirmed** = user decided; **proposed** = recommended default, may
be changed before the milestone that depends on it starts.

- **D1 Name** — ADA Surface, repo `ada-surface`, C++ namespace `ada`. The app is
  **ADA Studio**. *Confirmed.*
- **D2 Visibility and licence** — **public** GitHub repo. Licence still open:
  until a `LICENSE` file is added, the code is visible but all rights are
  reserved. *Confirmed (public); licence pending.*
- **D3 Stack** — C++20, CMake ≥ 3.24 with presets, FetchContent at pinned tags,
  Ninja. Same toolchain and CMake modules as nurbs2subd (including the MinGW
  static-runtime workaround). *Confirmed.*
- **D4 NURBS basics** — nurbs2subd is **not** used as a library. Copy only the
  individual files a milestone needs, when it needs them, into
  `core/third_party/n2s/` (namespace `n2s` kept, MIT notice kept).
  `SOURCE.md` there lists every copied file, the nurbs2subd commit it came
  from and any local changes. Expected so far: knot vector, basis and surface
  as oracles and for the single-span B-spline form (M1), differential geometry
  (M2), closest-point projection (M11). The `nurbs/` files depend only on
  Eigen, fmt and `n2s/tolerances.hpp`. *Confirmed.*
- **D5 Agent interface** — a hand-written MCP stdio server (JSON-RPC, tools only)
  in C++ with nlohmann/json; no SDK dependency on the kernel side. Clients:
  Claude Code first (M8), then an agent hosted inside Studio (M9). *Confirmed.*
- **D6 UI** — **ADA Studio, an Electron app** (React + three.js), with the kernel
  running as a native sidecar process that Studio talks to over MCP. This
  replaces the earlier Polyscope-then-Vulkan viewer plan. three.js makes the
  view-dependent zebra and reflection shaders straightforward, which Polyscope
  could not do. *Confirmed.*
- **D7 Primitive** — polynomial single-span Bézier patches with matched edges
  for L0–L3 (section 4). *Confirmed for now*; revisit at L4.
- **D8 Export** — a hand-written IGES writer (entity 128) first, since NX imports
  it directly. STEP AP214/242 later through optional OpenCASCADE behind a CMake
  option. *Confirmed.*
- **D9 Linear algebra** — Eigen 3.4, dense LDLT/QR at first. An L2 network is
  only a few hundred CVs, so sparse solvers come only when a case needs them.
  *Confirmed.*
- **D10 Studio tooling** — Electron Forge with its Vite plugin, TypeScript
  (strict), React, three.js, the official MCP TypeScript SDK (client side),
  Vitest. No UI component kit and no state-management library to start with.
  Versions pinned via `package-lock.json`. Electron security defaults:
  `contextIsolation` on, `nodeIntegration` off, renderer sandboxed. *Confirmed.*
- **D11 WebAssembly kernel** — an Emscripten build of `ada_core` for a no-install
  browser demo (GitHub Pages). *Deferred*: it adds a toolchain and nothing
  needs it before Phase 3. *Confirmed (deferred).*
- **D12 Studio's agent host** — the Anthropic client SDK (`@anthropic-ai/sdk`)
  in Studio's main process, driving the conversation over Studio's existing
  MCP client and sidecar (its tool runner, or a hand-written streaming loop
  where the UI needs more control), with the craft guide as instructions.
  Not the Claude Agent SDK: that runs the Claude Code binary as a second
  process, starts its own kernel, and brings file and shell tools that would
  have to be switched off to keep "the agent never writes control points".
  Checked against the current Anthropic docs, 2026-10-27. *Confirmed; the
  package is added when M9 resumes.*

## 6. Milestones

### Phase 0 — Foundations

#### M0 — Repository and build
- [x] CMake project, presets `dev` (Debug, sanitizers off-Windows), `release`, `ci`. (No `ci-nogfx`: the C++ build has no graphics at all.)
- [x] Port `CompilerWarnings`, `Sanitizers`, `GitVersion`, `Dependencies` modules from nurbs2subd.
- [x] FetchContent: Eigen 3.4, nlohmann/json, fmt, CLI11, Catch2 v3.
- [x] Empty `ada_core`, `ada`, `ada-mcp` targets; one passing test.
- [x] `studio/` skeleton (D10): Forge + Vite + TS + React + three.js; a window with an empty viewport; `typecheck` and Vitest scripts.
- [x] `.clang-format`, `.editorconfig`; GitHub Actions on Windows (MSVC) and Linux (GCC, Clang, ASan) + clang-format.
- [x] Studio CI job (`npm ci`, lint, typecheck, test, package) on Windows and Linux.
- Exit: CI green on all configurations, including the Studio job.

#### M1 — Geometry core
- [x] Copy the n2s files M1 needs (D4) and start `core/third_party/n2s/SOURCE.md`.
- [x] Bézier curve and surface: de Casteljau evaluation, derivatives up to 3rd order.
- [x] Degree elevation, split at a parameter, boundary row and cross-boundary row extraction.
- [x] Single-span Bézier ↔ B-spline conversion (for export and for n2s algorithms).
- Tests (oracle first): closed-form Bernstein evaluation; elevation and split leave the geometry unchanged (< 1e-12); derivatives against finite differences.
- Exit: all tests pass.

#### M2 — Analysis and quality report
- [x] Curvature fields (Gaussian, mean, principal) on a (u,v) grid.
- [x] Edge continuity measurement G0/G1/G2 sampled along shared edges and symmetry edges.
- [x] Highlight field `s(u,v)`, marching-squares contour extraction, highlight wiggle metric; isophotes.
- [x] CV-hull metrics, economy metrics.
- [x] Quality report JSON + pass/fail against named budgets; `ada report <scene>`.
- Tests: two patches built C2 by construction measure ~0 on every check; moving one CV in row 2 breaks G2 but not G1; a cylinder's highlights are straight and evenly spaced; a sphere patch has constant curvature 1/r.
- Exit: every report field has an oracle test that fails when the property is broken.

#### M3 — Kernel server and Studio v0
- [x] `ada-mcp` transport: stdio JSON-RPC with `initialize`, `tools/list`, `tools/call`; tools `scene_open`, `scene_summary`, `quality_report`.
- [x] `--ui-tools`: `tessellate` returns positions, normals, indices, (u,v) and per-vertex curvature and highlight fields as base64 little-endian arrays; `cv_hulls`; `boundary_combs`.
- [x] Studio main: start the sidecar, connect the MCP client, watch the scene file, re-tessellate on change; surface sidecar crashes in the UI.
- [x] Viewport: patches, CV hulls, boundary curvature combs, orbit camera, neutral studio lighting.
- [x] Diagnostic shaders: curvature colour maps with an explicit fixed range and a legend; highlight isolines from the kernel field (anti-aliased in screen space); zebra and reflection lines (view-dependent, shader only).
- [x] Colour each edge green/amber/red from the quality report; report panel.
- [x] Picking writes `selection.json`.
- [x] Package `ada-mcp` with Studio (`extraResource`) and resolve the sidecar path in dev vs packaged builds.
- [x] Content-Security-Policy for packaged builds (dev keeps Vite's inline React-refresh preamble working).
- Exit: editing the scene JSON by hand updates Studio within a second. Zebra stripes run continuously across a C2 pair and visibly kink across a G1-only pair. *Visual check by the user.*

### Phase 1 — Easy cases, solved deterministically (L0–L2)

#### M4 — Constrained solver
- [x] Unknowns = free CVs; energy = w_f·(E2 | E3) + w_m·‖x − x_prev‖² (minimal change, so edits stay local) [+ w_t·fit in Phase 3].
- [x] Linear equality constraints: fixed CVs, shared G0 rows, G1/G2 edge pairs (fixed α, γ), Y=0 G1 (G2 follows, see §4), pinned CVs.
- [x] KKT solve; report constraint residuals and conditioning; reject inconsistent constraint sets with a clear message.
- [ ] Optional outer loop over α, γ (Gauss–Newton on the energy). *Deferred until a case needs it: α = 1, γ = 0 (parametric C2) is right for uniformly parameterised networks, which is everything in L0–L2.*
- Tests: constraints hold to < 1e-10; with zero fairness weight and full constraints the result matches a hand-built C2 pair; changing w_m changes how far untouched CVs move.
- Exit: a 3×3 network solves in < 50 ms in release.

#### M5 — Scene document and recipes
- [x] Scene JSON (format v1): units, budgets, curves (through points or control points), patches with recipes, edge/symmetry links, surface pins, solve settings. Targets wait for M11; undo history is a class here and reaches the MCP session in M7.
- [x] Recipe `from_boundaries`: degree-matched boundary curves → discrete Coons interior → solve.
- [x] Recipe `blend`: between edges of two fixed surfaces with G1/G2 on each side.
- [x] Networks: `from_boundaries` patches over shared curves plus G2 edge links, solved jointly; `add_network` builds one from a curve grid. (A separate `network` recipe would only duplicate that.)
- [x] Snapshot undo; JSON round trip leaves a scene unchanged.
- Exit: every L0–L2 case is expressible as a scene file with no CVs written by hand. *Met: `cases/*.ada.json`.*

#### M6 — Case ladder, bench and independent check
- [x] Cases: `L0-roof-centre`, `L0s-roof-centre-sym`, `L1-g2-blend`, `L2-hood-2x2-sym`, `L2-panel-3x3`, each with a budget (and a `description` saying why).
- [x] `ada bench cases/` → scorecard (markdown + JSON); runs in CI, scorecard in the job summary.
- [x] IGES 128 export: `ada export` (with `--solve` for authored cases), the `export_iges` MCP tool, Studio's Export IGES button; CI uploads every case as the `case-iges` artifact.
- [x] Independent check: OpenCASCADE reads the exported IGES and measures every joint again (`scripts/occt_check.py`, run in CI); it agrees with `ada report` on every check of every case. Results in `docs/occt-verification.md`. *Replaces the NX check while there is no NX access (the user's call, 2026-10-26).*
- [ ] Optional: the same check in NX (or Rhino), recorded in `docs/nx-verification.md`, for a Class-A tool's view of the surfaces.
- Exit: all cases pass their budgets in CI, and an independent tool's continuity analysis agrees with our report within tolerance. *Met with OpenCASCADE; NX when available.*

### Phase 2 — The vibe loop

#### M7 — Agent toolset
- [x] Tools: `scene_new`, `curve_from_points`, `patch_from_boundaries`, `patch_blend`, `network_from_curves`, `edge_constrain`, `symmetry_constrain`, `patch_set_degree`, `fairness_set`, `get_selection`, `undo`, `export_iges`. (`solve` already exists, from M5.) Also `redo`, which the snapshot history gives for free.
- [x] Structured errors written for an agent to read (what failed, which constraint, what to try): `{code, message, try, details}`, e.g. `constraints_contradict` with every constraint taking part and its residual.
- [x] No tool sets raw CV coordinates (design rule, section 1). UI-only tools stay behind `--ui-tools`.
- [x] `claude mcp add ada -- ada-mcp --scene <file>` documented in README, with the tool list.
- Exit: the L2 hood case can be built from scratch through tool calls alone. *Met: a test builds it with `scene_new`, one `curve_from_points`, `network_from_curves` and two `symmetry_constrain` calls; the file equals the authored case and its surfaces match solving that case directly (< 1e-6 mm).*

#### M8 — Craft guide and selection bridge (route A, Claude Code)
- [x] Craft guide as a Claude Code skill, `.claude/skills/class-a-surfacing/SKILL.md` (not `agent/`: Claude Code finds project skills only there, and it loads whenever Claude Code runs in the repo). Class-A workflow (inspect → change → solve → report → explain), ADA's conventions with a worked network layout, every quality check, what to try per failure and error code, degree versus split, fairest versus least change, and when to stop and ask.
- [x] Selection bridge end to end: pick in Studio, then the agent refers to "this edge". *Seen working in the user's first session: "make this edge G1" → `get_selection` → `hood_0_1.VMax` → `edge_constrain`.*
- Exit: a natural-language session in Claude Code, watched live in Studio, builds a symmetric G2 roof, and the agent explains the final report correctly. *Session judged by the user. Met 2026-10-26: "works well".*

#### M9 — Studio chat (route B, in-app agent)
*Deferred (the user's call, 2026-10-27): the in-app chat needs Anthropic API billing, which the user does not want for now. Route A, Claude Code next to a live Studio, does the same job and works. M10 runs on route A meanwhile.*
- [x] Choose the agent host by checking the current Anthropic docs at the time: Claude Agent SDK (TypeScript) versus the Messages API tool runner with an MCP client. Record the choice as D12. *Client SDK; see D12.*
- [ ] Host the agent in Studio's main process, connected to the same sidecar; hide UI-only tools from it; load the craft guide (`.claude/skills/class-a-surfacing/SKILL.md`) as its instructions.
- [ ] Chat panel: streaming replies, collapsed tool-call rows, rendered report summaries, the current selection attached to the message as a chip.
- [ ] API key stored with Electron `safeStorage`; never in the repo, the scene file or logs.
- [ ] Windows installer via the Squirrel maker, so others can try it (approve `electron-winstaller`'s install script under npm 11's `allowScripts` first).
- Exit: the M8 roof session works entirely inside Studio.

#### M10 — Intent eval
- [x] 10–20 scripted intent tasks with success defined by the quality report (not by judging the transcript). *Twelve, in `agent/eval/tasks.json`, judged under each task's own budgets.*
- [x] Runner on route A (`claude -p` with the ADA MCP server) first; route B once M9 lands. Save transcripts and scorecards. *`agent/eval/run_eval.py`.*
- Exit: baseline success rate recorded; regressions visible from one run to the next. *Met: 12/12 on 2026-10-27 (`docs/intent-eval.md`, `agent/eval/baseline.json`); each scorecard lists the tasks that regressed or were fixed since the previous run.*

### Phase 3 — Fitting to targets (L3)

#### M11 — Target fitting
- [x] Read OBJ/STL/PLY targets; sample points; closest-point parameter correction (n2s projection). *`io/target`, `fit/fit`; n2s `projection` copied with a warm-start option.*
- [x] Fit term in the solver; deviation colour map in Studio and in the report. *Report: points, max/RMS/p95 against the budgets `fit_max_mm`, `fit_rms_mm`, `fit_p95_mm`. Studio: the target's points coloured by signed deviation, fixed range (the `fit_max_mm` budget by default), a "Fit to target" report section.*
- [x] CV economy: start at a low degree and elevate (or split) until deviation is within budget. *Elevation only; splitting needs a layout change (R3). `ada solve --economy`, `fit_economically`.*
- Exit: synthetic targets (known Bézier + noise) are recovered within noise level at the original degree. *Met: a degree-4 panel on cubic boundaries, sampled with 0.05 mm noise, comes back at degree 4 (economy starts at 3 and stops after one step), with RMS within 1.2x the noise and the surface within half the noise of the truth (`tests/fit/test_fit_scene.cpp`).*

#### M12 — Hood from real data
- [ ] Source the target: the Cranfield DrivAer CAD packs (CC BY 4.0) or the TUM DrivAer CAD (on request; check its terms). Synthetic fallback either way.
- [ ] Case `L3-hood-from-target`, runnable by the agent: "rebuild this hood, G2 at Y=0, 0.5 mm".
- Exit: the case passes in the bench and exported surfaces pass the NX check.

### Phase 4 — Learning (outline; to be detailed after Phase 2)

- **R1 Learned critic.** Inject synthetic defects (bumps, CV zig-zags, broken
  continuity, flat spots, curvature reversals) into clean surfaces and train a
  model that predicts a defect heatmap from curvature and highlight fields or
  rendered zebra images. Compare against the deterministic report; validate
  against a practitioner's ratings.
- **R2 Vision in the loop.** A `render_view` tool returns Studio viewport
  captures to the agent. Measure on the M10 eval whether seeing zebra improves
  results.
- **R3 Layout proposal.** From a target mesh, propose feature lines and a quad
  patch layout, so L3 works without a hand-made curve network.
- **R4 SubD → Class-A.** Turn a concept SubD into a sparse Bézier network: the
  reverse of nurbs2subd.

### Out of scope until L0–L3 are solid
Trimmed surfaces, non-quad corners, set-back fillets, flanges and hems, an NX
plug-in beyond IGES/STEP plus an NX Open journal, the WebAssembly build (D11),
auto-update and code signing, cloud, multi-user.

## 7. Open questions

- **NX access**: which NX version, and is it available for the M6/M12
  verification step? M6 was closed with OpenCASCADE instead; NX would still
  add a Class-A tool's view (its zebra, its tolerances).
- **Practitioner contact**: can a working Class-A modeller review the budgets
  in section 4 and judge results? This is the most valuable outside input.
- **Licence** (D2): the repo is public; pick a licence or stay all-rights-reserved.

## Progress

- 2026-10-23 — Repo created (private), plan drafted. Next: confirm D2–D9, then M0.
- 2026-10-23 — D6 confirmed: ADA Studio is an Electron app (React + three.js)
  with the kernel as an MCP sidecar, replacing the Polyscope/Vulkan viewer plan.
  The minimal MCP transport moved into M3, Studio chat added as M9, milestones
  renumbered to M0–M12, D10 (Studio tooling) and D11 (WASM, deferred) added.
- 2026-10-23 — All decisions confirmed. D2: repo made public (licence open).
  D4 changed: copy individual nurbs2subd files on demand instead of a vendored
  subset. Next: M0.
- 2026-10-23 — M0 kernel half done: CMake/presets/modules ported from
  nurbs2subd with `ADA_` prefixes, `ada_core` + `ada` + `ada-mcp` + 2 tests,
  green locally on MinGW GCC 14.2. Studio half blocked: the local Node is
  v20.11 (EOL), and current Electron Forge tooling needs at least 20.12.
- 2026-10-23 — Local Node switched to 24.22.1 LTS (user PATH). M0 Studio half
  done: Forge 8 + Vite 8 + TS 6 (strict) + React 19 + three.js 0.186 +
  Vitest 5, Electron 45. Viewport shows a Z-up mm ground grid with on-demand
  rendering; `framingDistance` has 4 tests. Deviations from the template:
  Linux deb/rpm makers and auto-unpack-natives dropped; `@types/node@24`
  added (Electron 45 no longer pulls it in); oxfmt bumped to latest because
  the template's 0.41 pulled a critically vulnerable tinypool.
- 2026-10-23 — **M0 exit met**: CI green on all 7 jobs (Linux GCC/Clang/ASan,
  Windows MSVC, clang-format, Studio on Windows + Linux).
- 2026-10-23 — Studio checked by the user: it starts and shows the viewport.
- 2026-10-23 — M1 done. `BezierCurve`, `BezierPatch` (+ `PatchDerivatives`
  table to order 3, `Edge`, `edge_row`/`boundary`), `to_nurbs`/`to_bezier`.
  Polygon operations live once in `src/geom/polygon_ops`, and patches reach u
  through a transpose. Copied from n2s at `dea5d2e`: tolerances, knot vector,
  basis, surface, binomial, plus the knot-vector and basis tests; upstream's
  surface tests are not copied (they need `curve.hpp`) and are replaced by a
  cross-library check against the Bézier code. Oracles: closed-form Bernstein
  sums, exact power-basis derivatives, finite differences. 36 tests, and
  mutation checks confirmed the suite fails when the hodograph factor or the
  split is broken. Evaluation allocates scratch vectors per call; revisit if
  tessellation (M3) or the solver (M4) shows it in a profile.
- 2026-10-23 — M2 done. Analysis: curvature summaries, edge continuity
  (closest-point correspondence; G1 includes a fold check; G2 = cross-boundary
  normal curvature, which is all that can differ once G1 holds), symmetry,
  highlight field + marching squares refined on the exact field + geodesic
  curvature inflections, isophotes, CV-hull bending (normal and sideways
  components counted separately; the first version signed turns by the
  normal, which is noise for rows along principal directions, and the clean
  cylinder test caught it). Budgets with validated keys; quality report as
  JSON and text; `ada report` (exit 0 pass / 1 fail / 2 error).
  **Deviations:** (1) mirror G2 follows from mirror G1, so §4 is corrected
  and symmetry links check G0 + G1 only; (2) the scene format v0 (solved
  CVs + edge/symmetry links + budgets + light) was needed for `ada report`
  and is pulled forward from M5, which extends it with recipes; (3) the
  "sphere has curvature 1/r" test uses exact analytic sphere derivatives
  plus exact Bézier paraboloids, since a polynomial patch cannot be a sphere.
  Copied n2s `differential` (curvature formulas). Example scenes in
  `cases/examples/` come from a hidden generator test, and a normal test fails
  if they drift. 75 tests; 11 deliberate breakages of the measurements were
  all caught. `ada report` on the 2-patch example takes ~1.2 s in Debug,
  mostly highlight extraction.
- 2026-10-23 — M3a: `ada-mcp` speaks MCP over stdio (revisions 2024-11-05 to
  2025-11-25 negotiated; `structuredContent` from 2025-06-18, JSON text for
  older clients). Tools: `scene_open`, `scene_summary`, `quality_report`;
  with `--ui-tools` also `tessellate`, `cv_hulls`, `boundary_combs`. Tool
  failures come back as `isError` results so an agent can read and correct
  them. The server re-reads the scene file whenever its contents change.
  Server logic is a library (`ada_mcp_server`) tested message by message.
- 2026-10-23 — M3b–d: Studio talks to `ada-mcp --ui-tools` through the
  official MCP TypeScript SDK (pinned 1.32.1; 1.32.2 was hours old). Main
  process: sidecar lifecycle with crash reporting and restart, scene loading
  (serialised), directory watch with debounce for hot reload, selection.json
  next to the scene, last scene remembered. Renderer: one surface shader
  with shaded / zebra / reflection lines / highlight lines (kernel field) /
  mean and Gaussian curvature (fixed range set as a radius, legend shown;
  colormap defined once in TS, GLSL generated); CV hulls, boundary combs,
  edges coloured by report status; report panel; click picking (edge within
  5 % of a side). `--scene=`, `--view=` and `--screenshot=` let Studio open a
  scene in a mode, capture itself and quit, which is how the shaders were
  checked before handing over. New example `hood-g1-only` (rear row 2 raised
  15 mm): G1 and symmetry pass, G2 fails at 144 %, and the zebra kink is
  plain to see, unlike `hood-g2-broken` (one CV, 4 mm), which is subtle at
  full-part zoom. Default zebra density raised to 64 stripes (24 was too
  sparse for near-flat panels). Packaged app ships `ada-mcp` as an extra
  resource and has a build-only CSP; verified by running the packaged exe.
  Exit criterion left to the user: hand-edit a scene and watch Studio update;
  click to pick.
- 2026-10-23 — M3 exit: the user ran Studio and reported "the app looks good".
  CI caught one Clang-only `-Wdouble-promotion` error in the MCP test (float
  mesh values passed as double); fixed by widening explicitly. A local Clang 19
  build of every translation unit is now warning-free too.
- 2026-10-23 — M4 done. `ada::solve`: the constraints act on x, y, z
  separately (true for everything L0–L3 needs; shear terms and tilted
  symmetry planes are L4+), so it solves three small exact problems. Per axis:
  constraint rows normalised, rank-revealing QR of Cᵀ (redundant rows from
  shared corners are fine), minimum-norm particular solution plus null-space
  basis, LDLT on the reduced energy. Contradictory constraints are rejected
  with the labels that conflict most. Energy = fairness (E2 or E3, exact
  Bernstein-Gram quadratic forms, normalised by mean diagonal) + move weight
  × |x − x_current|² + a 1e-9 tie-break. Fairness 0 + move 1 is the
  least-change projection onto the constraints ("make this edge G2"); on
  hood-g2-broken it undoes exactly the 4 mm move. Builders: `add_fixed`,
  `add_fixed_row`, `add_edge_join` (G0/G1/G2 rows with α, γ; orientation from
  geometry), `add_symmetry` (Y=0 G1). The KKT system of the plan became this
  null-space formulation, which handles redundancy without special cases.
  3×3 bicubic network, 12 internal G2 joins, scrambled interior: 6.5 ms in
  release (target 50 ms), every joint measured G2. Studio now runs the most
  recently built kernel instead of preferring release (a stale release build
  had become a trap once one existed).
- 2026-10-24 — M5 done. Scene format v1 (v0 still loads, as fixed patches):
  curves through points (chord-length interpolation, least squares beyond
  degree + 1 points, ends always exact), `from_boundaries` and `blend`
  recipes, surface pins, solve settings, a `solved` flag (a recipe-only patch
  carries its starting guess and fails the report as `unsolved`), canonical
  writing so authored files round-trip unchanged, `History` for undo/redo.
  `solve_scene` runs from_boundaries patches jointly, then blends in
  dependency order with sources held; blends choose α per side so they cross
  the gap at an even pace. **Design finding:** curves fitted one by one are
  never smooth through the nodes of a network, and G2 across the joints
  requires that, so holding every curve control point made every network and
  every symmetric patch contradictory. The solver gained soft targets; curve
  control points are hard except where a G1/G2 join or symmetry acts on them,
  and the solve reports how far each curve gave way (0.1 mm for the symmetric
  roof, ~0.6 mm at the hood-network nodes). Five cases authored in `cases/`
  with points only; all solve in ≤ 7 ms and pass the default budgets. Two
  analysis fixes the cases exposed: the CV-hull zig-zag test now ignores
  bending components below 5 % of the row's largest bend (sideways leak from
  a strong S-bend had registered as zig-zags on the L1 blend), and the default
  highlight light faces the scene's dominant direction (the vertical door had
  normals parallel to an overhead light plane, and the unbounded highlight
  range hung the report; levels are now also capped at 200). Pulled forward
  from M7: the `solve` MCP tool (writes the scene back) and Studio's Solve
  button with an "unsolved" banner. Studio's `--screenshot` now waits for the
  renderer to report a drawn scene instead of a fixed delay (a Vite reload in
  dev had produced an empty capture).
- 2026-10-26 — M6 code done; the NX check is the user's. Each case now
  carries per-case budgets and a `description` (new scene field) saying why:
  panels are degree 3 with no curvature reversal; L0 roofs allow no highlight
  inflection; the L1 blend allows its S (reversal, degree 5, two inflections);
  the L2 networks cap inflections at the observed value as a regression guard,
  not a Class-A rule. `ada bench <dir>` solves every case in memory and writes
  a Markdown/JSON scorecard (exit 0 only if all pass); CI runs it and posts
  the scorecard to the job summary. IGES export writes one entity 128 per
  patch in mm (hand-written writer, records checked by a test-only reader
  that reads every patch back exactly). Found on the way: on a symmetric part
  the level-0 highlight line lies on a grid line of the sampled field, where
  rounding noise decided marching-squares classification and zig-zagged the
  line (the full-width roof reported 34 inflections); levels now sit 1e-6 of
  the spacing off k·spacing (0 inflections). docs/nx-verification.md is the
  procedure and the results table for the NX check; the decisive pair is
  hood (G2) vs hood-g1-only (G1, G2 off by 144 %).
- 2026-10-26 — M6 done, with OpenCASCADE instead of NX (no NX access).
  `scripts/occt_check.py` (Python; `cadquery-ocp` 7.9.3.1.1, a validation-only
  dependency the user approved, in `.venv`) solves, reports and exports each
  case, then reads the IGES with OCCT's own reader, finds the joints and Y = 0
  edges from the geometry alone, and measures G0/G1/G2 with OCCT's evaluator
  and principal curvatures. It also measures G2 over all tangent directions,
  mirror G2, and OCCT's LocalAnalysis verdict as a second opinion. It agrees
  with `ada report` on every check of every case, including the decisive pair
  (hood 1.1e-14, hood-g1-only 1.443, hood-g2-broken 0.171 in both tools). It
  also catches a G0 mutant, a G1 mutant and an unlinked shared boundary. Mirror
  G2 measures below 1e-15 on every Y = 0 edge, which confirms §4. CI runs it on
  windows-msvc. Deviation: the M6 exit named NX; the NX check is now
  optional. Found: L1-g2-blend's `inner` sheet ends on Y = 0 without a
  symmetry link (0.044° normal tilt there). That is not a disagreement; it is
  open with the user.
- 2026-10-26 — M7 done: the agent toolset. Design choices (defaults, not
  separate decisions): every edit tool applies its edit to a copy, solves,
  and only then writes, so the file always holds a solved, valid scene and an
  edit that cannot be solved changes nothing (no "edit without solving"
  mode). `curve_from_points` takes several curves per call, because moving a
  node shared by four curves passes through states where corners do not
  meet. `patch_set_degree` carries a degree across linked edges (matched
  edges need equal degrees). `get_selection` also finds, geometrically, the
  edges lying along a picked one, so "make this edge G2" works before any
  link exists. New in the core: optional `degree_u`/`degree_v` on
  from_boundaries recipes (at least the curves' degree; curves are
  elevated exactly); a raised degree starts the solve from the stored
  surface elevated exactly, so a least-change solve keeps it; `doc/topology`
  (edge gaps, touching edges, centre-plane distance; `tol::kEdgeContact`,
  1e-3 of the model size); `failed_checks()`; and `SolveError::conflicts()`.
  Found on the way: contradiction reports named only one side. The residual
  of the solution through an independent subset of the equations falls
  entirely on the equations left out (a symmetry constraint against a curve
  0.5 mm off the plane was reported as three boundary rows and no Y = 0).
  Conflicts are now measured with the least-squares residual, which spreads
  over every constraint involved, and every label above 10 % of the worst
  is named. MinGW Debug builds needed `-Wa,-mbig-obj` (MSVC already had
  `/bigobj`). `scene_summary` now lists curves and recipes in full.
- 2026-10-26 — M8 craft guide and selection bridge; the exit session is the
  user's. The user's first agent session (Claude Code in VS Code, ada
  registered with the bundled claude.exe) built a symmetric 2 x 2 hood in six
  tool calls, then changed and undid a joint picked in Studio. Read from its
  transcript, and fixed: the agent sees each tool's structured result, not
  its text, and the undo result did not say what it took back (History
  entries now carry the edit's description); `scene_new` refused a missing
  folder (it now creates it); Studio's `--view=highlights` silently showed
  zebra, because the toolbar says Highlights and the mode is highlight (ids
  and labels are now accepted, anything else exits with the list); and the
  agent read a case file to learn the network layout and had no guidance on
  curves giving way at nodes (both are now in the guide, which reports the
  cases' own values for calibration: 0.6 mm hood, 1.5 mm 3 x 3 panel).
- 2026-10-27 — D12 decided (client SDK); M9 deferred, because the user does not
  want API billing. M10 done on route A. `agent/eval/run_eval.py` runs each of
  12 tasks as `claude -p` in the repo: only the ada MCP server, no
  shell or edit tools, project settings only, and one appended sentence
  saying nobody will answer questions. It judges the scene left behind
  under the task's budgets (`agent/eval/tasks.json`; starting scenes from
  `make_setups.py`, solved before each session). Baseline: 12/12, Opus 5.5,
  8.6 min. The first judgement failed fix-wavy on a check bug (it measured the
  control net's box: the wiggly degree-5 net stood 50 mm above its surface,
  the refitted bicubic's 26 mm); the
  check now samples the surface, and `--rejudge` re-applies changed checks
  to a finished run. From the transcripts: a gentle crown fitted as a cubic
  through points of a parabola picks up a highlight inflection (chord-length
  parameters); degree 2 through three points cures it. That is now in the
  craft guide. 12/12 also means the tasks are too easy; harder ones are the
  eval's next step.
- 2026-10-27 — M11 done: target fitting. Scenes name targets (OBJ, STL, PLY;
  sampled at the centroids of an even triangle subdivision; cached by path
  and time) and a fit; from_boundaries patches then follow the target
  through a fit term in the solver (normalised like move), with
  point-distance parameter correction (n2s projection, warm-started). Design
  findings: (1) a fit fixes the shape, not the parameterisation, so recovery
  is measured geometrically (equal-(u, v) comparison showed 0.11 mm between
  coincident surfaces); (2) point-distance correction converges linearly, so
  it stops on footpoint motion in mm (points on a shared edge may switch
  patches without moving) or when RMS improves by less than 0.1 %. Tangent-
  or squared-distance minimisation would converge faster but couples the
  solver's axes. (3) Assignment skips patches whose control-net box is
  farther than the best footpoint: the example fit went from 171 s to 21 s
  in Debug, 2 s in Release. CV economy raises the worst patch (by RMS) and
  everything tied to it until the fit budgets hold; the degree logic moved
  into the core (`set_degree`, `DegreeError`) and the MCP tool now uses it.
  Studio's deviation map orients the sign along the axis the surface mostly
  faces (patch normals may point either way; the hood's point down), so red
  is above a hood or outboard of a right door. Example:
  `cases/examples/hood-fit` (the L2 hood with a 3 mm bump in its scan):
  degree 3 misses by 1.7 mm; economy reaches 0.47 mm max / 0.09 mm RMS at
  up to 6x6, and the report then shows the price (highlight inflections,
  zig-zags): following a bumped scan is not Class-A. Blends are not fitted
  yet.
