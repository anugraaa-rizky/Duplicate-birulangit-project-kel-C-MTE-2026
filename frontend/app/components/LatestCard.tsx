"use client";
import type { HourlyRow, Param, Reading } from "../lib/api";
import { FLAG_LABEL, META, fmt, statusOf } from "../lib/aq";
import { StatusChip, WarnIcon } from "./Icons";

interface Props {
  param: Param;
  reading: Reading | null;
  hourly: HourlyRow[];
  tone: "a" | "b" | "c" | "d";
}

/** Kartu nilai terkini: nilai mentah terbaru + flag; flag ≠ 0 diberi ikon peringatan. */
export default function LatestCard({ param, reading, hourly, tone }: Props) {
  const m = META[param];
  const value = reading ? reading[param] : null;
  const flag = reading ? reading.flags[m.flagIndex] : 0;
  const status = flag === 0 ? statusOf(param, value) : null;
  const spark = hourly.map((h) => h[param]);
  const known = spark.filter((v): v is number => v !== null);
  const lo = Math.min(...known), hi = Math.max(...known);
  const pts = spark.map((v, i) => v === null ? null :
    `${(i / Math.max(1, spark.length - 1)) * 100},${known.length > 1 && hi > lo ? 28 - ((v - lo) / (hi - lo)) * 24 : 16}`);
  let d = "", pen = false;
  for (const p of pts) { if (!p) { pen = false; continue; } d += (pen ? "L" : "M") + p; pen = true; }

  return (
    <article className={`card value-card tone-${tone}`}>
      <header className="value-head">
        <div>
          <h3>{m.short}</h3>
          <p className="muted">{m.label}</p>
        </div>
        {status && <StatusChip level={status.level} label={status.label} />}
      </header>
      <div className="value-number">
        <span className={flag !== 0 ? "flagged" : undefined}>{fmt(value, m.decimals)}</span>
        <small>{m.unit}</small>
      </div>
      {flag !== 0 ? (
        <p className="flag-note"><WarnIcon /> {FLAG_LABEL[flag] ?? `flag ${flag}`}</p>
      ) : (
        <p className="flag-note ok">Data bersih · flag 0</p>
      )}
      {known.length > 1 && (
        <div className="spark">
          <svg viewBox="0 0 100 32" preserveAspectRatio="none" aria-hidden>
            <path d={d} fill="none" vectorEffect="non-scaling-stroke" />
          </svg>
          <span className="muted small">24 jam · {fmt(lo, m.decimals)}–{fmt(hi, m.decimals)}</span>
        </div>
      )}
    </article>
  );
}
