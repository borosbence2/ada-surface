# Intent eval (M10)

Does the agent turn plain-language intent into the right surfaces? The eval
gives it thirteen tasks of the kind a designer would ask for and judges each
one by the scene it leaves behind, never by reading the conversation. The
baseline below is the reference that later runs are compared against.

## How a task runs

`agent/eval/run_eval.py` runs every task in
[`agent/eval/tasks.json`](../agent/eval/tasks.json) as its own headless
Claude Code session (`claude -p`):

- **Started in this repository**, so the craft guide
  (`.claude/skills/class-a-surfacing`) and `CLAUDE.md` apply, as they do when
  you work with Claude Code yourself.
- **Only the `ada` MCP server** (`--strict-mcp-config`). File reads are
  allowed. Shell and file-editing tools are off, so the only way to change
  geometry is through the kernel's tools.
- **Project settings only** (`--setting-sources project`). Personal settings
  and permissions do not leak into the run.
- **One sentence appended to the system prompt.** It says that nobody will
  answer questions, so the agent should make reasonable choices and state
  them. Without it, an agent that follows the guide's "stop and ask" would
  wait forever.
- **Editing tasks start from a scene in `agent/eval/setups/`**, solved and
  already open in the kernel. Those setups are derived from `cases/` by
  `make_setups.py`. Tasks that say "this edge" also get a `selection.json`,
  standing in for a pick in ADA Studio.

The runner keeps everything under `results/eval/<run>/<task>/`: the final
scene, the full transcript (stream-json), the agent's answer, and the
judgement. It also writes `scorecard.md` and `scorecard.json`, which compare
the run with the previous one, so a regression shows.

```
python agent/eval/run_eval.py                       # all thirteen tasks, about a quarter of an hour
python agent/eval/run_eval.py --tasks pick-g1,undo  # some of them
python agent/eval/run_eval.py --rejudge results/eval/<run>   # apply changed checks to a finished run
```

The scorecard compares itself with the previous run in `results/eval/`, or,
on a fresh clone, with the committed baseline (`agent/eval/baseline.json`).

It uses the newest `ada` and `ada-mcp` build and the `claude` CLI (on the PATH,
or the copy inside the VS Code extension). Runs count against your Claude
Code usage; the scorecard's cost column is what the same tokens would cost on
the API.

## How a task is judged

Every check reads the final scene and its quality report:

| Check | Passes when |
|---|---|
| `quality` | the report passes under the **task's** budgets (or a named case's), not the budgets in the scene, so the agent cannot pass by relaxing them |
| `patches` | the scene has the expected number of patches |
| `size`, `range` | the surface's bounding box (sampled on the patches, not taken from the control net) has the expected size or limits |
| `size_like_setup` | that bounding box moved less than a tolerance from the setup's |
| `links`, `link` | the joints (explicit and implied by blends) have the expected count (when given) and continuity |
| `symmetry` | enough Y = 0 edges join their mirror image at the required continuity or better |
| `blends` | the expected blend patches exist at the expected continuity |
| `fit` | the scene is fitted to the named target, every target point lies over the patches, and the largest distance is within the given value |
| `iges` | the IGES file exists with one entity 128 per patch |

A task passes when all its checks do.

## The tasks

| Task | Asks for | Exercises |
|---|---|---|
| `build-roof` | a single-patch roof of given size and crown | curves through points, one patch |
| `build-roof-half` | the half roof, mirror-symmetric at Y = 0 | symmetry |
| `build-hood` | a 2 × 2 hood network, symmetric, G2 everywhere | networks, node continuity |
| `build-door` | a 3 × 3 door skin, G2 everywhere | larger networks |
| `build-blend` | two offset panels and a G2 blend between them | blends |
| `pick-g1` | "make this edge G1" with an edge picked | selection, `edge_constrain` |
| `pick-fix-kink` | "the zebra stripes kink across this joint, fix it" | diagnosis from designer words |
| `make-symmetric` | make a half roof symmetric | symmetry on an existing scene |
| `undo` | change a joint, report on it, undo | history |
| `export` | export IGES | export |
| `fix-wavy` | "the reflections wobble along one side, fix it" | finding a bad curve and refitting it |
| `lengthen-hood` | make the hood 100 mm longer | moving shared nodes, several curves at once |
| `rebuild-hood` | "rebuild this hood, G2 at Y = 0, 0.5 mm", with the DrivAer hood's tessellation attached as the target | reading a target's outline, `network_from_target`, budgets, fitting (L3) |

