"""LangitBiru AQMS backend (FastAPI + SQLite).

Contract follows "Pengantar IoT Web Application" (P3):
  POST /api/readings          ESP32 -> server, one packet per 2-minute slot, 201 Created
  GET  /api/readings/latest   newest record with flags (value cards)
  GET  /api/readings/hourly   24 hourly means from clean data only (chart)
Extra endpoints for the dashboard: recent raw records, daily summary, ML forecast,
plus a local mirror of the RnD BiruLangit TOR server for offline testing.
"""
from __future__ import annotations

import json
import os
import time
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from typing import Literal

from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from . import db
from .forecast import forecast_series

WIB = timezone(timedelta(hours=7))
PARAMS = ("co2", "pm25", "temp", "rh")          # order of the flags array
FLAG_NAMES = {0: "F_CLEAN", 1: "F_SENSOR", 2: "F_RANGE", 3: "F_OUTLIER"}
HOURLY_MIN_VALID = 23                            # same completeness rule as firmware F8
SLOTS_PER_DAY = 720



@asynccontextmanager
async def lifespan(_: FastAPI):
    db.connect()
    yield


app = FastAPI(title="LangitBiru AQMS API", version="1.0.0", lifespan=lifespan,
              description="Menerima paket ESP32, menyajikan data ke dashboard, dan prakiraan ML.")

origins = os.environ.get("AQ_CORS_ORIGINS", "http://localhost:3000,http://127.0.0.1:3000").split(",")
app.add_middleware(CORSMiddleware, allow_origins=[o.strip() for o in origins if o.strip()],
                   allow_methods=["GET", "POST"], allow_headers=["*"])


class Reading(BaseModel):
    device_id: str = Field(min_length=1, max_length=64, examples=["aqms-01"])
    ts: int = Field(description="epoch UTC (detik), awal slot", examples=[1791010800])
    co2: float | None = Field(description="ppm", examples=[612.0])
    pm25: float | None = Field(description="ug/m3", examples=[23.5])
    temp: float | None = Field(description="degC", examples=[27.8])
    rh: float | None = Field(description="%RH", examples=[64.2])
    flags: list[int] = Field(min_length=4, max_length=4, examples=[[0, 2, 0, 0]],
                             description="urutan CO2, PM2.5, T, RH; 0 = bersih")


def wib(ts: int) -> str:
    return datetime.fromtimestamp(ts, WIB).strftime("%Y-%m-%d %H:%M:%S")


def resolve_device(device_id: str | None) -> str:
    if device_id:
        return device_id
    rows = db.query("SELECT device_id FROM readings ORDER BY ts DESC LIMIT 1")
    if not rows:
        raise HTTPException(404, "Belum ada data")
    return rows[0]["device_id"]


def last_ts(device_id: str) -> int | None:
    rows = db.query("SELECT MAX(ts) AS t FROM readings WHERE device_id=?", (device_id,))
    return rows[0]["t"] if rows and rows[0]["t"] is not None else None


def hourly_series(device_id: str, hours: int, end_ts: int | None = None,
                  min_valid: int = HOURLY_MIN_VALID) -> list[dict]:
    """Hourly means of clean (flag 0) values, one entry per hour, oldest first.

    A parameter's mean is null unless at least `min_valid` clean samples exist in that hour.
    """
    end_ts = end_ts if end_ts is not None else last_ts(device_id)
    if end_ts is None:
        return []
    end_hour = end_ts - end_ts % 3600
    start_hour = end_hour - (hours - 1) * 3600
    rows = db.query("SELECT ts, co2, pm25, temp, rh, flags FROM readings "
                    "WHERE device_id=? AND ts>=? AND ts<?", (device_id, start_hour, end_hour + 3600))
    acc = {start_hour + i * 3600: {p: [] for p in PARAMS} for i in range(hours)}
    for r in rows:
        bucket = acc.get(r["ts"] - r["ts"] % 3600)
        if bucket is None:
            continue
        flags = json.loads(r["flags"])
        for i, p in enumerate(PARAMS):
            if flags[i] == 0 and r[p] is not None:
                bucket[p].append(r[p])
    out = []
    for h, vals in acc.items():
        item = {"ts": h, "time": wib(h)}
        for p in PARAMS:
            n = len(vals[p])
            item[p] = round(sum(vals[p]) / n, 3) if n >= min_valid else None
            item[f"count_{p}"] = n
        out.append(item)
    return out


