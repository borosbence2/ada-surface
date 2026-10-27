#!/usr/bin/env python3
"""Intent eval (M10): each task in tasks.json runs as a headless Claude Code
session (`claude -p`) whose only tools are the ada MCP server's, started in
this repository so the class-a-surfacing skill applies. The result is judged
by the scene the session leaves behind and its quality report, under the
task's own budgets, never by reading the transcript.

    python agent/eval/run_eval.py                    # every task
    python agent/eval/run_eval.py --tasks pick-g1,undo

Each run writes results/eval/<run>/: per task the scene, transcript
(stream-json), answer and judgement, plus scorecard.md and scorecard.json.
The scorecard compares itself with the previous run, so regressions show.
Python standard library only. docs/intent-eval.md describes the tasks.
"""

from __future__ import annotations

import argparse
import datetime
import json
import math
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT = REPO / "results" / "eval"
LEVEL = {"G0": 0, "G1": 1, "G2": 2}

# Appended to Claude Code's system prompt for every task. Without it, an agent
# following the craft guide may stop to ask, and nobody answers in a batch run.
NOTE = (
    "This is an automated evaluation run. ADA Studio is not open and nobody will answer "
    "questions, so do not wait for a reply: where something is unclear, make the reasonable "
    "choice, do the work, and say what you assumed."
)
# Hand edits would bypass the kernel (CLAUDE.md: the agent never writes control
# points), so the session gets the ada tools and read-only file access only.
DISALLOWED = ["Bash", "PowerShell", "Write", "Edit", "NotebookEdit"]


def newest(paths: list[Path]) -> Path | None:
    existing = [p for p in paths if p.is_file()]
    return max(existing, key=lambda p: p.stat().st_mtime) if existing else None


def find_claude() -> Path | None:
    on_path = shutil.which("claude")
    if on_path:
        return Path(on_path)
    # Only the VS Code extension installed: it ships the CLI.
    extensions = Path.home() / ".vscode" / "extensions"
    return newest(list(extensions.glob("anthropic.claude-code-*/resources/native-binary/claude*")))


def find_build(name: str) -> Path | None:
    exe = name + (".exe" if os.name == "nt" else "")
    return newest(list((REPO / "build").glob(f"*/bin/{exe}")))


def version(binary: Path) -> str:
    result = subprocess.run([str(binary), "--version"], capture_output=True, text=True)
    return result.stdout.strip()


# --- Running a task --------------------------------------------------------------


def prepare(task: dict, tdir: Path, ada: Path, ada_mcp: Path) -> tuple[Path, str]:
    """The task's folder, starting scene, selection and MCP config; returns
    the scene path and the prompt."""
    tdir.mkdir(parents=True)
    scene = tdir / "scene.ada.json"
    server_args: list[str] = []
    if "setup" in task:
        # Solved, like a scene someone is working on: undoing the session's
        # first edit should land on a solved scene, not an authored one.
        subprocess.run([str(ada), "solve", str(HERE / "setups" / task["setup"]), "-o", str(scene)],
                       capture_output=True)  # fmt: skip
        server_args = ["--scene", str(scene)]
    if "selection" in task:
        selection = {
            "scene": str(scene),
            **task["selection"],
            "uv": [0.5, 0.5],
            "point": [0.0, 0.0, 0.0],
            "time": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        }
        (tdir / "selection.json").write_text(json.dumps(selection, indent=2), encoding="utf-8")
    config = {"mcpServers": {"ada": {"type": "stdio", "command": str(ada_mcp), "args": server_args}}}
    (tdir / "mcp.json").write_text(json.dumps(config, indent=2), encoding="utf-8")
    prompt = task["prompt"].format(scene=scene.as_posix(), dir=tdir.as_posix())
    if "setup" in task:
        prompt = f"(The scene {scene.as_posix()} is open in ADA.) {prompt}"
    return scene, prompt


