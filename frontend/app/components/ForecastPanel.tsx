"use client";
import { useState } from "react";
import type { ForecastResponse, Param } from "../lib/api";
import { PARAMS } from "../lib/api";
import { COLORS, META, fmt, statusOf, wibHHMM } from "../lib/aq";
import LineChart, { Legend } from "./LineChart";
import { StatusChip } from "./Icons";

/** Prakiraan ML per jam: histori 24 jam (data bersih) + prakiraan ridge-AR dengan pita 95%. */
export default function ForecastPanel({ data }: { data: ForecastResponse | null }) {
  const [param, setParam] = useState<Param>("pm25");
  const m = META[param];
  const f = data?.forecast[param];
  const history = data?.history ?? [];
  const last = [...history].reverse().find((h) => h[param] !== null && h[param] !== undefined);

  const actual = history.map((h) => ({ x: h.ts, y: h[param] ?? null }));
  const fc = f?.status === "ok" && last
    ? [{ x: last.ts, y: last[param] as number }, ...f.points.map((p) => ({ x: p.ts, y: p.value }))]
    : [];
  const peak = f?.points.reduce<null | { value: number; ts: number }>((a, p) => (!a || p.value > a.value ? p : a), null);
  const peakStatus = peak ? statusOf(param, peak.value) : null;

  return (
    <section className="forecast">
      <div className="segmented" role="tablist" aria-label="Parameter prakiraan">
        {PARAMS.map((p) => (
          <button key={p} role="tab" aria-selected={param === p} onClick={() => setParam(p)}>{META[p].short}</button>
        ))}
      </div>

      <div className="forecast-grid">
        <article className="card chart-card wide">
          <header className="chart-head">
            <h3>Prakiraan {m.short} <span className="muted">· {data?.horizon ?? 6} jam ke depan</span></h3>
            {f?.model && <span className="pill">{f.model}</span>}
          </header>
          {f?.status === "insufficient_data" ? (
            <div className="chart-empty" style={{ height: 260 }}>
              Butuh minimal {f.hours_required} jam data bersih (tersedia {f.hours_available}).
            </div>
          ) : (
            <>
              <Legend items={[
                { label: "Aktual (rata-rata jam)", color: COLORS.s1 },
                { label: "Prakiraan", color: COLORS.s2, style: "dashed" },
                { label: "Interval 95%", color: COLORS.s2, style: "band" },
              ]} />
              <LineChart ariaLabel={`Prakiraan ${m.label}`} unit={m.unit} decimals={m.decimals} height={280}
                xFormat={wibHHMM}
                marker={last ? { x: last.ts, label: "terakhir" } : undefined}
                series={[
                  { id: "actual", label: "Aktual", color: COLORS.s1, points: actual },
                  { id: "fc", label: "Prakiraan", color: COLORS.s2, style: "dashed", points: fc },
                ]}
                band={f ? { label: "Interval 95%", color: COLORS.s2, points: f.points.map((p) => ({ x: p.ts, lo: p.lower, hi: p.upper })) } : undefined}
              />
            </>
          )}
        </article>

        <aside className="card stat-stack">
          <div>
            <p className="muted small">Puncak prakiraan</p>
            <p className="stat-big">{fmt(peak?.value ?? null, m.decimals)} <small>{m.unit}</small></p>
            <p className="muted small">{peak ? `pukul ${wibHHMM(peak.ts)} WIB` : "—"}</p>
            {peakStatus && <StatusChip level={peakStatus.level} label={peakStatus.label} />}
          </div>
          <hr />
          <div>
            <p className="muted small">Backtest {f?.metrics?.backtest_hours ?? "—"} jam (1 langkah)</p>
            <dl className="metrics">
              <dt>MAE model</dt><dd>{fmt(f?.metrics?.mae ?? null, 2)} {m.unit}</dd>
              <dt>MAE persistence</dt><dd>{fmt(f?.metrics?.mae_persistence ?? null, 2)} {m.unit}</dd>
              <dt>Skill</dt><dd>{f?.metrics?.skill != null ? `${Math.round(f.metrics.skill * 100)}%` : "—"}</dd>
            </dl>
          </div>
          <hr />
          <p className="muted small">
            Ridge regression autoregresif: lag 1–3 jam{f?.model?.includes("daily") ? ", lag 24 jam" : ""}, dan pola jam harian (sin/cos).
            Dilatih ulang tiap permintaan dari {f?.hours_trained ?? "—"} jam data bersih.
          </p>
        </aside>
      </div>

      {f?.status === "ok" && (
        <div className="card table-wrap">
          <table className="data">
            <thead><tr><th>Jam (WIB)</th><th>Prakiraan</th><th>Bawah</th><th>Atas</th></tr></thead>
            <tbody>
              {f.points.map((p) => (
                <tr key={p.ts}><td>{wibHHMM(p.ts)}</td><td>{fmt(p.value, m.decimals)}</td>
                  <td>{fmt(p.lower, m.decimals)}</td><td>{fmt(p.upper, m.decimals)}</td></tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </section>
  );
}
