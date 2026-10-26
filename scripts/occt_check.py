#!/usr/bin/env python3
"""Check ADA's IGES export and continuity numbers with OpenCASCADE.

ADA measures its own surfaces, so a mistake shared by its surfaces and its
measurements could hide. This script has an independent kernel measure them
again. OpenCASCADE reads the exported IGES with its own parser, finds the
shared boundaries geometrically (not from ADA's joint list), and measures G0,
G1 and G2 along them with its own surface evaluation and curvature code. The
verdicts are then compared with `ada report`, check by check.

    python -m venv .venv
    .venv/Scripts/pip install -r scripts/requirements-occt.txt   # .venv/bin on Linux
    .venv/Scripts/python scripts/occt_check.py                   # every case
    .venv/Scripts/python scripts/occt_check.py cases/examples/hood.ada.json

Exit code 0 when OpenCASCADE agrees with ADA everywhere, 1 when it does not,
2 on errors. docs/occt-verification.md describes the measures and the results.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

try:
    from OCP.BRep import BRep_Tool
    from OCP.BRepCheck import BRepCheck_Analyzer
    from OCP.GeomAbs import GeomAbs_Shape
    from OCP.GeomAPI import GeomAPI_ProjectPointOnCurve
    from OCP.GeomLProp import GeomLProp_SLProps
    from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt, gp_Vec
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.IGESControl import IGESControl_Reader
    from OCP.LocalAnalysis import LocalAnalysis_SurfaceContinuity
    from OCP.Message import Message, Message_Gravity
    from OCP.TopAbs import TopAbs_FACE
    from OCP.TopoDS import TopoDS
except ImportError:
    sys.exit("OpenCASCADE is missing: pip install -r scripts/requirements-occt.txt (in a venv)")

REPO = Path(__file__).resolve().parent.parent
EDGES = ("UMin", "UMax", "VMin", "VMax")

SAMPLES = 65  # along each joint; includes ADA's 33 uniform samples
DETECT_SAMPLES = 9  # per boundary, when looking for shared boundaries
DETECT_MM = 1.0  # boundaries closer than this form a joint; G0 is judged on the budget
MIN_EDGE_MM = 1.0  # shorter boundaries are degenerate and never form a joint
CURVATURE_FLOOR = 1e-5  # 1/mm; ADA's kCurvatureFloor, so relative G2 values compare
PROPS_RESOLUTION = 1e-9  # GeomLProp_SLProps: derivatives shorter than this are undefined
POLE_TOLERANCE_MM = 1e-9  # IGES poles against the scene's control points
VALUE_AGREEMENT = 0.10  # a failing value must also match ADA's to within 10 %
IGES_UNIT_MM = 2  # Global section unit flag for millimetres
IGES_BSPLINE_SURFACE = 128


def uniform(n: int) -> list[float]:
    return [k / (n - 1) for k in range(n)]


# --- Sheets and their boundaries ----------------------------------------------


@dataclass
class Sheet:
    label: str
    surface: object  # Geom_BSplineSurface


def edge_range(surface, edge: str) -> tuple[float, float]:
    """Parameter range of a boundary (v for the U edges, u for the V edges)."""
    u1, u2, v1, v2 = surface.Bounds()
    return (v1, v2) if edge in ("UMin", "UMax") else (u1, u2)


def edge_uv(surface, edge: str, t: float) -> tuple[float, float]:
    """Surface parameters of the point at parameter t on a boundary."""
    u1, u2, v1, v2 = surface.Bounds()
    return {"UMin": (u1, t), "UMax": (u2, t), "VMin": (t, v1), "VMax": (t, v2)}[edge]


def boundary(surface, edge: str):
    u1, u2, v1, v2 = surface.Bounds()
    if edge in ("UMin", "UMax"):
        return surface.UIso(u1 if edge == "UMin" else u2)
    return surface.VIso(v1 if edge == "VMin" else v2)


def boundary_points(surface, edge: str, n: int) -> list:
    curve = boundary(surface, edge)
    lo, hi = edge_range(surface, edge)
    return [curve.Value(lo + s * (hi - lo)) for s in uniform(n)]


def polyline_length(points: list) -> float:
    return sum(a.Distance(b) for a, b in zip(points, points[1:]))


def project(point, curve, lo: float, hi: float) -> tuple[float, float]:
    """Closest point on curve[lo, hi] as (parameter, distance), ends included."""
    best = min(
        ((lo, point.Distance(curve.Value(lo))), (hi, point.Distance(curve.Value(hi)))),
        key=lambda c: c[1],
    )
    projection = GeomAPI_ProjectPointOnCurve(point, curve, lo, hi)
    if projection.NbPoints() > 0 and projection.LowerDistance() < best[1]:
        best = (projection.LowerDistanceParameter(), projection.LowerDistance())
    return best


def find_joints(sheets: list[Sheet]) -> list[tuple[int, str, int, str]]:
    """Pairs of boundaries that lie on each other, found from the geometry alone."""
    points = {
        (k, e): boundary_points(s.surface, e, DETECT_SAMPLES)
        for k, s in enumerate(sheets)
        for e in EDGES
    }

    def apart(a, ea, b, eb) -> float:
        curve = boundary(sheets[b].surface, eb)
        lo, hi = edge_range(sheets[b].surface, eb)
        return max(project(p, curve, lo, hi)[1] for p in points[(a, ea)])

    joints = []
    for a in range(len(sheets)):
        for b in range(a + 1, len(sheets)):
            for ea in EDGES:
                for eb in EDGES:
                    if min(polyline_length(points[(a, ea)]), polyline_length(points[(b, eb)])) < MIN_EDGE_MM:
                        continue
                    if max(apart(a, ea, b, eb), apart(b, eb, a, ea)) < DETECT_MM:
                        joints.append((a, ea, b, eb))
    return joints


def find_centre_edges(sheets: list[Sheet]) -> list[tuple[int, str]]:
    """Boundaries that lie in the Y = 0 plane."""
    found = []
    for k, s in enumerate(sheets):
        for e in EDGES:
            pts = boundary_points(s.surface, e, DETECT_SAMPLES)
            if polyline_length(pts) >= MIN_EDGE_MM and max(abs(p.Y()) for p in pts) < DETECT_MM:
                found.append((k, e))
    return found


# --- Measuring a joint ----------------------------------------------------------


@dataclass
class Measure:
    samples: int = 0
    undefined: int = 0
    gap_mm: float = 0.0
    angle_deg: float = 0.0
    g2_across: float = 0.0  # ADA's definition: normal curvature across the joint
    g2_any: float = 0.0  # the same, worst over every tangent direction
    fold: bool = False
    flipped: bool = False
    # OCCT's own LocalAnalysis_SurfaceContinuity, at the same points
    la_g1_deg: float = 0.0
    la_g2_gap: float = 0.0  # 1/mm
    la_is_g2: int = 0
    la_done: int = 0


def unit(v):
    length = v.Magnitude()
    return v.Divided(length) if length > PROPS_RESOLUTION else None


def without(v, n):
    """v with its component along the unit vector n removed."""
    return v.Subtracted(n.Multiplied(v.Dot(n)))


def inward(props, edge: str):
    return {
        "UMin": props.D1U(),
        "UMax": props.D1U().Reversed(),
        "VMin": props.D1V(),
        "VMax": props.D1V().Reversed(),
    }[edge]


class SecondForm:
    """Second fundamental form at a point, built from OCCT's principal curvatures."""

    def __init__(self, props, sign: float):
        self.sign = sign
        self.scale = max(abs(props.MaxCurvature()), abs(props.MinCurvature()))
        if props.IsUmbilic():
            self.terms = [(props.MeanCurvature(), None)]
        else:
            dmax, dmin = gp_Dir(), gp_Dir()
            props.CurvatureDirections(dmax, dmin)
            self.terms = [(props.MaxCurvature(), gp_Vec(dmax)), (props.MinCurvature(), gp_Vec(dmin))]

    def __call__(self, x, y) -> float:
        total = 0.0
        for k, d in self.terms:
            total += k * (x.Dot(y) if d is None else x.Dot(d) * y.Dot(d))
        return self.sign * total


