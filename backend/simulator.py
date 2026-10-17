"""Virtual ESP32 for testing the backend and dashboard without hardware.

Ports the firmware's Simulasi.h (CO2/PM2.5 random walk + outliers + out-of-range)
and Edge.h (range check, IQR k=1.5 over 30 samples with minimum IQR) so the flags
look like the real device's. Temperature/RH follow a smooth daily curve in place of SHT31.

  python simulator.py --seed-days 3                 # backfill 3 days of 2-minute slots
  python simulator.py --live                        # then keep posting every 120 s
  python simulator.py --live --interval 5           # demo: one slot every 5 s (time advances 2 min)
  python simulator.py --seed-days 1 --tor           # also exercise the local TOR mirror
"""
from __future__ import annotations

import argparse
import math
import random
import time
from collections import deque

import httpx

WIB = 7 * 3600
F_CLEAN, F_SENSOR, F_RANGE, F_OUTLIER = 0, 1, 2, 3
MIN_IQR = {"temp": 0.5, "rh": 2.0, "co2": 20.0, "pm25": 2.0}
MIN_IQR_REL = {"temp": 0.0, "rh": 0.0, "co2": 0.30, "pm25": 0.30}   # x median, same as Edge.h
RANGES = {"temp": (-40, 125), "rh": (0, 100), "co2": (0, 5000), "pm25": (0, 1000)}


def base_co2(h: float, indoor: bool) -> float:
    if not indoor:
        return 450 + 35 * math.cos((h - 3) * math.pi / 12)
    rise = 950.0
    peak = rise * (1 - math.exp(-4.5))
    if 8 <= h <= 17:
        return 450 + rise * (1 - math.exp(-(h - 8) / 2))
    if h > 17:
        return 450 + peak * math.exp(-(h - 17) / 1.5)
    return 450 + peak * math.exp(-(h + 7) / 1.5)


def base_pm25(h: float, indoor: bool) -> float:
    morning = math.exp(-((h - 7.5) ** 2) / 2)
    evening = math.exp(-((h - 18.5) ** 2) / 2)
    outdoor = 18 + 55 * max(morning, evening)
    return 0.6 * outdoor if indoor else outdoor


class Device:
    def __init__(self, indoor: bool, rng: random.Random):
        self.indoor = indoor
        self.rng = rng
        self.b_co2 = 450.0
        self.b_pm = 30.0
        self.windows = {p: deque(maxlen=30) for p in RANGES}

    def _clamp(self, v: float, m: float) -> float:
        return max(-m, min(m, v))

    def sample(self, slot_epoch_wib: int) -> dict:
        r = self.rng
        h = (slot_epoch_wib % 86400) / 3600
        t_co2, t_pm = base_co2(h, self.indoor), base_pm25(h, self.indoor)
        self.b_co2 = max(0.0, self.b_co2 + self._clamp((t_co2 - self.b_co2) * 0.2 + r.uniform(-6, 6), 40))
        noise = 1.5 if not self.indoor else 0.6
        self.b_pm = max(0.0, self.b_pm + self._clamp((t_pm - self.b_pm) * 0.2 + r.uniform(-noise, noise), 5))
        out = {"co2": self.b_co2, "pm25": self.b_pm, "truth_co2": "NORMAL", "truth_pm25": "NORMAL"}
        for p, lo, span in (("co2", 5200, 800), ("pm25", 1050, 450)):
            if r.random() < 0.005:
                out[p], out[f"truth_{p}"] = lo + r.uniform(0, span), "SIM_RANGE"
            elif r.random() < 0.02:
                out[p], out[f"truth_{p}"] = out[p] * r.uniform(3, 5), "SIM_OUTLIER"
        temp = 27.5 + 3.5 * math.sin((h - 9) * math.pi / 12) + r.gauss(0, 0.08)
        rh = 70 - 12 * math.sin((h - 9) * math.pi / 12) + r.gauss(0, 0.4)
        if r.random() < 0.003:            # occasional SHT31 read failure
            temp = rh = None
        out["temp"], out["rh"] = temp, rh
        return out

    def flag(self, p: str, v: float | None) -> int:
        if v is None or math.isnan(v):
            return F_SENSOR
        lo, hi = RANGES[p]
        if v < lo or v > hi:
            return F_RANGE
        w = self.windows[p]
        f = F_CLEAN
        if len(w) >= 10:
            s = sorted(w)
            q1, q3 = _pct(s, 0.25), _pct(s, 0.75)
            iqr = max(q3 - q1, MIN_IQR[p], MIN_IQR_REL[p] * abs(_pct(s, 0.5)))
            if v < q1 - 1.5 * iqr or v > q3 + 1.5 * iqr:
                f = F_OUTLIER
        w.append(v)
        return f


