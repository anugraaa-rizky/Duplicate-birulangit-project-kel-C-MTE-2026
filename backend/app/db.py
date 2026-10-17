"""SQLite storage. Raw values and flags are kept as sent; nothing is discarded."""
import json
import os
import sqlite3
import threading

DB_PATH = os.environ.get("AQ_DB_PATH", os.path.join(os.path.dirname(os.path.dirname(__file__)), "aq.db"))

SCHEMA = """
CREATE TABLE IF NOT EXISTS readings (
    device_id   TEXT    NOT NULL,
    ts          INTEGER NOT NULL,          -- epoch UTC, start of 2-minute slot
    co2         REAL,
    pm25        REAL,
    temp        REAL,
    rh          REAL,
    flags       TEXT    NOT NULL,          -- JSON [co2, pm25, temp, rh]
    received_at INTEGER NOT NULL,
    PRIMARY KEY (device_id, ts)
);
CREATE TABLE IF NOT EXISTS tor_packets (
    device_1     TEXT    NOT NULL,         -- e.g. 2m_raw-Kelompok_C
    created_at   TEXT    NOT NULL,         -- WIB, YYYY-MM-DD HH:MM:SS
    payload      TEXT    NOT NULL,
    received_at  INTEGER NOT NULL,
    PRIMARY KEY (device_1, created_at)
);
"""

_lock = threading.Lock()
_conn: sqlite3.Connection | None = None


def connect(path: str | None = None) -> sqlite3.Connection:
    """Open (or reopen) the shared connection and make sure the tables exist."""
    global _conn
    with _lock:
        if _conn is not None:
            _conn.close()
        _conn = sqlite3.connect(path or DB_PATH, check_same_thread=False)
        _conn.row_factory = sqlite3.Row
        _conn.execute("PRAGMA journal_mode=WAL")
        _conn.executescript(SCHEMA)
        _conn.commit()
    return _conn


def conn() -> sqlite3.Connection:
    return _conn or connect()


def write(sql: str, params: tuple = ()) -> int:
    """Run one write under the lock; returns the number of rows changed."""
    with _lock:
        cur = conn().execute(sql, params)
        conn().commit()
        return cur.rowcount


def query(sql: str, params: tuple = ()) -> list[sqlite3.Row]:
    with _lock:
        return conn().execute(sql, params).fetchall()


def row_to_reading(row: sqlite3.Row) -> dict:
    d = dict(row)
    d["flags"] = json.loads(d["flags"])
    return d
