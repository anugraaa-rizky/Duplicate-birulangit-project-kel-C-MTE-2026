"use client";
import { useLayoutEffect, useMemo, useRef, useState } from "react";

export interface Pt { x: number; y: number | null }
export interface Series {
  id: string;
  label: string;
  color: string;
  points: Pt[];
  style?: "line" | "dashed" | "dots";
  width?: number;
}
export interface Band { label: string; color: string; points: { x: number; lo: number; hi: number }[] }

interface Props {
  series: Series[];
  band?: Band;
  unit: string;
  decimals: number;
  height?: number;
  xFormat: (x: number) => string;
  marker?: { x: number; label: string };
  ariaLabel: string;
}

const M = { top: 14, right: 14, bottom: 28, left: 46 };

function niceTicks(min: number, max: number, count = 4): number[] {
  if (min === max) { min -= 1; max += 1; }
  const raw = (max - min) / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((s) => s >= raw) ?? raw;
  const out: number[] = [];
  for (let v = Math.ceil(min / step) * step; v <= max + step * 1e-9; v += step) out.push(+v.toFixed(10));
  return out;
}

function pathFor(points: Pt[], sx: (x: number) => number, sy: (y: number) => number): string {
  let d = "";
  let pen = false;
  for (const p of points) {
    if (p.y === null) { pen = false; continue; }
    d += `${pen ? "L" : "M"}${sx(p.x).toFixed(1)},${sy(p.y).toFixed(1)}`;
    pen = true;
  }
  return d;
}