def run_session(claude: Path, prompt: str, tdir: Path, model: str | None, timeout: float) -> dict:
    command = [
        str(claude), "-p", prompt,
        "--output-format", "stream-json", "--verbose",
        "--mcp-config", str(tdir / "mcp.json"), "--strict-mcp-config",
        "--allowedTools", "mcp__ada",
        "--disallowedTools", *DISALLOWED,
        "--setting-sources", "project",
        "--no-session-persistence",
        "--append-system-prompt", NOTE,
    ]  # fmt: skip
    if model:
        command += ["--model", model]
    start = time.monotonic()
    timed_out = False
    with open(tdir / "transcript.jsonl", "w", encoding="utf-8") as transcript:
        try:
            result = subprocess.run(
                command,
                cwd=REPO,
                stdin=subprocess.DEVNULL,
                stdout=transcript,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                timeout=timeout,
            )
            exit_code, stderr = result.returncode, result.stderr
        except subprocess.TimeoutExpired:
            exit_code, stderr, timed_out = None, "", True
    if stderr.strip():
        (tdir / "stderr.txt").write_text(stderr, encoding="utf-8")
    return {"exit": exit_code, "timed_out": timed_out, "seconds": time.monotonic() - start}


def digest(transcript: Path) -> dict:
    """Model, tool calls by name, tool results flagged as errors, and the final result."""
    info = {"model": None, "ada_connected": None, "calls": {}, "tool_errors": 0, "result": {}}
    for line in transcript.open(encoding="utf-8"):
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        kind = event.get("type")
        if kind == "system" and event.get("subtype") == "init":
            info["model"] = event.get("model")
            servers = {s.get("name"): s.get("status") for s in event.get("mcp_servers", [])}
            info["ada_connected"] = servers.get("ada") == "connected"
        elif kind == "assistant":
            for block in event.get("message", {}).get("content", []):
                if block.get("type") == "tool_use":
                    name = block.get("name", "?").removeprefix("mcp__ada__")
                    info["calls"][name] = info["calls"].get(name, 0) + 1
        elif kind == "user":
            content = event.get("message", {}).get("content", [])
            if isinstance(content, list):
                info["tool_errors"] += sum(
                    1 for b in content if b.get("type") == "tool_result" and b.get("is_error")
                )
        elif kind == "result":
            info["result"] = event
    return info


# --- Judging -----------------------------------------------------------------------


def report_for(scene: dict, budgets: dict, tdir: Path, ada: Path, name: str) -> dict:
    judged = dict(scene)
    judged.pop("budgets", None)
    if budgets:
        judged["budgets"] = budgets
    path = tdir / f"{name}.ada.json"
    path.write_text(json.dumps(judged), encoding="utf-8")
    report_path = tdir / f"{name}.report.json"
    subprocess.run([str(ada), "report", str(path), "--json", str(report_path)], capture_output=True)
    return json.loads(report_path.read_text(encoding="utf-8"))


def surface_points(patch: dict, samples: int = 17) -> list[list[float]]:
    """Points on a patch over a uniform (u, v) grid. Bounds must come from the
    surface, not the control net: a wiggly net reaches well past its surface,
    so its box shrinks when the wiggle is fixed even though the shape stays."""
    du, dv = patch["degree"]
    cps = patch["control_points"]  # i * (dv + 1) + j, with i the u index

    def bernstein(n: int, t: float) -> list[float]:
        return [math.comb(n, i) * t**i * (1 - t) ** (n - i) for i in range(n + 1)]

    points = []
    for a in range(samples):
        bu = bernstein(du, a / (samples - 1))
        for b in range(samples):
            bv = bernstein(dv, b / (samples - 1))
            points.append([sum(bu[i] * bv[j] * cps[i * (dv + 1) + j][k]
                               for i in range(du + 1) for j in range(dv + 1)) for k in range(3)])  # fmt: skip
    return points