def measure_joint(sa, ea: str, sb, eb: str, budgets: dict) -> Measure:
    """Walks boundary ea of sa and compares sb at the closest point of its boundary eb."""
    m = Measure()
    curve_b = boundary(sb, eb)
    lo_b, hi_b = edge_range(sb, eb)
    lo_a, hi_a = edge_range(sa, ea)
    sb_reversed = sb.UReversed()  # for LocalAnalysis when the normals oppose
    for s in uniform(SAMPLES):
        m.samples += 1
        ua, va = edge_uv(sa, ea, lo_a + s * (hi_a - lo_a))
        pa = GeomLProp_SLProps(sa, ua, va, 2, PROPS_RESOLUTION)
        tb, gap = project(pa.Value(), curve_b, lo_b, hi_b)
        m.gap_mm = max(m.gap_mm, gap)
        ub, vb = edge_uv(sb, eb, tb)
        pb = GeomLProp_SLProps(sb, ub, vb, 2, PROPS_RESOLUTION)

        t = unit(pa.D1V() if ea in ("UMin", "UMax") else pa.D1U())
        if t is None or not (pa.IsNormalDefined() and pb.IsNormalDefined()):
            m.undefined += 1
            continue
        na = gp_Vec(pa.Normal())
        nb = gp_Vec(pb.Normal())
        sign = -1.0 if na.Dot(nb) < 0.0 else 1.0
        m.flipped = m.flipped or sign < 0.0
        nb = nb.Multiplied(sign)
        m.angle_deg = max(m.angle_deg, math.degrees(math.atan2(na.Crossed(nb).Magnitude(), na.Dot(nb))))
        m.fold = m.fold or without(inward(pa, ea), t).Dot(without(inward(pb, eb), t)) > 0.0

        if sign < 0.0:
            s2, u2, v2 = sb_reversed, sb.UReversedParameter(ub), vb
        else:
            s2, u2, v2 = sb, ub, vb
        la = LocalAnalysis_SurfaceContinuity(
            sa, ua, va, s2, u2, v2, GeomAbs_Shape.GeomAbs_G2,
            EpsC0=budgets["g0_gap_mm"],
            EpsG1=math.radians(budgets["g1_angle_deg"]),
            Percent=budgets["g2_relative"],
        )
        if la.IsDone():
            m.la_done += 1
            m.la_g1_deg = max(m.la_g1_deg, math.degrees(la.G1Angle()))
            m.la_g2_gap = max(m.la_g2_gap, la.G2CurvatureGap())
            m.la_is_g2 += 1 if la.IsG2() else 0

        if not (pa.IsCurvatureDefined() and pb.IsCurvatureDefined()):
            m.undefined += 1
            continue
        fa, fb = SecondForm(pa, 1.0), SecondForm(pb, sign)
        da = na.Crossed(t)
        tb_unit = unit(without(t, nb))
        db = nb.Crossed(tb_unit)
        ka, kb = fa(da, da), fb(db, db)
        m.g2_across = max(m.g2_across, abs(ka - kb) / max(abs(ka), abs(kb), CURVATURE_FLOOR))
        # Largest normal-curvature difference over all directions: the larger
        # absolute eigenvalue of the difference of the two forms.
        p = fa(t, t) - fb(tb_unit, tb_unit)
        q = fa(t, da) - fb(tb_unit, db)
        r = ka - kb
        worst = abs(p + r) / 2.0 + math.hypot((p - r) / 2.0, q)
        m.g2_any = max(m.g2_any, worst / max(fa.scale, fb.scale, CURVATURE_FLOOR))
    return m


