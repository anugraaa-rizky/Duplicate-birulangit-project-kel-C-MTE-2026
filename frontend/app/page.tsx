"use client";
import { Fragment, useEffect, useState } from "react";
import { DEFAULT_DEVICE, POLL_MS, api, PARAMS } from "./lib/api";
import type { Device, ForecastResponse, HourlyRow, Reading, Summary, TorCount } from "./lib/api";
import { META, fmt, wibHHMM } from "./lib/aq";
import { usePoll } from "./lib/usePoll";
import LatestCard from "./components/LatestCard";
import HourlyChart from "./components/HourlyChart";
import QualityChart from "./components/QualityChart";
import ForecastPanel from "./components/ForecastPanel";
import DataTable from "./components/DataTable";
import { CloudLogo } from "./components/Icons";

type View = "ringkas" | "grafik" | "prakiraan" | "tabel";
const VIEWS: { id: View; label: string }[] = [
  { id: "ringkas", label: "Ringkas" },
  { id: "grafik", label: "Grafik" },
  { id: "prakiraan", label: "Prakiraan ML" },
  { id: "tabel", label: "Tabel" },
];
const TONES = ["a", "b", "c", "d"] as const;

interface Bundle {
  latest: Reading | null;
  hourly: HourlyRow[];
  recent: Reading[];
  summary: Summary | null;
  forecast: ForecastResponse | null;
  tor: TorCount[];
  devices: Device[];
}

