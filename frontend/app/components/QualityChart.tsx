"use client";
import { useState } from "react";
import type { HourlyRow, Param, Reading } from "../lib/api";
import { COLORS, META, wibHHMM } from "../lib/aq";
import LineChart, { Legend } from "./LineChart";

/**
 * Grafik uji T9: 2m_raw, titik outlier, 2m_validated, dan 1h_valid dalam satu grafik.
 * Nilai F_RANGE (di luar rentang ukur) tidak digambar agar skala tetap terbaca; jumlahnya ditampilkan.
 */
export default function QualityChart({ recent, hourly }: { recent: Reading[]; hourly: HourlyRow[] }) {
  const [param, setParam] = useState<Param>("pm25");
  const m = META[param];
  const i = m.flagIndex;
  const rangeCount = recent.filter((r) => r.flags[i] === 2).length;
  const outliers = recent.filter((r) => r.flags[i] === 3);

  const series = [
    { id: "raw", label: "2m_raw", color: COLORS.raw, width: 1.25,
      points: recent.map((r) => ({ x: r.ts, y: r.flags[i] === 2 ? null : r[param] })) },
    { id: "val", label: "2m_validated", color: COLORS.s1,
      points: recent.map((r) => ({ x: r.ts, y: r.flags[i] === 0 ? r[param] : null })) },
    { id: "hour", label: "1h_valid", color: COLORS.s3, width: 2.5,
      points: hourly.map((h) => ({ x: h.ts + 1800, y: h[param] })) },
    { id: "out", label: "Outlier (F_OUTLIER)", color: COLORS.critical, style: "dots" as const,
      points: outliers.map((r) => ({ x: r.ts, y: r[param] })) },
  ];

  return (
    <article className="card chart-card wide">
      <header className="chart-head">
        <div>
          <h3>Raw vs tervalidasi <span className="muted">· {m.short} 24 jam</span></h3>
          <p className="muted small">Uji T9: data mentah, pencilan, data tervalidasi, dan rata-rata jam dalam satu grafik.</p>
        </div>
        <div className="segmented small" role="tablist" aria-label="Parameter">
          {(["pm25", "co2"] as Param[]).map((p) => (
            <button key={p} role="tab" aria-selected={param === p} onClick={() => setParam(p)}>{META[p].short}</button>
          ))}
        </div>
      </header>
      <Legend items={[
        { label: "2m_raw", color: COLORS.raw },
        { label: "2m_validated", color: COLORS.s1 },
        { label: "1h_valid", color: COLORS.s3 },
        { label: `Outlier · ${outliers.length}`, color: COLORS.critical, style: "dots" },
      ]} />
      <LineChart ariaLabel={`${m.label}: raw, validated, hourly, outlier`} unit={m.unit} decimals={m.decimals}
        height={280} xFormat={wibHHMM} series={series} />
      <p className="muted small">
        {rangeCount > 0 ? `${rangeCount} nilai F_RANGE tidak digambar (di luar rentang ukur sensor). ` : ""}
        Titik 1h_valid digambar di tengah jam yang dirata-rata.
      </p>
    </article>
  );
}