def measure_centre(surface, edge: str, budgets: dict) -> tuple[Measure, float, float]:
    """The sheet against its mirror image in Y = 0, plus plane distance and normal tilt."""
    mirror = surface.Mirrored(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0)))
    m = measure_joint(surface, edge, mirror, edge, budgets)
    lo, hi = edge_range(surface, edge)
    plane_mm = tilt_deg = 0.0
    for s in uniform(SAMPLES):
        u, v = edge_uv(surface, edge, lo + s * (hi - lo))
        props = GeomLProp_SLProps(surface, u, v, 1, PROPS_RESOLUTION)
        plane_mm = max(plane_mm, abs(props.Value().Y()))
        if props.IsNormalDefined():
            n = props.Normal()
            tilt_deg = max(tilt_deg, math.degrees(math.atan2(abs(n.Y()), math.hypot(n.X(), n.Z()))))
    return m, plane_mm, tilt_deg


# --- Reading the IGES file --------------------------------------------------------


def read_iges(path: Path) -> tuple[list[Sheet], list[str]]:
    """Every root entity as a sheet, plus whatever is wrong with the file."""
    reader = IGESControl_Reader()
    if reader.ReadFile(str(path)) != IFSelect_RetDone:
        raise RuntimeError(f"OpenCASCADE could not read {path}")
    problems = []
    unit_flag = reader.IGESModel().GlobalSection().UnitFlag()
    if unit_flag != IGES_UNIT_MM:
        problems.append(f"unit flag {unit_flag}, expected {IGES_UNIT_MM} (mm)")
    sheets = []
    for i in range(1, reader.NbRootsForTransfer() + 1):
        entity = reader.RootForTransfer(i)
        # IGES right-justifies the label field; OCCT keeps the padding.
        label = entity.ShortLabel().ToCString().strip() if entity.HasShortLabel() else ""
        if entity.TypeNumber() != IGES_BSPLINE_SURFACE:
            problems.append(f"root {i} ({label}) is entity {entity.TypeNumber()}")
            continue
        if not reader.TransferOneRoot(i):
            problems.append(f"root {i} ({label}) did not transfer")
            continue
        shape = reader.Shape(reader.NbShapes())
        if shape.ShapeType() != TopAbs_FACE:
            problems.append(f"root {i} ({label}) became a {shape.ShapeType()}, not a face")
            continue
        face = TopoDS.Face_s(shape)
        if not BRepCheck_Analyzer(face).IsValid():
            problems.append(f"root {i} ({label}) fails BRepCheck")
        sheets.append(Sheet(label, BRep_Tool.Surface_s(face)))
    return sheets, problems


