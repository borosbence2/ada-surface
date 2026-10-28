#!/usr/bin/env python3
"""Builds the L3 case from real data: the DrivAer hood.

Source: DrivAerML (Ashton et al. 2024, arXiv:2408.11969), run 1, the part
"BodyHood" of drivaer_1.stl, licensed CC BY-SA 4.0. The script downloads the
STL into results/drivaerml/ (gitignored, 142 MB) unless it is there, and writes

    cases/targets/drivaer-hood.ply      the hood's right half (y >= 0), in mm,
                                        as a mesh (CC BY-SA 4.0, see README.md
                                        there)
    cases/L3-hood-from-target.ada.json  the case: a 2 x 2 curve network on the
                                        hood's outline and two lines across it,
                                        fitted to the target's vertices

Python standard library only.

    python scripts/drivaer_hood.py
"""

from __future__ import annotations

import json
import math
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE_URL = "https://huggingface.co/datasets/neashton/drivaerml/resolve/main/run_1/drivaer_1.stl"
SOURCE = REPO / "results" / "drivaerml" / "drivaer_1.stl"
TARGET = REPO / "cases" / "targets" / "drivaer-hood.ply"
CASE = REPO / "cases" / "L3-hood-from-target.ada.json"
PART = "BodyHood"
MM = 1000.0  # the STL is in metres
# 4 x 4: at 3 x 3 the fit needs degree 7 in places for 0.5 mm, at 4 x 4
# degree 5 (the edge curves') holds it to 0.26 mm.
GRID = 4  # patches per side


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


def outline(vertices, triangles) -> list[int]:
    """The boundary loop of the whole part, as vertex indices."""
    count: dict[tuple[int, int], int] = {}
    for t in triangles:
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            key = (min(a, b), max(a, b))
            count[key] = count.get(key, 0) + 1
    neighbours: dict[int, list[int]] = {}
    for (a, b), n in count.items():
        if n == 1:
            neighbours.setdefault(a, []).append(b)
            neighbours.setdefault(b, []).append(a)
    start = next(iter(neighbours))
    loop, previous, current = [start], None, start
    while True:
        following = [n for n in neighbours[current] if n != previous]
        nxt = following[0]
        if nxt == start:
            return loop
        loop.append(nxt)
        previous, current = current, nxt


def height(vertices, triangles, x: float, y: float) -> float:
    """z of the mesh above (x, y): every facet faces up, so the hood is a height field."""
    for a, b, c in triangles:
        (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = vertices[a], vertices[b], vertices[c]
        det = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3)
        if det == 0.0:
            continue
        l1 = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / det
        l2 = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / det
        l3 = 1.0 - l1 - l2
        if min(l1, l2, l3) >= -1e-9:
            return l1 * z1 + l2 * z2 + l3 * z3
    raise ValueError(f"({x:.1f}, {y:.1f}) is not over the hood")