# ---------------------------------------------------------------- ESP32 -> server
@app.post("/api/readings", status_code=201, tags=["perangkat"])
def receive(r: Reading) -> dict:
    """Simpan satu paket. PRIMARY KEY (device_id, ts) membuat kiriman ulang tidak ganda."""
    changed = db.write("INSERT OR IGNORE INTO readings VALUES (?,?,?,?,?,?,?,?)",
                       (r.device_id, r.ts, r.co2, r.pm25, r.temp, r.rh, json.dumps(r.flags), int(time.time())))
    return {"status": "ok", "duplicate": changed == 0}


# ---------------------------------------------------------------- server -> dashboard
@app.get("/api/devices", tags=["dashboard"])
def devices() -> list[dict]:
    rows = db.query("SELECT device_id, COUNT(*) AS n, MAX(ts) AS last_ts FROM readings "
                    "GROUP BY device_id ORDER BY last_ts DESC")
    return [{"device_id": r["device_id"], "records": r["n"], "last_ts": r["last_ts"],
             "last_time": wib(r["last_ts"])} for r in rows]


@app.get("/api/readings/latest", tags=["dashboard"])
def latest(device_id: str | None = None) -> dict:
    device_id = resolve_device(device_id)
    rows = db.query("SELECT * FROM readings WHERE device_id=? ORDER BY ts DESC LIMIT 1", (device_id,))
    if not rows:
        return {}
    d = db.row_to_reading(rows[0])
    d["time"] = wib(d["ts"])
    d["age_s"] = int(time.time()) - d["ts"]
    return d


@app.get("/api/readings/hourly", tags=["dashboard"])
def hourly(device_id: str | None = None, hours: int = Query(24, ge=1, le=24 * 14)) -> list[dict]:
    """Rata-rata per jam dari data berflag 0; null bila < 23 sampel bersih."""
    return hourly_series(resolve_device(device_id), hours)


@app.get("/api/readings/recent", tags=["dashboard"])
def recent(device_id: str | None = None, hours: int = Query(24, ge=1, le=24 * 7)) -> list[dict]:
    """Record 2 menit mentah lengkap dengan flag, untuk grafik raw vs validated dan tabel."""
    device_id = resolve_device(device_id)
    end = last_ts(device_id)
    if end is None:
        return []
    rows = db.query("SELECT * FROM readings WHERE device_id=? AND ts>? ORDER BY ts",
                    (device_id, end - hours * 3600))
    out = []
    for r in rows:
        d = db.row_to_reading(r)
        d["time"] = wib(d["ts"])
        out.append(d)
    return out


