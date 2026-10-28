#!/usr/bin/env python3
"""Writes the eval's starting scenes (agent/eval/setups/) from the committed
cases, so a change to a case carries over. Run it after changing a case;
the outputs are committed."""

import copy
import json
import math
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent
CASES = HERE.parent.parent / "cases"
SETUPS = HERE / "setups"


def load(name: str) -> dict:
    return json.loads((CASES / name).read_text(encoding="utf-8"))


def save(name: str, scene: dict, description: str) -> None:
    scene = copy.deepcopy(scene)
    scene["description"] = description
    SETUPS.mkdir(exist_ok=True)
    (SETUPS / name).write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8", newline="\n")


hood = load("L2-hood-2x2-sym.ada.json")
save("hood.ada.json", hood, "Eval setup: the L2 hood, a 2 x 2 network, G2 everywhere, symmetric at Y = 0.")

joint_g1 = copy.deepcopy(hood)
joint = next(e for e in joint_g1["edges"] if e["a"] == ["hood_0_0", "VMax"])
joint["continuity"] = "G1"
save("hood-joint-g1.ada.json",
     joint_g1,
     "Eval setup: the L2 hood with the front/rear joint on the centre side (hood_0_0.VMax ~ "
     "hood_1_0.VMin) only G1, so its zebra stripes kink there.")

half_roof = load("L0s-roof-centre-sym.ada.json")
half_roof.pop("symmetry", None)
save("roof-half-nosym.ada.json",
     half_roof,
     "Eval setup: the left half of the L0 roof (y >= 0) without its symmetry link.")

# A roof whose side edge (y = 650) was drawn through nine points on the roof
# shape plus a +-2 mm ripple, and fitted at degree 5: the curve follows the
# ripple, and the panel's highlight lines inherit it.
def roof_z(x: float, y: float) -> float:
    s, t = x / 1400.0, y / 650.0
    return 1450.0 + 90.0 * s * (1.0 - s) - 55.0 * t * t


wavy = load("L0-roof-centre.ada.json")
side = next(c for c in wavy["curves"] if c["id"] == "roof_umax")
(x0, y0, _), (x1, y1, _) = side["through"][0], side["through"][-1]
points = []
for k in range(9):
    s = k / 8
    x, y = x0 + s * (x1 - x0), y0 + s * (y1 - y0)
    ripple = 2.0 * math.sin(4 * math.pi * s)
    points.append([round(x, 1), round(y, 1), round(roof_z(x, y) + ripple, 1)])
points[0], points[-1] = side["through"][0], side["through"][-1]
side["through"] = points
side["degree"] = 5
save("roof-wavy.ada.json",
     wavy,
     "Eval setup: the L0 roof with its side edge at y = 650 rippling (degree 5 through nine "
     "points with a +-2 mm wave), so the highlight lines wobble.")

# The L3 target alone: the agent lays the network, constrains it and fits it.
drivaer = load("L3-hood-from-target.ada.json")
targets = [dict(t, file=os.path.relpath(CASES / t["file"], SETUPS).replace(os.sep, "/")) for t in drivaer["targets"]]
save("hood-target.ada.json",
     {"format": drivaer["format"], "version": drivaer["version"], "units": drivaer["units"],
      "curves": [], "patches": [], "targets": targets},
     "Eval setup: the DrivAer hood's right half (y >= 0), its CAD tessellation attached as the target "
     "'drivaer'. Nothing is built yet.")
print("wrote", ", ".join(sorted(p.name for p in SETUPS.glob("*.ada.json"))))