def compare_poles(sheets: list[Sheet], patches: list[dict]) -> tuple[float, list[str]]:
    """Largest pole deviation from the scene's control points, plus structural problems."""
    problems = []
    if len(sheets) != len(patches):
        return math.inf, [f"{len(sheets)} sheets for {len(patches)} patches"]
    worst = 0.0
    for sheet, patch in zip(sheets, patches):
        s = sheet.surface
        du, dv = patch["degree"]
        if sheet.label != patch["id"][:8]:
            problems.append(f"label '{sheet.label}' for patch '{patch['id']}'")
        if (s.UDegree(), s.VDegree()) != (du, dv):
            problems.append(f"{patch['id']}: degree {s.UDegree()}x{s.VDegree()}, scene {du}x{dv}")
            continue
        if s.IsURational() or s.IsVRational():
            problems.append(f"{patch['id']}: rational")
        if (s.NbUKnots(), s.NbVKnots()) != (2, 2) or s.Bounds() != (0.0, 1.0, 0.0, 1.0):
            problems.append(f"{patch['id']}: not a single span on [0, 1]^2")
        nv = dv + 1
        for i in range(du + 1):
            for j in range(nv):
                x, y, z = patch["control_points"][i * nv + j]
                worst = max(worst, s.Pole(i + 1, j + 1).Distance(gp_Pnt(x, y, z)))
    return worst, problems


