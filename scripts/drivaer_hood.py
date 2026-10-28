#!/usr/bin/env python3
"""Fetches the L3 target from real data: the DrivAer hood.

Source: DrivAerML (Ashton et al. 2024, arXiv:2408.11969), run 1, the part
"BodyHood" of drivaer_1.stl, licensed CC BY-SA 4.0. The script downloads the
STL into results/drivaerml/ (gitignored, 142 MB) unless it is there, and writes

    cases/targets/drivaer-hood.ply      the hood's right half (y >= 0), in mm,
                                        as a mesh (CC BY-SA 4.0, see README.md
                                        there)

The case cases/L3-hood-from-target.ada.json is built from it the way an agent
would, through the MCP tools (target_add, target_info, network_from_target,
symmetry_constrain, budgets_set, fit_set):

    ada_tests "write the L3 case through tool calls"

Python standard library only.

    python scripts/drivaer_hood.py
"""

from __future__ import annotations

import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE_URL = "https://huggingface.co/datasets/neashton/drivaerml/resolve/main/run_1/drivaer_1.stl"
SOURCE = REPO / "results" / "drivaerml" / "drivaer_1.stl"
TARGET = REPO / "cases" / "targets" / "drivaer-hood.ply"
PART = "BodyHood"
MM = 1000.0  # the STL is in metres


def read_part() -> tuple[list[tuple[float, float, float]], list[tuple[int, int, int]]]:
    """The part's vertices (merged, in mm) and triangles."""
    if not SOURCE.is_file():
        SOURCE.parent.mkdir(parents=True, exist_ok=True)
        print(f"downloading {SOURCE_URL} (142 MB)")
        urllib.request.urlretrieve(SOURCE_URL, SOURCE)
    index: dict[tuple[str, str, str], int] = {}
    vertices: list[tuple[float, float, float]] = []
    triangles: list[tuple[int, int, int]] = []
    inside = False
    corners: list[int] = []
    with SOURCE.open(encoding="ascii", errors="replace") as f:
        for line in f:
            s = line.strip()
            if s.startswith("solid"):
                inside = s[5:].strip() == PART
            elif not inside:
                continue
            elif s.startswith("vertex"):
                key = tuple(s.split()[1:4])
                if key not in index:
                    index[key] = len(vertices)
                    vertices.append(tuple(MM * float(t) for t in key))
                corners.append(index[key])
            elif s.startswith("endfacet"):
                triangles.append(tuple(corners))
                corners = []
            elif s.startswith("endsolid"):
                inside = False
    return vertices, triangles


def right_half(vertices, triangles):
    """Triangles with their centroid at y >= 0, renumbered."""
    kept = [t for t in triangles if sum(vertices[k][1] for k in t) >= 0.0]
    used = sorted({k for t in kept for k in t})
    renumber = {k: n for n, k in enumerate(used)}
    return [vertices[k] for k in used], [tuple(renumber[k] for k in t) for t in kept]


def write_ply(vertices, triangles) -> None:
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    with TARGET.open("w", encoding="ascii", newline="\n") as f:
        f.write("ply\nformat ascii 1.0\n")
        f.write("comment DrivAer hood, right half, mm. From DrivAerML run 1 (BodyHood), CC BY-SA 4.0.\n")
        f.write(f"element vertex {len(vertices)}\nproperty double x\nproperty double y\nproperty double z\n")
        f.write(f"element face {len(triangles)}\nproperty list uchar int vertex_indices\nend_header\n")
        for x, y, z in vertices:
            f.write(f"{x:.4f} {y:.4f} {z:.4f}\n")
        for a, b, c in triangles:
            f.write(f"3 {a} {b} {c}\n")


def main() -> None:
    vertices, triangles = read_part()
    print(f"{PART}: {len(vertices)} vertices, {len(triangles)} triangles")
    half_v, half_t = right_half(vertices, triangles)
    write_ply(half_v, half_t)
    print(f"wrote {TARGET.relative_to(REPO)}: {len(half_v)} vertices, {len(half_t)} triangles")
    print('the L3 case is built from it through the MCP tools: ada_tests "write the L3 case through tool calls"')


if __name__ == "__main__":
    main()