@app.get("/api/summary", tags=["dashboard"])
def summary(device_id: str | None = None) -> dict:
    """Kelengkapan hari berjalan (WIB) dan jumlah flag per parameter."""
    device_id = resolve_device(device_id)
    end = last_ts(device_id)
    day_start = int(datetime.fromtimestamp(end, WIB).replace(hour=0, minute=0, second=0).timestamp())
    rows = db.query("SELECT flags FROM readings WHERE device_id=? AND ts>=? AND ts<?",
                    (device_id, day_start, day_start + 86400))
    counts = {p: {name: 0 for name in FLAG_NAMES.values()} for p in PARAMS}
    for r in rows:
        for i, f in enumerate(json.loads(r["flags"])):
            counts[PARAMS[i]][FLAG_NAMES.get(f, "F_CLEAN")] += 1
    elapsed_slots = min(SLOTS_PER_DAY, (end - day_start) // 120 + 1)
    return {"device_id": device_id, "date": datetime.fromtimestamp(day_start, WIB).strftime("%Y-%m-%d"),
            "records": len(rows), "expected_so_far": elapsed_slots, "expected_day": SLOTS_PER_DAY,
            "flags": counts}


@app.get("/api/forecast", tags=["ml"])
def forecast(device_id: str | None = None,
             param: Literal["co2", "pm25", "temp", "rh", "all"] = "all",
             horizon: int = Query(6, ge=1, le=24),
             history_hours: int = Query(24 * 7, ge=12, le=24 * 30)) -> dict:
    """Prakiraan per jam (ridge regression autoregresif) beserta backtest MAE."""
    device_id = resolve_device(device_id)
    series = hourly_series(device_id, history_hours)
    first = next((i for i, s in enumerate(series) if any(s[p] is not None for p in PARAMS)), len(series))
    series = series[first:]
    ts = [s["ts"] for s in series]
    wanted = PARAMS if param == "all" else (param,)
    result = {}
    for p in wanted:
        f = forecast_series(ts, [s[p] for s in series], horizon, lower_bound=None if p == "temp" else 0.0)
        for pt in f["points"]:
            pt["time"] = wib(pt["ts"])
        result[p] = f
    return {"device_id": device_id, "horizon": horizon, "generated_at": wib(int(time.time())),
            "history": [{"ts": s["ts"], "time": s["time"], **{p: s[p] for p in wanted}} for s in series[-24:]],
            "forecast": result}


# ---------------------------------------------------------------- local TOR server mirror
@app.get("/api/get-time", tags=["tor-mirror"])
def get_time() -> dict:
    """Sama seperti server RnD: {"datetime": "YYYY-MM-DD HH:MM:SS"} dalam WIB."""
    return {"datetime": datetime.now(WIB).strftime("%Y-%m-%d %H:%M:%S")}


@app.post("/api/post/{group}", status_code=201, tags=["tor-mirror"])
async def tor_post(group: str, request: Request) -> dict:
    """Terima payload TOR (device_1, created_at_1, ... semua string) untuk uji lokal T2/T3."""
    try:
        body = await request.json()
    except Exception:
        raise HTTPException(400, "Body bukan JSON")
    if not isinstance(body, dict) or "device_1" not in body or "created_at_1" not in body:
        raise HTTPException(422, "device_1 dan created_at_1 wajib ada")
    if not all(isinstance(v, str) for v in body.values()):
        raise HTTPException(422, "Semua nilai harus string sesuai TOR")
    changed = db.write("INSERT OR IGNORE INTO tor_packets VALUES (?,?,?,?)",
                       (body["device_1"], body["created_at_1"], json.dumps(body), int(time.time())))
    return {"status": "ok", "group": group, "duplicate": changed == 0}


@app.get("/api/tor/summary", tags=["tor-mirror"])
def tor_summary(date: str | None = None) -> list[dict]:
    """Jumlah paket per device_1 per tanggal (target T2: 720 / 720 / 24)."""
    sql = "SELECT device_1, substr(created_at,1,10) AS day, COUNT(*) AS n FROM tor_packets "
    params: tuple = ()
    if date:
        sql += "WHERE substr(created_at,1,10)=? "
        params = (date,)
    rows = db.query(sql + "GROUP BY device_1, day ORDER BY day DESC, device_1", params)
    target = {"2m_raw": 720, "2m_validated": 720, "1h_valid": 24}
    return [{"device_1": r["device_1"], "date": r["day"], "count": r["n"],
             "target": target.get(r["device_1"].split("-")[0])} for r in rows]


@app.get("/api/health", tags=["meta"])
def health() -> dict:
    n = db.query("SELECT COUNT(*) AS n FROM readings")[0]["n"]
    return {"status": "ok", "records": n, "server_time": get_time()["datetime"]}