# --- Comparing with ADA -----------------------------------------------------------


@dataclass
class CheckResult:
    name: str
    budget: float
    ada: float
    occt: float
    ada_pass: bool

    @property
    def occt_pass(self) -> bool:
        return self.occt <= self.budget

    @property
    def agree(self) -> bool:
        if self.ada_pass != self.occt_pass:
            return False
        if self.ada_pass:
            return True
        return abs(self.occt - self.ada) <= VALUE_AGREEMENT * max(abs(self.ada), abs(self.occt))


@dataclass
class Row:
    joint: str
    needs: str
    measure: Measure
    checks: list[CheckResult] = field(default_factory=list)
    ada_values: dict = field(default_factory=dict)  # what ADA measured, by check name
    occt_values: dict = field(default_factory=dict)
    note: str = ""

    @property
    def agree(self) -> bool:
        return all(c.agree for c in self.checks)


@dataclass
class CaseResult:
    name: str
    sheets: int = 0
    ada_pass: bool = False
    pole_mm: float = 0.0
    problems: list[str] = field(default_factory=list)  # each one is a disagreement
    rows: list[Row] = field(default_factory=list)
    error: str = ""

    @property
    def checks(self) -> list[CheckResult]:
        return [c for row in self.rows for c in row.checks]

    @property
    def occt_pass(self) -> bool:
        return all(c.occt_pass for c in self.checks)

    @property
    def agree(self) -> bool:
        return not self.error and not self.problems and all(row.agree for row in self.rows)


def check_results(ada_checks: list[dict], occt: dict) -> list[CheckResult]:
    results = []
    for c in ada_checks:
        if c["name"] not in occt:
            raise RuntimeError(f"no OpenCASCADE measure for ADA check '{c['name']}'")
        results.append(CheckResult(c["name"], c["budget"], c["value"], occt[c["name"]], c["pass"]))
    return results


def split_ref(ref: str) -> tuple[str, str]:
    patch, edge = ref.rsplit(".", 1)
    return patch, edge


def run_ada(ada: Path, *args, ok=(0,)) -> None:
    result = subprocess.run([str(ada), *map(str, args)], capture_output=True, text=True)
    if result.returncode not in ok:
        raise RuntimeError(f"ada {args[0]} exited {result.returncode}: {result.stderr.strip() or result.stdout.strip()}")