function store(key: string, value?: string): string | null {
  try {
    if (value !== undefined) localStorage.setItem(key, value);
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

export default function Dashboard() {
  const [view, setView] = useState<View>("ringkas");
  const [device, setDevice] = useState(DEFAULT_DEVICE);
  const [theme, setTheme] = useState<"light" | "dark" | null>(null);

  useEffect(() => {
    const q = new URLSearchParams(location.search);
    const v = (q.get("view") ?? store("lb.view")) as View | null;
    if (v && VIEWS.some((x) => x.id === v)) setView(v);
    const qt = q.get("theme");
    if (qt === "light" || qt === "dark") document.documentElement.dataset.theme = qt;
    const t = document.documentElement.dataset.theme as "light" | "dark" | undefined;
    setTheme(t ?? (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light"));
  }, []);

  const toggleTheme = () => {
    const next = theme === "dark" ? "light" : "dark";
    document.documentElement.dataset.theme = next;
    store("lb.theme", next);
    setTheme(next);
  };
  const choose = (v: View) => { setView(v); store("lb.view", v); };

  const { data, error, loading, refresh } = usePoll<Bundle>(async () => {
    const [devices, latest] = await Promise.all([api.devices(), api.latest(device)]);
    const [hourly, recent, summary, forecast, tor] = await Promise.all([
      api.hourly(device), api.recent(device), api.summary(device).catch(() => null),
      api.forecast(device, 6).catch(() => null), api.tor().catch(() => []),
    ]);
    return { devices, latest: "ts" in latest ? (latest as Reading) : null, hourly, recent, summary, forecast, tor };
  }, POLL_MS, [device]);

  const latest = data?.latest ?? null;
  const pmNext = data?.forecast?.forecast.pm25?.points[0];

  return (
    <main className="shell">
      <header className="topbar">
        <div className="brand">
          <CloudLogo />
          <div>
            <h1>langitbiru</h1>
            <p className="muted small">Kualitas udara · CO₂ · PM2.5 · T/RH</p>
          </div>
        </div>
        <div className="topbar-actions">
          <select aria-label="Perangkat" value={device} onChange={(e) => setDevice(e.target.value)}>
            {(data?.devices.length ? data.devices : [{ device_id: device } as Device]).map((d) => (
              <option key={d.device_id} value={d.device_id}>{d.device_id}</option>
            ))}
          </select>
          <button className="icon-btn" onClick={refresh} aria-label="Muat ulang" title="Muat ulang">
            <svg width="16" height="16" viewBox="0 0 24 24" aria-hidden className={loading ? "spin" : undefined}>
              <path d="M20 12a8 8 0 1 1-2.3-5.6M20 4v5h-5" fill="none" stroke="currentColor" strokeWidth="2.4" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          </button>
          <button className="icon-btn" onClick={toggleTheme} aria-label="Ganti tema" title={theme === "dark" ? "Langit siang" : "Langit malam"}>
            {theme === "dark" ? (
              <svg width="16" height="16" viewBox="0 0 24 24" aria-hidden><circle cx="12" cy="12" r="4.5" fill="currentColor" />
                <path d="M12 2v2.5M12 19.5V22M2 12h2.5M19.5 12H22M4.9 4.9l1.8 1.8M17.3 17.3l1.8 1.8M4.9 19.1l1.8-1.8M17.3 6.7l1.8-1.8" stroke="currentColor" strokeWidth="2" strokeLinecap="round" /></svg>
            ) : (
              <svg width="16" height="16" viewBox="0 0 24 24" aria-hidden><path d="M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5Z" fill="currentColor" /></svg>
            )}
          </button>
        </div>
      </header>

      <nav className="segmented views" role="tablist" aria-label="Tampilan data">
        {VIEWS.map((v) => (
          <button key={v.id} role="tab" aria-selected={view === v.id} onClick={() => choose(v.id)}>{v.label}</button>
        ))}
      </nav>

      <div className="statusline">
        <span className={`live ${error ? "off" : "on"}`}>{error ? "Backend tidak terjangkau" : "Terhubung"}</span>
        {latest && <span className="pill pill-soft">Slot terakhir {latest.time.slice(0, 16)} WIB</span>}
        {data?.summary && (
          <span className="pill pill-soft">Hari ini {data.summary.records}/{data.summary.expected_so_far} slot</span>
        )}
        <span className="muted small">Polling tiap {Math.round(POLL_MS / 1000)} s</span>
      </div>

      {error && !data && (
        <div className="card notice">
          <h3>Belum bisa mengambil data</h3>
          <p className="muted">{error}. Jalankan backend: <code>uvicorn app.main:app --port 8000</code> di folder <code>backend/</code>, lalu isi data dengan <code>python simulator.py --seed-days 3</code>.</p>
        </div>
      )}

      {data && view === "ringkas" && (
        <>
          <section className="grid-4">
            {PARAMS.map((p, i) => <LatestCard key={p} param={p} reading={latest} hourly={data.hourly} tone={TONES[i]} />)}
          </section>
          <section className="grid-3">
            <article className="card tone-ink">
              <p className="small">Prakiraan PM2.5 jam berikutnya</p>
              <p className="stat-big">{fmt(pmNext?.value ?? null, 1)} <small>µg/m³</small></p>
              <p className="small">{pmNext ? `pukul ${wibHHMM(pmNext.ts)} WIB · ±${fmt((pmNext.upper - pmNext.lower) / 2, 1)}` : "data belum cukup"}</p>
              <button className="pill pill-invert" onClick={() => choose("prakiraan")}>Lihat prakiraan →</button>
            </article>
            {data.summary && (
              <article className="card">
                <p className="small muted">Kelengkapan hari ini ({data.summary.date})</p>
                <p className="stat-big">{data.summary.records}<small> / {data.summary.expected_day}</small></p>
                <div className="meter" aria-hidden><span style={{ width: `${(data.summary.records / data.summary.expected_day) * 100}%` }} /></div>
                <p className="small muted">Target T2: 720 record 2 menit per hari</p>
              </article>
            )}
            {data.summary && (
              <article className="card">
                <p className="small muted">Flag hari ini</p>
                <dl className="metrics">
                  {PARAMS.map((p) => {
                    const c = data.summary!.flags[p];
                    return (<Fragment key={p}><dt>{META[p].short}</dt>
                      <dd>{c.F_OUTLIER} outlier · {c.F_RANGE} range · {c.F_SENSOR} sensor</dd></Fragment>);
                  })}
                </dl>
              </article>
            )}
          </section>
        </>
      )}

      {data && view === "grafik" && (
        <>
          <QualityChart recent={data.recent} hourly={data.hourly} />
          <section className="grid-2">
            {PARAMS.map((p) => <HourlyChart key={p} param={p} rows={data.hourly} />)}
          </section>
        </>
      )}

      {data && view === "prakiraan" && <ForecastPanel data={data.forecast} />}
      {data && view === "tabel" && <DataTable rows={data.recent} tor={data.tor} />}

      <footer className="foot muted small">
        ESP32 → FastAPI → SQLite → Next.js · API {process.env.NEXT_PUBLIC_API_URL ?? "http://localhost:8000"}
      </footer>
    </main>
  );
}