def bounds(scene: dict) -> dict | None:
    points = [p for patch in scene.get("patches", []) if "control_points" in patch
              for p in surface_points(patch)]  # fmt: skip
    if not points:
        return None
    return {axis: (min(p[k] for p in points), max(p[k] for p in points)) for k, axis in enumerate("xyz")}


def failed_checks(report: dict) -> list[str]:
    failed = []
    for group, label in (("patches", "id"), ("edges", "a"), ("symmetry", "edge")):
        for item in report.get(group, []):
            for c in item.get("checks", []):
                if not c["pass"]:
                    failed.append(f"{item.get(label)} {c['name']} = {c['value']:.4g} > {c['budget']:.4g}")
    return failed


def judge(task: dict, tdir: Path, ada: Path, case_budgets: dict) -> list[dict]:
    checks = task["checks"]
    results: list[dict] = []

    def add(name: str, ok: bool, detail: str) -> None:
        results.append({"check": name, "pass": bool(ok), "detail": detail})

    if "iges" in checks:
        spec = checks["iges"]
        path = tdir / spec["file"]
        if not path.is_file():
            add("iges", False, f"no file {spec['file']}")
        else:
            # Directory entries come in pairs of D lines; the first of each
            # pair starts with the entity type number.
            d_lines = [line for line in path.read_text(encoding="ascii", errors="replace").splitlines()
                       if len(line) >= 73 and line[72] == "D"]  # fmt: skip
            entities = sum(1 for line in d_lines[0::2] if line[:8].strip() == "128")
            add("iges", entities == spec["entities"], f"{entities} entities 128, wanted {spec['entities']}")
    if set(checks) <= {"iges"}:
        return results

    scene_path = tdir / "scene.ada.json"
    if not scene_path.is_file():
        add("scene", False, "no scene file")
        return results
    scene = json.loads(scene_path.read_text(encoding="utf-8"))
    spec = checks.get("quality", {})
    budgets = case_budgets[spec] if isinstance(spec, str) else spec
    report = report_for(scene, budgets, tdir, ada, "judged")
    if "quality" in checks:
        failed = failed_checks(report)
        add("quality", report.get("pass", False), "; ".join(failed) or "pass")

    if "patches" in checks:
        count = len(scene.get("patches", []))
        add("patches", count == checks["patches"]["count"], f"{count} patches")

    box = bounds(scene)
    for name in ("size", "range", "size_like_setup"):
        if name in checks and box is None:
            add(name, False, "no solved patches")
    if box is not None:
        if "size" in checks:
            sizes = {a: box[a][1] - box[a][0] for a in "xyz"}
            ok = all(lo <= sizes[a] <= hi for a, (lo, hi) in checks["size"].items())
            add("size", ok, ", ".join(f"{a} {sizes[a]:.0f} mm" for a in checks["size"]))
        if "range" in checks:
            values = {f"{a}_min": box[a][0] for a in "xyz"} | {f"{a}_max": box[a][1] for a in "xyz"}
            ok = all(lo <= values[k] <= hi for k, (lo, hi) in checks["range"].items())
            add("range", ok, ", ".join(f"{k} {values[k]:.1f}" for k in checks["range"]))
        if "size_like_setup" in checks:
            setup = tdir / "setup.solved.ada.json"
            subprocess.run([str(ada), "solve", str(HERE / "setups" / task["setup"]), "-o", str(setup)],
                           capture_output=True)  # fmt: skip
            before = bounds(json.loads(setup.read_text(encoding="utf-8")))
            tol = checks["size_like_setup"]["tolerance_mm"]
            worst = max(abs(box[a][k] - before[a][k]) for a in "xyz" for k in (0, 1))
            add("size_like_setup", worst <= tol, f"bounds moved up to {worst:.1f} mm")

    edges = report.get("edges", [])
    if "links" in checks:
        spec = checks["links"]
        levels = [e["continuity"] for e in edges]
        ok = len(edges) == spec["count"]
        if "all" in spec:
            ok = ok and all(c == spec["all"] for c in levels)
        if "count_G2" in spec:
            ok = ok and levels.count("G2") == spec["count_G2"]
        add("links", ok, ", ".join(f"{e['a']} ~ {e['b']} {e['continuity']}" for e in edges) or "none")
    if "link" in checks:
        spec = checks["link"]
        found = [e for e in edges if {e["a"], e["b"]} == set(spec["edges"])]
        got = found[0]["continuity"] if found else "not linked"
        add("link", got == spec["continuity"], f"{' ~ '.join(spec['edges'])}: {got}")
    if "symmetry" in checks:
        spec = checks["symmetry"]
        strong = [s for s in report.get("symmetry", []) if LEVEL[s["continuity"]] >= LEVEL[spec["at_least"]]]
        add("symmetry", len(strong) >= spec["min_count"],
            ", ".join(f"{s['edge']} {s['continuity']}" for s in report.get("symmetry", [])) or "none")  # fmt: skip
    if "blends" in checks:
        spec = checks["blends"]
        blends = [p["recipe"] for p in scene.get("patches", []) if p.get("recipe", {}).get("type") == "blend"]
        ok = len(blends) == spec["count"] and all(b.get("continuity") == spec["continuity"] for b in blends)
        add("blends", ok, ", ".join(b.get("continuity", "?") for b in blends) or "none")
    return results