def check_case(ada: Path, scene: Path, out: Path) -> CaseResult:
    name = scene.name.removesuffix(".ada.json")
    result = CaseResult(name)
    try:
        solved, report_path, iges = out / f"{name}.ada.json", out / f"{name}.report.json", out / f"{name}.igs"
        run_ada(ada, "solve", scene, "-o", solved, ok=(0, 1))  # 1: solved, quality fails
        run_ada(ada, "report", solved, "--json", report_path, ok=(0, 1))
        run_ada(ada, "export", solved, "-o", iges)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        patches = json.loads(solved.read_text(encoding="utf-8"))["patches"]
        budgets = report["budgets"]
        result.ada_pass = report["pass"]

        sheets, problems = read_iges(iges)
        result.sheets = len(sheets)
        result.problems += problems
        result.pole_mm, problems = compare_poles(sheets, patches)
        result.problems += problems
        if result.pole_mm > POLE_TOLERANCE_MM:
            result.problems.append(f"poles differ from the scene by {result.pole_mm:.2e} mm")
        if len(sheets) != len(patches):
            return result
        index = {p["id"]: k for k, p in enumerate(patches)}
        ids = [p["id"] for p in patches]

        found = {frozenset({(a, ea), (b, eb)}) for a, ea, b, eb in find_joints(sheets)}
        for edge in report["edges"]:
            (pa, ea), (pb, eb) = split_ref(edge["a"]), split_ref(edge["b"])
            a, b = index[pa], index[pb]
            key = frozenset({(a, ea), (b, eb)})
            if key not in found:
                result.problems.append(f"{edge['a']} ~ {edge['b']}: ADA reports a joint the geometry does not have")
            found.discard(key)
            m = measure_joint(sheets[a].surface, ea, sheets[b].surface, eb, budgets)
            occt = {
                "g0_gap_mm": m.gap_mm,
                "g1_angle_deg": m.angle_deg,
                "fold": 1.0 if m.fold else 0.0,
                "g2_relative": m.g2_across,
            }
            row = Row(f"{edge['a']} ~ {edge['b']}", edge["continuity"], m)
            row.checks = check_results(edge["checks"], occt)
            row.ada_values = {k: edge[k] for k in ("g0_gap_mm", "g1_angle_deg", "g2_relative")}
            row.occt_values = occt
            result.rows.append(row)
        for key in sorted(found, key=sorted):
            (a, ea), (b, eb) = sorted(key)
            result.problems.append(
                f"{ids[a]}.{ea} ~ {ids[b]}.{eb}: shared boundary that ADA does not report"
            )

        centre = set(find_centre_edges(sheets))
        for sym in report["symmetry"]:
            patch, edge = split_ref(sym["edge"])
            k = index[patch]
            if (k, edge) not in centre:
                result.problems.append(f"{sym['edge']}: ADA reports a Y = 0 edge that is not on Y = 0")
            centre.discard((k, edge))
            m, plane_mm, tilt_deg = measure_centre(sheets[k].surface, edge, budgets)
            occt = {
                "g0_plane_distance_mm": plane_mm,
                "g1_normal_tilt_deg": tilt_deg,
                "g2_mirror_relative": m.g2_across,
            }
            row = Row(f"{sym['edge']} ~ mirror", f"{sym['continuity']} (Y = 0)", m)
            row.checks = check_results(sym["checks"], occt)
            if sym["continuity"] == "G2":
                # ADA does not measure this: mirror G1 implies mirror G2 (MILESTONES.md §4).
                implied = sym.get("g2_implied_by_g1", False)
                row.checks.append(CheckResult("g2_mirror_relative", budgets["g2_relative"], 0.0, m.g2_across, implied))
                row.note = "ADA: G2 implied by G1"
            row.ada_values = {
                "g0_gap_mm": sym["g0_plane_distance_mm"],
                "g1_angle_deg": sym["g1_normal_tilt_deg"],
            }
            row.occt_values = {"g0_gap_mm": plane_mm, "g1_angle_deg": tilt_deg, "g2_relative": m.g2_across}
            result.rows.append(row)
        for k, edge in sorted(centre):
            m, plane_mm, tilt_deg = measure_centre(sheets[k].surface, edge, budgets)
            row = Row(f"{ids[k]}.{edge} ~ mirror", "none (Y = 0, unlinked)", m)
            row.occt_values = {"g0_gap_mm": plane_mm, "g1_angle_deg": tilt_deg, "g2_relative": m.g2_across}
            row.note = "lies on Y = 0, no symmetry link in the scene"
            result.rows.append(row)
    except (RuntimeError, KeyError, ValueError, OSError) as e:
        result.error = str(e)
    return result


# --- Output ----------------------------------------------------------------------


def num(value) -> str:
    return "-" if value is None else f"{value:.2e}"


def pair(row: Row, key: str) -> str:
    return f"{num(row.ada_values.get(key))} / {num(row.occt_values.get(key))}"


