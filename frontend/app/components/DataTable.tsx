"use client";
import { useMemo, useState } from "react";
import type { Reading, TorCount } from "../lib/api";
import { PARAMS } from "../lib/api";
import { FLAG_LABEL, META, fmt, hhmm } from "../lib/aq";
import { WarnIcon } from "./Icons";

const PAGE = 30;

/** Tabel record 2 menit (terbaru di atas) dengan flag per parameter, plus hitungan kiriman TOR. */
export default function DataTable({ rows, tor }: { rows: Reading[]; tor: TorCount[] }) {
  const [onlyFlagged, setOnlyFlagged] = useState(false);
  const [page, setPage] = useState(0);
  const list = useMemo(() => {
    const r = [...rows].reverse();
    return onlyFlagged ? r.filter((x) => x.flags.some((f) => f !== 0)) : r;
  }, [rows, onlyFlagged]);
  const pages = Math.max(1, Math.ceil(list.length / PAGE));
  const view = list.slice(page * PAGE, page * PAGE + PAGE);

  return (
    <section className="table-view">
      <div className="card table-wrap">
        <header className="chart-head">
          <h3>Record 2 menit <span className="muted">· {list.length} baris</span></h3>
          <label className="toggle">
            <input type="checkbox" checked={onlyFlagged} onChange={(e) => { setOnlyFlagged(e.target.checked); setPage(0); }} />
            Hanya yang ber-flag
          </label>
        </header>
        <table className="data">
          <thead>
            <tr>
              <th>Waktu (WIB)</th>
              {PARAMS.map((p) => <th key={p}>{META[p].short} <span className="muted">{META[p].unit}</span></th>)}
            </tr>
          </thead>
          <tbody>
            {view.map((r) => (
              <tr key={r.ts}>
                <td><span className="muted">{r.time.slice(5, 10)}</span> {hhmm(r.time)}</td>
                {PARAMS.map((p) => {
                  const f = r.flags[META[p].flagIndex];
                  return (
                    <td key={p} className={f ? "cell-flag" : undefined} title={FLAG_LABEL[f]}>
                      {fmt(r[p], META[p].decimals)}
                      {f !== 0 && <span className="flag-tag"><WarnIcon />{["", "SENSOR", "RANGE", "OUTLIER"][f]}</span>}
                    </td>
                  );
                })}
              </tr>
            ))}
          </tbody>
        </table>
        <footer className="pager">
          <button disabled={page === 0} onClick={() => setPage(page - 1)}>‹ Baru</button>
          <span className="muted">{page + 1} / {pages}</span>
          <button disabled={page >= pages - 1} onClick={() => setPage(page + 1)}>Lama ›</button>
        </footer>
      </div>

      <div className="card table-wrap">
        <header className="chart-head">
          <h3>Kiriman format TOR <span className="muted">· mirror lokal</span></h3>
        </header>
        {tor.length === 0 ? (
          <p className="muted small">Belum ada paket TOR. Arahkan api_post firmware ke backend ini atau jalankan simulator dengan --tor.</p>
        ) : (
          <table className="data">
            <thead><tr><th>Tanggal</th><th>device_1</th><th>Jumlah</th><th>Target T2</th></tr></thead>
            <tbody>
              {tor.slice(0, 12).map((t) => (
                <tr key={t.device_1 + t.date}>
                  <td>{t.date}</td><td><code>{t.device_1}</code></td><td>{t.count}</td>
                  <td>{t.target ?? "—"}{t.target && t.count < t.target ? <span className="muted"> · kurang {t.target - t.count}</span> : ""}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </section>
  );
}
