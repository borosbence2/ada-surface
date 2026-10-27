import type { Check, QualityReportJson, Selection } from '../shared/types';
import { kStatusColors, statusOf, type Status } from './status';

interface ReportPanelProps {
  report: QualityReportJson | null;
  selection: Selection | null;
}

function Dot({ status }: { status: Status }) {
  return <span className="dot" style={{ background: kStatusColors[status] }} title={status} />;
}

function exp(value: number): string {
  return value === 0 ? '0' : value.toExponential(2);
}

function failed(where: string, checks: Check[]): string[] {
  return checks
    .filter((c) => !c.pass)
    .map((c) => `${where}: ${c.name} ${exp(c.value)} > ${exp(c.budget)}`);
}

export function ReportPanel({ report, selection }: ReportPanelProps) {
  if (!report) {
    return (
      <aside className="report">
        <p className="report__empty">No scene open.</p>
      </aside>
    );
  }
  const failures = [
    ...report.patches.flatMap((p) => failed(p.id, p.checks)),
    ...report.edges.flatMap((e) => failed(`${e.a} ~ ${e.b}`, e.checks)),
    ...report.symmetry.flatMap((s) => failed(`${s.edge} (Y=0)`, s.checks)),
    ...(report.fit ? failed(`fit to ${report.fit.target}`, report.fit.checks) : []),
  ];

  return (
    <aside className="report">
      <div className={`report__verdict ${report.pass ? 'is-pass' : 'is-fail'}`}>
        {report.pass ? 'PASS' : 'FAIL'}
      </div>

      {selection && (
        <section>
          <h3>Selection</h3>
          <p className="report__mono">
            {selection.patch}
            {selection.edge ? `.${selection.edge}` : ''} at (u, v) = ({selection.uv[0].toFixed(3)},{' '}
            {selection.uv[1].toFixed(3)})
          </p>
        </section>
      )}

      <section>
        <h3>Edges</h3>
        {report.edges.length === 0 && <p className="report__empty">none linked</p>}
        <table>
          <tbody>
            {report.edges.map((e) => (
              <tr key={`${e.a}~${e.b}`}>
                <td>
                  <Dot status={statusOf(e.checks)} />
                </td>
                <td className="report__mono">
                  {e.a} ~ {e.b} <span className="report__dim">{e.continuity}</span>
                  <div className="report__dim">
                    gap {exp(e.g0_gap_mm)} mm · angle {exp(e.g1_angle_deg)}° · curv{' '}
                    {exp(e.g2_relative)}
                    {e.fold ? ' · FOLD' : ''}
                  </div>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>

      <section>
        <h3>Symmetry (Y = 0)</h3>
        {report.symmetry.length === 0 && <p className="report__empty">none</p>}
        <table>
          <tbody>
            {report.symmetry.map((s) => (
              <tr key={s.edge}>
                <td>
                  <Dot status={statusOf(s.checks)} />
                </td>
                <td className="report__mono">
                  {s.edge} <span className="report__dim">{s.continuity}</span>
                  <div className="report__dim">
                    off-plane {exp(s.g0_plane_distance_mm)} mm · tilt {exp(s.g1_normal_tilt_deg)}°
                  </div>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>

      <section>
        <h3>Patches</h3>
        <table>
          <tbody>
            {report.patches.map((p) => {
              const radius =
                p.curvature.max_abs_principal > 0 ? 1 / p.curvature.max_abs_principal : Infinity;
              return (
                <tr key={p.id}>
                  <td>
                    <Dot status={statusOf(p.checks)} />
                  </td>
                  <td className="report__mono">
                    {p.id}{' '}
                    <span className="report__dim">
                      {p.degree[0]}×{p.degree[1]}
                    </span>
                    <div className="report__dim">
                      R<sub>min</sub> {Number.isFinite(radius) ? `${radius.toFixed(0)} mm` : '∞'} ·
                      highlights {p.highlights.lines} (max {p.highlights.max_inflections} infl.) ·
                      zig-zag rows {p.cv_hull.zigzag_rows}
                    </div>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </section>

      {report.fit && (
        <section>
          <h3>Fit to target</h3>
          <table>
            <tbody>
              <tr>
                <td>
                  <Dot status={statusOf(report.fit.checks)} />
                </td>
                <td className="report__mono">
                  {report.fit.target}
                  <div className="report__dim">
                    {report.fit.loaded
                      ? `${report.fit.points} points (${report.fit.outside} beyond the patches) · ` +
                        `max ${report.fit.max_mm.toFixed(3)} mm · rms ${report.fit.rms_mm.toFixed(3)} mm · ` +
                        `p95 ${report.fit.p95_mm.toFixed(3)} mm`
                      : 'not loaded'}
                  </div>
                </td>
              </tr>
            </tbody>
          </table>
        </section>
      )}

      {failures.length > 0 && (
        <section>
          <h3>Failed checks</h3>
          <ul className="report__failures">
            {failures.map((f) => (
              <li key={f}>{f}</li>
            ))}
          </ul>
        </section>
      )}

      <p className="report__legend">
        <Dot status="pass" /> under half the budget <Dot status="warn" /> within it{' '}
        <Dot status="fail" /> over it
      </p>
    </aside>
  );
}