def markdown(results: list[CaseResult]) -> str:
    agree = all(r.agree for r in results)
    out = [f"### OpenCASCADE check: {'agrees with ADA' if agree else 'DISAGREES with ADA'}", ""]
    out.append("| case | sheets | poles vs scene (mm) | joints checked | ADA | OpenCASCADE | agree |")
    out.append("|---|---:|---:|---:|---|---|---|")
    for r in results:
        if r.error:
            out.append(f"| {r.name} | - | - | - | - | - | **ERROR** |")
            continue
        verdict = lambda ok: "pass" if ok else "fail"  # noqa: E731
        out.append(
            f"| {r.name} | {r.sheets} | {r.pole_mm:.1e} | {len(r.rows)} | {verdict(r.ada_pass)} "
            f"| {verdict(r.occt_pass)} | {'yes' if r.agree else '**NO**'} |"
        )
    out.append("")
    out.append("Joints, ADA / OpenCASCADE. G2 is the relative normal-curvature difference across")
    out.append("the joint; *all dir.* is the worst over every tangent direction; *OCCT LA* is")
    out.append("OpenCASCADE's own LocalAnalysis_SurfaceContinuity (max G2 gap in 1/mm, and at how")
    out.append("many samples it calls the joint G2).")
    out.append("")
    out.append("| case | joint | needs | G0 mm | G1 deg | G2 | G2 all dir. | OCCT LA G2 gap | OCCT LA IsG2 | agree |")
    out.append("|---|---|---|---|---|---|---:|---:|---:|---|")
    for r in results:
        for row in r.rows:
            m = row.measure
            agree = "yes" if row.agree else "**NO**"
            if not row.checks:
                agree = "(info)"
            g2 = pair(row, "g2_relative") if row.note != "ADA: G2 implied by G1" else f"implied / {num(m.g2_across)}"
            out.append(
                f"| {r.name} | {row.joint} | {row.needs} | {pair(row, 'g0_gap_mm')} | {pair(row, 'g1_angle_deg')} "
                f"| {g2} | {num(m.g2_any)} | {num(m.la_g2_gap)} | {m.la_is_g2}/{m.la_done} | {agree} |"
            )
    problems = [f"- **{r.name}**: {p}" for r in results for p in ([r.error] if r.error else r.problems)]
    for r in results:
        for row in r.rows:
            for c in row.checks:
                if not c.agree:
                    problems.append(
                        f"- **{r.name}** {row.joint}: {c.name} ADA {c.ada:.3e} ({'pass' if c.ada_pass else 'fail'}), "
                        f"OpenCASCADE {c.occt:.3e} ({'pass' if c.occt_pass else 'fail'}), budget {c.budget:g}"
                    )
            if row.measure.undefined:
                problems.append(f"- {r.name} {row.joint}: {row.measure.undefined} samples without curvature")
    if problems:
        out += ["", *problems]
    return "\n".join(out) + "\n"


def default_ada() -> Path:
    """The most recently built ada CLI, as Studio picks its kernel."""
    exe = "ada.exe" if os.name == "nt" else "ada"
    built = [p for p in (REPO / "build").glob(f"*/bin/{exe}") if p.is_file()]
    return max(built, key=lambda p: p.stat().st_mtime) if built else REPO / "build" / "dev" / "bin" / exe


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("scenes", nargs="*", type=Path, help="scene files (default: every case)")
    parser.add_argument("--ada", type=Path, default=default_ada(), help="the ada CLI")
    parser.add_argument("--out", type=Path, default=REPO / "results" / "occt", help="working directory")
    parser.add_argument("--markdown", type=Path, help="also write the result here (appends)")
    args = parser.parse_args()

    scenes = args.scenes or sorted((REPO / "cases").glob("*.ada.json")) + sorted(
        (REPO / "cases" / "examples").glob("*.ada.json")
    )
    if not args.ada.exists():
        print(f"no ada CLI at {args.ada}; build the kernel or pass --ada", file=sys.stderr)
        return 2
    args.out.mkdir(parents=True, exist_ok=True)
    for printer in Message.DefaultMessenger_s().Printers():
        printer.SetTraceLevel(Message_Gravity.Message_Fail)  # the IGES reader is chatty

    results = [check_case(args.ada, scene, args.out) for scene in scenes]
    text = markdown(results)
    print(text, end="")
    if args.markdown:
        with args.markdown.open("a", encoding="utf-8") as f:
            f.write(text)
    if any(r.error for r in results):
        return 2
    return 0 if all(r.agree for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