export default function LineChart({ series, band, unit, decimals, height = 220, xFormat, marker, ariaLabel }: Props) {
  const wrap = useRef<HTMLDivElement>(null);
  const [width, setWidth] = useState(600);
  const [hover, setHover] = useState<number | null>(null);

  useLayoutEffect(() => {
    const el = wrap.current;
    if (!el) return;
    const measure = () => setWidth(Math.max(240, Math.floor(el.getBoundingClientRect().width)));
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const geo = useMemo(() => {
    const xs = new Set<number>();
    let lo = Infinity, hi = -Infinity;
    for (const s of series) for (const p of s.points) {
      xs.add(p.x);
      if (p.y !== null) { lo = Math.min(lo, p.y); hi = Math.max(hi, p.y); }
    }
    for (const p of band?.points ?? []) { xs.add(p.x); lo = Math.min(lo, p.lo); hi = Math.max(hi, p.hi); }
    const allX = [...xs].sort((a, b) => a - b);
    if (!allX.length || lo === Infinity) return null;
    const pad = (hi - lo) * 0.08 || 1;
    const ticks = niceTicks(lo - pad, hi + pad);
    const y0 = Math.min(lo - pad, ticks[0]), y1 = Math.max(hi + pad, ticks[ticks.length - 1]);
    const x0 = allX[0], x1 = allX[allX.length - 1] === x0 ? x0 + 1 : allX[allX.length - 1];
    const iw = width - M.left - M.right, ih = height - M.top - M.bottom;
    const sx = (x: number) => M.left + ((x - x0) / (x1 - x0)) * iw;
    const sy = (y: number) => M.top + (1 - (y - y0) / (y1 - y0)) * ih;
    const span = x1 - x0;
    const stepH = [1, 2, 3, 4, 6, 12, 24].find((h) => span / (h * 3600) <= 7) ?? 24;
    const xticks: number[] = [];
    for (let t = Math.ceil(x0 / (stepH * 3600)) * stepH * 3600; t <= x1; t += stepH * 3600) xticks.push(t);
    return { allX, ticks: ticks.filter((t) => t >= y0 && t <= y1), xticks, sx, sy, iw, ih };
  }, [series, band, width, height]);

  if (!geo) return <div className="chart chart-empty" ref={wrap} style={{ height }}>Belum ada data untuk digambar</div>;
  const { allX, ticks, xticks, sx, sy, ih } = geo;

  const onMove = (e: React.PointerEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const px = ((e.clientX - rect.left) / rect.width) * width;
    let best = allX[0], bestD = Infinity;
    for (const x of allX) { const d = Math.abs(sx(x) - px); if (d < bestD) { bestD = d; best = x; } }
    setHover(best);
  };

  const hoverRows = hover === null ? [] : [
    ...series.map((s) => ({ s, p: s.points.find((p) => p.x === hover) })).filter((r) => r.p && r.p.y !== null)
      .map(({ s, p }) => ({ id: s.id, label: s.label, color: s.color, text: `${p!.y!.toFixed(decimals)} ${unit}`, y: p!.y! })),
    ...(band ? band.points.filter((p) => p.x === hover).map((p) => ({
      id: "band", label: band.label, color: band.color, text: `${p.lo.toFixed(decimals)}–${p.hi.toFixed(decimals)}`, y: null as number | null,
    })) : []),
  ];
  const tipLeft = hover !== null && sx(hover) > width * 0.6;

  return (
    <div className="chart" ref={wrap} style={{ height }}>
      <svg width={width} height={height} role="img" aria-label={ariaLabel}
        onPointerMove={onMove} onPointerLeave={() => setHover(null)}>
        {ticks.map((t) => (
          <g key={t}>
            <line className="grid" x1={M.left} x2={width - M.right} y1={sy(t)} y2={sy(t)} />
            <text className="tick" x={M.left - 8} y={sy(t)} dy="0.32em" textAnchor="end">{t.toLocaleString("id-ID")}</text>
          </g>
        ))}
        {xticks.map((t) => (
          <text key={t} className="tick" x={sx(t)} y={height - 8} textAnchor="middle">{xFormat(t)}</text>
        ))}
        <line className="axis" x1={M.left} x2={width - M.right} y1={M.top + ih} y2={M.top + ih} />
        {marker && (
          <g>
            <line className="now" x1={sx(marker.x)} x2={sx(marker.x)} y1={M.top} y2={M.top + ih} />
            <text className="tick now-label" x={sx(marker.x) + 4} y={M.top + 10}>{marker.label}</text>
          </g>
        )}
        {band && band.points.length > 0 && (
          <path style={{ fill: band.color }} opacity={0.16}
            d={`M${band.points.map((p) => `${sx(p.x)},${sy(p.hi)}`).join("L")}L${[...band.points].reverse().map((p) => `${sx(p.x)},${sy(p.lo)}`).join("L")}Z`} />
        )}
        {series.map((s) => s.style === "dots" ? (
          <g key={s.id}>
            {s.points.filter((p) => p.y !== null).map((p) => (
              <circle key={p.x} cx={sx(p.x)} cy={sy(p.y!)} r={4} style={{ fill: s.color }} className="dot" />
            ))}
          </g>
        ) : (
          <path key={s.id} d={pathFor(s.points, sx, sy)} fill="none" style={{ stroke: s.color }} strokeWidth={s.width ?? 2}
            strokeLinejoin="round" strokeLinecap="round" strokeDasharray={s.style === "dashed" ? "5 4" : undefined} />
        ))}
        {hover !== null && (
          <g>
            <line className="crosshair" x1={sx(hover)} x2={sx(hover)} y1={M.top} y2={M.top + ih} />
            {hoverRows.filter((r) => r.y !== null).map((r) => (
              <circle key={r.id} cx={sx(hover)} cy={sy(r.y!)} r={4.5} style={{ fill: r.color }} className="dot" />
            ))}
          </g>
        )}
      </svg>
      {hover !== null && hoverRows.length > 0 && (
        <div className="tooltip" style={{ left: tipLeft ? undefined : sx(hover) + 12, right: tipLeft ? width - sx(hover) + 12 : undefined }}>
          <div className="tooltip-time">{xFormat(hover)}</div>
          {hoverRows.map((r) => (
            <div key={r.id} className="tooltip-row">
              <span className="swatch" style={{ background: r.color }} />
              <span className="tooltip-label">{r.label}</span>
              <b>{r.text}</b>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export function Legend({ items }: { items: { label: string; color: string; style?: "line" | "dashed" | "dots" | "band" }[] }) {
  return (
    <ul className="legend">
      {items.map((i) => (
        <li key={i.label}>
          <svg width="22" height="10" aria-hidden>
            {i.style === "dots" ? <circle cx="11" cy="5" r="4" style={{ fill: i.color }} /> :
              i.style === "band" ? <rect x="1" y="1" width="20" height="8" rx="2" style={{ fill: i.color }} opacity={0.25} /> :
                <line x1="1" x2="21" y1="5" y2="5" style={{ stroke: i.color }} strokeWidth="2.5" strokeDasharray={i.style === "dashed" ? "5 3" : undefined} />}
          </svg>
          {i.label}
        </li>
      ))}
    </ul>
  );
}