def _pct(s: list[float], q: float) -> float:
    pos = (len(s) - 1) * q
    lo, hi = math.floor(pos), math.ceil(pos)
    return s[lo] + (s[hi] - s[lo]) * (pos - lo)


def rnd(v: float | None, d: int) -> float | None:
    return None if v is None else round(v, d)


def tor_payload(kind: str, group: str, created: str, vals: dict, pm_ok: bool, sht_ok: bool) -> dict:
    s = lambda v, d: "NULL" if v is None else f"{v:.{d}f}"
    return {"device_1": f"{kind}-{group}", "RSSI_1": "-60", "created_at_1": created, "Uptime_1": "00:00:00",
            "SD_status_1": "1", "PM25_status_1": "1" if pm_ok else "0", "SHT_status_1": "1" if sht_ok else "0",
            "CO2v_1": s(vals["co2"], 0), "PM25v_1": s(vals["pm25"], 1),
            "Tempinv_1": s(vals["temp"], 2), "Humidinv_1": s(vals["rh"], 2)}


def run(args: argparse.Namespace) -> None:
    rng = random.Random(args.random_seed)
    dev = Device(args.mode == "indoor", rng)
    client = httpx.Client(base_url=args.api, timeout=10)
    now = int(time.time())
    slot = now - now % 120 - args.seed_days * 86400 if args.seed_days else now - now % 120
    end_seed = now - now % 120
    sent = 0

    def emit(slot_utc: int) -> None:
        nonlocal sent
        s = dev.sample(slot_utc + WIB)
        flags = [dev.flag(p, s[p]) for p in ("co2", "pm25", "temp", "rh")]
        body = {"device_id": args.device, "ts": slot_utc, "co2": rnd(s["co2"], 0), "pm25": rnd(s["pm25"], 1),
                "temp": rnd(s["temp"], 2), "rh": rnd(s["rh"], 2), "flags": flags}
        client.post("/api/readings", json=body).raise_for_status()
        if args.tor:
            created = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(slot_utc + WIB))
            client.post(f"/api/post/{args.group}", json=tor_payload(
                "2m_raw", args.group, created, body, body["pm25"] is not None, body["temp"] is not None))
            v = {p: (body[p] if flags[i] == 0 else None) for i, p in enumerate(("co2", "pm25", "temp", "rh"))}
            client.post(f"/api/post/{args.group}", json=tor_payload(
                "2m_validated", args.group, created, v, flags[1] == 0, flags[2] == 0 and flags[3] == 0))
        sent += 1

    if args.seed_days:
        print(f"[SIM] seeding {args.seed_days} hari mode={args.mode} device={args.device}")
        while slot <= end_seed:
            emit(slot)
            slot += 120
        print(f"[SIM] {sent} record terkirim")
    if args.live:
        slot = max(slot, end_seed + 120)
        print(f"[SIM] live: 1 slot tiap {args.interval}s (Ctrl+C untuk berhenti)")
        try:
            while True:
                if args.interval >= 120:
                    wait = slot - time.time()
                    if wait > 0:
                        time.sleep(wait)
                emit(slot)
                print(f"[SIM] slot {time.strftime('%Y-%m-%d %H:%M', time.gmtime(slot + WIB))} WIB terkirim")
                slot += 120
                if args.interval < 120:
                    time.sleep(args.interval)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--api", default="http://localhost:8000")
    ap.add_argument("--device", default="aqms-01")
    ap.add_argument("--group", default="Kelompok_C")
    ap.add_argument("--mode", choices=["outdoor", "indoor"], default="outdoor")
    ap.add_argument("--seed-days", type=int, default=0)
    ap.add_argument("--live", action="store_true")
    ap.add_argument("--interval", type=float, default=120)
    ap.add_argument("--tor", action="store_true", help="juga kirim payload TOR ke mirror lokal")
    ap.add_argument("--random-seed", type=int, default=None)
    run(ap.parse_args())
