import math
import os
import tempfile

import pytest
from fastapi.testclient import TestClient

os.environ["AQ_DB_PATH"] = os.path.join(tempfile.mkdtemp(), "test.db")

from app import db  # noqa: E402
from app.main import app  # noqa: E402

BASE = 1791010800  # Sabtu 03-10-2026 14:00 WIB (contoh slide)


@pytest.fixture()
def client():
    with TestClient(app) as c:
        db.connect(os.environ["AQ_DB_PATH"])
        db.write("DELETE FROM readings")
        db.write("DELETE FROM tor_packets")
        yield c


def reading(ts, pm25=23.5, flags=(0, 0, 0, 0), device="aqms-01"):
    return {"device_id": device, "ts": ts, "co2": 612.0, "pm25": pm25, "temp": 27.8, "rh": 64.2,
            "flags": list(flags)}


def test_post_is_idempotent(client):
    r = client.post("/api/readings", json=reading(BASE))
    assert r.status_code == 201 and r.json()["duplicate"] is False
    r = client.post("/api/readings", json=reading(BASE))
    assert r.status_code == 201 and r.json()["duplicate"] is True
    assert client.get("/api/health").json()["records"] == 1


def test_bad_payload_is_422(client):
    bad = reading(BASE)
    bad["flags"] = [0, 0, 0]
    assert client.post("/api/readings", json=bad).status_code == 422


def test_null_values_accepted_and_latest(client):
    client.post("/api/readings", json=reading(BASE, pm25=None, flags=(0, 1, 0, 0)))
    client.post("/api/readings", json=reading(BASE + 120, pm25=30.0))
    d = client.get("/api/readings/latest", params={"device_id": "aqms-01"}).json()
    assert d["ts"] == BASE + 120 and d["pm25"] == 30.0 and d["flags"] == [0, 0, 0, 0]
    assert d["time"] == "2026-10-03 14:02:00"


def test_hourly_uses_clean_data_and_completeness(client):
    # Jam 14: 30 slot, 2 di antaranya outlier (flag 3) bernilai 999 -> diabaikan.
    for i in range(30):
        bad = i in (5, 6)
        client.post("/api/readings", json=reading(BASE + i * 120, pm25=999.0 if bad else 20.0 + i % 2,
                                                  flags=(0, 3 if bad else 0, 0, 0)))
    # Jam 15: hanya 10 slot -> PM2.5 tidak lengkap (null).
    for i in range(10):
        client.post("/api/readings", json=reading(BASE + 3600 + i * 120))
    rows = client.get("/api/readings/hourly", params={"device_id": "aqms-01", "hours": 2}).json()
    assert [r["ts"] for r in rows] == [BASE, BASE + 3600]
    clean = [20.0 + i % 2 for i in range(30) if i not in (5, 6)]
    assert math.isclose(rows[0]["pm25"], sum(clean) / len(clean), abs_tol=1e-3)
    assert rows[0]["count_pm25"] == 28
    assert rows[1]["pm25"] is None and rows[1]["count_pm25"] == 10


def test_forecast_learns_daily_pattern(client):
    # 4 hari pola sinus harian -> model harus mengalahkan persistence.
    for slot in range(4 * 720):
        ts = BASE + slot * 120
        h = ((ts + 7 * 3600) % 86400) / 3600
        client.post("/api/readings", json=reading(ts, pm25=round(40 + 25 * math.sin(h * math.pi / 12), 1)))
    f = client.get("/api/forecast", params={"param": "pm25", "horizon": 6}).json()["forecast"]["pm25"]
    assert f["status"] == "ok" and len(f["points"]) == 6
    assert f["model"] == "ridge-ar+daily"
    assert f["metrics"]["mae"] < f["metrics"]["mae_persistence"]


def test_forecast_insufficient(client):
    client.post("/api/readings", json=reading(BASE))
    f = client.get("/api/forecast", params={"param": "co2"}).json()["forecast"]["co2"]
    assert f["status"] == "insufficient_data"


def test_tor_mirror(client):
    assert len(client.get("/api/get-time").json()["datetime"]) == 19
    body = {"device_1": "2m_raw-Kelompok_C", "RSSI_1": "-61", "created_at_1": "2026-10-03 14:36:00",
            "Uptime_1": "02:14:07", "SD_status_1": "1", "PM25_status_1": "1", "SHT_status_1": "1",
            "CO2v_1": "612", "PM25v_1": "NULL", "Tempinv_1": "27.81", "Humidinv_1": "71.20"}
    assert client.post("/api/post/telkom_group", json=body).status_code == 201
    assert client.post("/api/post/telkom_group", json=body).json()["duplicate"] is True
    assert client.post("/api/post/telkom_group", json={**body, "RSSI_1": -61}).status_code == 422
    s = client.get("/api/tor/summary").json()
    assert s == [{"device_1": "2m_raw-Kelompok_C", "date": "2026-10-03", "count": 1, "target": 720}]