def main() -> None:
    vertices, triangles = read_part()
    print(f"{PART}: {len(vertices)} vertices, {len(triangles)} triangles")
    half_v, half_t = right_half(vertices, triangles)
    write_ply(half_v, half_t)
    print(f"wrote {TARGET.relative_to(REPO)}: {len(half_v)} vertices, {len(half_t)} triangles")

    # The outline's right half, from the front centre round to the rear centre.
    # It crosses y = 0 between vertices, so both centre corners are found by
    # interpolating the crossing edges.
    loop = [vertices[k] for k in outline(vertices, triangles)]
    n = len(loop)
    crossings = []
    for i in range(n):
        a, b = loop[i], loop[(i + 1) % n]
        if (a[1] < 0.0) != (b[1] < 0.0):
            t = a[1] / (a[1] - b[1])
            point = tuple(a[k] + t * (b[k] - a[k]) for k in range(3))
            crossings.append((i, point))
    assert len(crossings) == 2, "the outline should cross y = 0 twice"
    (i_front, front_c), (i_rear, rear_c) = sorted(crossings, key=lambda c: c[1][0])
    # Walk from the front crossing in the direction that goes to y > 0.
    step = 1 if loop[(i_front + 1) % n][1] > 0 else -1
    k = (i_front + 1) % n if step == 1 else i_front
    path = [front_c]
    while loop[k][1] >= 0.0:
        path.append(loop[k])
        k = (k + step) % n
    path.append(rear_c)
    # A crossing can fall on (or within rounding of) an outline vertex, which
    # the walk then adds again; a curve cannot pass through the same point
    # twice in a row, and points a micrometre apart are the same point here.
    path = [p for i, p in enumerate(path) if i == 0 or math.dist(p, path[i - 1]) > 1e-3]
    # Corners: front side is the outline point furthest forward-out (min x - y),
    # rear side the one furthest rear-out (max x + y).
    i_fs = min(range(len(path)), key=lambda i: path[i][0] - path[i][1])
    i_rs = max(range(len(path)), key=lambda i: path[i][0] + path[i][1])
    front, side, rear = path[: i_fs + 1], path[i_fs : i_rs + 1], path[i_rs:]
    centre = [front_c] + sorted(
        (v for v in vertices if abs(v[1]) < 1e-9 and front_c[0] < v[0] < rear_c[0]), key=lambda v: v[0]
    ) + [rear_c]
    print(f"corners: front centre {path[0][:2]}, front side {path[i_fs][:2]}, "
          f"rear side {path[i_rs][:2]}, rear centre {path[-1][:2]}")

    def split(points, parts):
        """Indices cutting a polyline into `parts` pieces of equal length (existing vertices)."""
        lengths = [0.0]
        for a, b in zip(points, points[1:]):
            lengths.append(lengths[-1] + math.dist(a, b))
        cuts = [0]
        for q in range(1, parts):
            target = q * lengths[-1] / parts
            cuts.append(min(range(len(points)), key=lambda i: abs(lengths[i] - target)))
        cuts.append(len(points) - 1)
        return cuts

    def thin(points, count):
        """At most `count` points, ends kept, evenly by index."""
        if len(points) <= count:
            return list(points)
        return [points[round(i * (len(points) - 1) / (count - 1))] for i in range(count)]

    def across(a, b, count):
        """Points on the hood along the straight line a -> b in plan."""
        out = []
        for i in range(count):
            t = i / (count - 1)
            x, y = a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])
            out.append(a if i == 0 else b if i == count - 1 else (x, y, height(vertices, triangles, x, y)))
        return out

    def curve(id_, points, count=9, degree=3):
        pts = thin(points, count)
        return {"id": id_, "degree": degree, "through": [[round(c, 4) for c in p] for p in pts]}

    def edge(id_, points):
        """An outline or centre-line curve: through every vertex on it, at
        degree 5. Cubics through a thinned set missed the real edge by up to
        0.9 mm, and boundary rows are held on their curves exactly."""
        return curve(id_, points, count=len(points), degree=5)

    # Grid nodes. u runs outwards (centre line c = 0 to side c = N), v rearwards
    # (front r = 0 to rear r = N). Edge nodes cut each boundary into equal
    # lengths; inner nodes blend them in plan (Coons) and sit on the hood.
    n = GRID
    rear_path = list(reversed(rear))  # centre to side, like front
    cut = {"front": split(front, n), "rear": split(rear_path, n),
           "centre": split(centre, n), "side": split(side, n)}  # fmt: skip
    front_nodes = [front[i] for i in cut["front"]]
    rear_nodes = [rear_path[i] for i in cut["rear"]]
    centre_nodes = [centre[i] for i in cut["centre"]]
    side_nodes = [side[i] for i in cut["side"]]

    def node(r, c):
        if r == 0:
            return front_nodes[c]
        if r == n:
            return rear_nodes[c]
        if c == 0:
            return centre_nodes[r]
        if c == n:
            return side_nodes[r]
        s_, t_ = c / n, r / n
        xy = [
            (1 - t_) * front_nodes[c][k] + t_ * rear_nodes[c][k] + (1 - s_) * centre_nodes[r][k]
            + s_ * side_nodes[r][k]
            - ((1 - s_) * (1 - t_) * front_nodes[0][k] + s_ * (1 - t_) * front_nodes[n][k]
               + (1 - s_) * t_ * rear_nodes[0][k] + s_ * t_ * rear_nodes[n][k])
            for k in (0, 1)
        ]  # fmt: skip
        return (xy[0], xy[1], height(vertices, triangles, xy[0], xy[1]))

    nodes = {(r, c): node(r, c) for r in range(n + 1) for c in range(n + 1)}
    curves = []
    along_u = []  # rows r: curves from (r, c) to (r, c + 1)
    for r in range(n + 1):
        line = []
        for c in range(n):
            id_ = f"u{r}_{c}"
            if r == 0:
                curves.append(edge(id_, front[cut["front"][c] : cut["front"][c + 1] + 1]))
            elif r == n:
                curves.append(edge(id_, rear_path[cut["rear"][c] : cut["rear"][c + 1] + 1]))
            else:
                curves.append(curve(id_, across(nodes[(r, c)], nodes[(r, c + 1)], 9)))
            line.append(id_)
        along_u.append(line)
    along_v = []  # columns c: curves from (r, c) to (r + 1, c)
    for c in range(n + 1):
        line = []
        for r in range(n):
            id_ = f"v{c}_{r}"
            if c == 0:
                curves.append(edge(id_, centre[cut["centre"][r] : cut["centre"][r + 1] + 1]))
            elif c == n:
                curves.append(edge(id_, side[cut["side"][r] : cut["side"][r + 1] + 1]))
            else:
                curves.append(curve(id_, across(nodes[(r, c)], nodes[(r + 1, c)], 9)))
            line.append(id_)
        along_v.append(line)

    patches = []
    edges = []
    for r in range(n):
        for c in range(n):
            patches.append({"id": f"hood_{r}_{c}",
                            "recipe": {"type": "from_boundaries", "UMin": along_v[c][r], "UMax": along_v[c + 1][r],
                                       "VMin": along_u[r][c], "VMax": along_u[r + 1][c]}})  # fmt: skip
            if c + 1 < n:
                edges.append({"a": [f"hood_{r}_{c}", "UMax"], "b": [f"hood_{r}_{c + 1}", "UMin"], "continuity": "G2"})
            if r + 1 < n:
                edges.append({"a": [f"hood_{r}_{c}", "VMax"], "b": [f"hood_{r + 1}_{c}", "VMin"], "continuity": "G2"})
    scene = {
        "format": "ada-scene",
        "version": 1,
        "units": "mm",
        "description": (
            f"L3: the DrivAer hood (DrivAerML run 1, CC BY-SA 4.0) rebuilt from its CAD tessellation: "
            f"a {n} x {n} network on the hood's outline and lines across it, G2 on every joint and at "
            "Y = 0, fitted to the tessellation's vertices (they lie on the CAD surface; the flat "
            "facets between them do not), within 0.5 mm. Curvature reversal is allowed because the "
            "hood itself bends upwards at its rear-outer corner. The control-net zig-zag and "
            "highlight-inflection budgets are the observed values, a regression guard: G2 joints "
            "with matched parameter speed space control points unevenly between patches of "
            "different size."
        ),
        "curves": curves,
        "patches": patches,
        "edges": edges,
        "symmetry": [{"edge": [f"hood_{r}_0", "UMin"], "continuity": "G2"} for r in range(n)],
        "targets": [{"id": "drivaer", "file": "targets/drivaer-hood.ply", "sample": "vertices"}],
        # Weight 10: the fit still holds 0.26 mm, and the fairness energy gets a
        # say in where the control points sit (at 1000 the control nets are
        # visibly rougher for 0.06 mm less).
        "fit": {"target": "drivaer", "weight": 10, "economy": True},
        "budgets": {
            "fit_max_mm": 0.5,
            "max_degree": 7,
            # The hood itself bends upwards near its rear-outer corner: z''
            # along x changes sign beyond x = 400 mm for y >= 400 mm.
            "allow_curvature_reversal": True,
            # Regression guards at the observed values, not Class-A rules: the
            # G2 joints are solved with matched parameter speed (alpha = 1),
            # which spaces control points unevenly across joints between
            # patches of different size.
            "max_zigzag_rows": 12,
            "max_highlight_inflections": 3,
        },
    }
    CASE.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {CASE.relative_to(REPO)}")


if __name__ == "__main__":
    main()