# --- Scorecard -----------------------------------------------------------------------


def previous_scorecard(current: Path) -> Path | None:
    """The last run's scorecard, or the committed baseline when there is none here."""
    runs = sorted(p for p in OUT.iterdir() if p.is_dir() and p != current and (p / "scorecard.json").is_file())
    if runs:
        return runs[-1] / "scorecard.json"
    baseline = HERE / "baseline.json"
    return baseline if baseline.is_file() else None


def scorecard(run: dict, run_dir: Path) -> str:
    passed = sum(t["pass"] for t in run["tasks"])
    lines = [
        f"### Intent eval {run['run']}: {passed}/{len(run['tasks'])} tasks pass",
        "",
        f"Model `{run['model']}`, Claude Code {run['claude']}, {run['ada']}."
        + (f" Judged again {run['rejudged']} after a check changed." if run.get("rejudged") else ""),
        "",
        "| task | result | failed checks | tool calls | tool errors | turns | minutes | cost (USD) |",
        "|---|---|---|---:|---:|---:|---:|---:|",
    ]
    for t in run["tasks"]:
        failed = "; ".join(f"{c['check']}: {c['detail']}" for c in t["checks"] if not c["pass"])
        lines.append(
            f"| {t['id']} | {'pass' if t['pass'] else '**FAIL**'} | {failed or '-'} | "
            f"{sum(t['calls'].values())} | {t['tool_errors']} | {t.get('turns', '-')} | "
            f"{t['seconds'] / 60:.1f} | {t.get('cost_usd') or 0:.2f} |"
        )
    before = previous_scorecard(run_dir)
    if before is not None:
        previous = json.loads(before.read_text(encoding="utf-8"))
        old = {t["id"]: t["pass"] for t in previous["tasks"]}
        changes = [
            f"- {t['id']}: {'fixed' if t['pass'] else '**regressed**'}"
            for t in run["tasks"]
            if t["id"] in old and old[t["id"]] != t["pass"]
        ]
        lines += ["", f"Against run {previous['run']}: " + ("no changes" if not changes else ""), *changes]
    return "\n".join(lines) + "\n"