## Results

### Baseline, 2026-10-27: 12 of 12 tasks pass

Claude Opus 5.5 through Claude Code 2.1.311, ADA `0a999d6` (Release). The whole
run took 8.6 minutes and 101 tool calls; at API prices it would have cost
$3.64.

| task | result | tool calls | tool errors | turns | minutes | cost (USD) |
|---|---|---:|---:|---:|---:|---:|
| build-roof | pass | 7 | 0 | 9 | 0.7 | 0.27 |
| build-roof-half | pass | 21 | 0 | 23 | 2.5 | 0.62 |
| build-hood | pass | 8 | 0 | 10 | 0.7 | 0.33 |
| build-door | pass | 6 | 0 | 8 | 1.1 | 0.41 |
| build-blend | pass | 8 | 0 | 10 | 0.4 | 0.25 |
| pick-g1 | pass | 5 | 0 | 7 | 0.3 | 0.18 |
| pick-fix-kink | pass | 6 | 0 | 8 | 0.4 | 0.22 |
| make-symmetric | pass | 8 | 1 | 10 | 0.4 | 0.20 |
| undo | pass | 6 | 0 | 8 | 0.2 | 0.22 |
| export | pass | 6 | 0 | 8 | 0.2 | 0.20 |
| fix-wavy | pass | 8 | 0 | 11 | 0.5 | 0.26 |
| lengthen-hood | pass | 12 | 0 | 14 | 1.2 | 0.48 |

The first judgement failed `fix-wavy`: "bounds moved 25 mm". The agent had
done the right thing. It refitted the rippled side curve at degree 3 through
the five points that carry no ripple, which also matched it to the opposite
side, and it explained the result accurately. But the check measured the
control net's box. The wavy degree-5 net reached 50 mm above its own
surface; a clean bicubic's net reaches 26 mm. The check now samples the surface, and `--rejudge` re-applied it to
the same run: the surface's bounds moved 0.3 mm.

What the transcripts show:

- **`build-roof-half` took 21 calls.** The agent chased a single highlight
  inflection on the nearly flat half roof. It found that a cubic through
  points sampled from a parabola, parametrised by chord length, is not
  exactly that parabola, and cured it by fitting the crown curves at degree
  2 through three points. That tip is now in the craft guide.
- **The agents read the craft guide in every task.** Some also read a case
  file or `MILESTONES.md` for reference (file reads are allowed), and none
  tried to edit a scene by hand.
- **The only tool error** was a `get_selection` call with nothing selected,
  made just in case before a symmetry edit; the agent went on correctly.
- **12 of 12 says the tasks are too easy to tell versions apart.** They
  guard against regressions; they don't yet measure much. Harder tasks are
  the next step for the eval: contradictory constraints, an under-specified
  intent, a fix that needs a higher degree, a layout change.

### `rebuild-hood`, 2026-10-28: pass

Added with M12 and run on its own; its entry joined `baseline.json`. Claude
Opus 5.5 through Claude Code 2.1.312, ADA at the M12 tools (Release): 28 tool
calls, no tool errors, 1.0 minute, $0.78 at API prices.

| task | result | tool calls | tool errors | turns | minutes | cost (USD) |
|---|---|---:|---:|---:|---:|---:|
| rebuild-hood | pass | 28 | 0 | 30 | 1.0 | 0.78 |

The agent read the guide, then `target_info`, and laid a 4 × 4 network on
the four outline corners. It put G1 symmetry on the four centre edges (G1
at a mirror gives G2), set `fit_max_mm` to 0.5 and the fit last (weight 10,
CV economy). The default zig-zag budget (0 rows) then failed. The agent
rebuilt the network with degree-3 outline curves, which halved the zig-zag
rows but doubled the deviation (0.21 to 0.41 mm, still within 0.5). It
stopped there and asked whether the zig-zag budget could rise, instead of
changing it. Asking was right; trading the fit for zig-zags was not, and the
craft guide now says why.

The first judgement failed the new `fit` check on a judge bug: it read the
report's fit numbers from the wrong place. `--rejudge` passed it.
