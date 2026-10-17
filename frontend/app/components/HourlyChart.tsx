"use client";
import type { HourlyRow, Param } from "../lib/api";
import { COLORS, META, wibHHMM } from "../lib/aq";
import LineChart from "./LineChart";

/** Grafik 24 rata-rata per jam dari /api/readings/hourly (hanya data bersih). Satu parameter, satu sumbu. */
export default function HourlyChart({ param, rows }: { param: Param; rows: HourlyRow[] }) {
  const m = META[param];
  const valid = rows.filter((r) => r[param] !== null).length;
  return (
    <article className="card chart-card">
      <header className="chart-head">
        <h3>{m.short} <span className="muted">· {m.unit}</span></h3>
        <span className="pill pill-soft">{valid}/{rows.length} jam sah</span>
      </header>
      <LineChart
        ariaLabel={`Rata-rata per jam ${m.label} 24 jam terakhir`}
        unit={m.unit}
        decimals={m.decimals}
        height={190}
        xFormat={wibHHMM}
        series={[{ id: param, label: "Rata-rata jam", color: COLORS.s1, points: rows.map((r) => ({ x: r.ts, y: r[param] })) }]}
      />
    </article>
  );
}