def rejudge(run_dir: Path, tasks: dict, ada: Path, case_budgets: dict) -> int:
    """Re-applies the checks to the scenes a run left behind; the sessions are not repeated."""
    run = json.loads((run_dir / "scorecard.json").read_text(encoding="utf-8"))
    for entry in run["tasks"]:
        tdir = run_dir / entry["id"]
        kept = [c for c in entry["checks"] if c["check"] in ("finished", "ada")]
        entry["checks"] = judge(tasks[entry["id"]], tdir, ada, case_budgets) + kept
        entry["pass"] = bool(entry["checks"]) and all(c["pass"] for c in entry["checks"])
        (tdir / "judgement.json").write_text(json.dumps(entry, indent=2), encoding="utf-8")
    run["rejudged"] = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    text = scorecard(run, run_dir)
    (run_dir / "scorecard.json").write_text(json.dumps(run, indent=2), encoding="utf-8")
    (run_dir / "scorecard.md").write_text(text, encoding="utf-8")
    print(text)
    return 0 if all(t["pass"] for t in run["tasks"]) else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--tasks", help="comma-separated task ids (default: all)")
    parser.add_argument("--model", help="model for claude -p (default: Claude Code's)")
    parser.add_argument("--timeout", type=float, default=1200.0, help="seconds per task")
    parser.add_argument("--claude", type=Path, default=find_claude())
    parser.add_argument("--ada", type=Path, default=find_build("ada"))
    parser.add_argument("--ada-mcp", type=Path, default=find_build("ada-mcp"))
    parser.add_argument("--rejudge", type=Path, help="judge a finished run again (after a check changed)")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    for name in ("claude", "ada", "ada_mcp"):
        if getattr(args, name) is None or not getattr(args, name).is_file():
            print(f"cannot find {name}; pass --{name.replace('_', '-')}", file=sys.stderr)
            return 2

    spec = json.loads((HERE / "tasks.json").read_text(encoding="utf-8"))
    tasks = spec["tasks"]
    if args.rejudge:
        return rejudge(args.rejudge.resolve(), {t["id"]: t for t in tasks}, args.ada, spec["case_budgets"])
    if args.tasks:
        wanted = args.tasks.split(",")
        tasks = [t for t in tasks if t["id"] in wanted]
    run_id = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = OUT / run_id
    run = {"run": run_id, "model": args.model, "claude": version(args.claude),
           "ada": version(args.ada), "tasks": []}  # fmt: skip

    for task in tasks:
        tdir = run_dir / task["id"]
        _, prompt = prepare(task, tdir, args.ada, args.ada_mcp)
        print(f"{task['id']}: running", flush=True)
        session = run_session(args.claude, prompt, tdir, args.model, args.timeout)
        info = digest(tdir / "transcript.jsonl")
        result = info["result"]
        (tdir / "answer.md").write_text(str(result.get("result", "")), encoding="utf-8")
        checks = judge(task, tdir, args.ada, spec["case_budgets"])
        if session["timed_out"]:
            checks.append({"check": "finished", "pass": False, "detail": "timed out"})
        if info["ada_connected"] is False:
            checks.append({"check": "ada", "pass": False, "detail": "ada server not connected"})
        run["model"] = run["model"] or info["model"]
        entry = {
            "id": task["id"],
            "pass": bool(checks) and all(c["pass"] for c in checks),
            "checks": checks,
            "calls": info["calls"],
            "tool_errors": info["tool_errors"],
            "turns": result.get("num_turns"),
            "cost_usd": result.get("total_cost_usd"),
            **session,
        }
        run["tasks"].append(entry)
        (tdir / "judgement.json").write_text(json.dumps(entry, indent=2), encoding="utf-8")
        print(f"{task['id']}: {'pass' if entry['pass'] else 'FAIL'} ({session['seconds'] / 60:.1f} min)", flush=True)

    text = scorecard(run, run_dir)
    (run_dir / "scorecard.json").write_text(json.dumps(run, indent=2), encoding="utf-8")
    (run_dir / "scorecard.md").write_text(text, encoding="utf-8")
    print(text)
    return 0 if all(t["pass"] for t in run["tasks"]) else 1


if __name__ == "__main__":
    sys.exit(main())
