"""Hourly forecasting with an autoregressive ridge-regression model (numpy only).

For each parameter the model learns y[t] from:
  * lags y[t-1], y[t-2], y[t-3]  (short-term persistence)
  * lag y[t-24] when 2+ days of history exist (daily pattern)
  * sin/cos of hour-of-day in WIB (diurnal shape, e.g. PM2.5 rush hours)
It forecasts recursively, feeding each prediction back as the next lag.
A rolling one-step backtest on the last hours reports MAE against a
persistence baseline, so the dashboard can show whether the model earns its keep.
"""
from __future__ import annotations

import math

import numpy as np

WIB_OFFSET = 7 * 3600
MIN_HOURS = 12          # below this, no model is trained
DAILY_LAG_HOURS = 48    # history needed before lag-24 is used
BACKTEST_HOURS = 12
ALPHA = 1.0


def _fill_gaps(values: list[float | None]) -> np.ndarray:
    """Linear interpolation over missing hours; edges take the nearest known value."""
    y = np.array([np.nan if v is None else float(v) for v in values], dtype=float)
    known = ~np.isnan(y)
    if known.sum() == 0:
        return y
    idx = np.arange(len(y))
    y[~known] = np.interp(idx[~known], idx[known], y[known])
    return y


def _hour_features(ts_hour: int) -> list[float]:
    h = ((ts_hour + WIB_OFFSET) // 3600) % 24
    a = 2 * math.pi * h / 24
    return [math.sin(a), math.cos(a)]


class RidgeAR:
    def __init__(self, use_daily: bool, alpha: float = ALPHA):
        self.use_daily = use_daily
        self.alpha = alpha
        self.lags = [1, 2, 3] + ([24] if use_daily else [])

    def _row(self, y: np.ndarray, t: int, ts_hour: int) -> list[float]:
        return [y[t - k] for k in self.lags] + _hour_features(ts_hour)

    def fit(self, y: np.ndarray, ts: list[int]) -> "RidgeAR":
        start = max(self.lags)
        X = np.array([self._row(y, t, ts[t]) for t in range(start, len(y))])
        target = y[start:]
        self.mu = X.mean(axis=0)
        self.sd = X.std(axis=0)
        self.sd[self.sd == 0] = 1.0
        Xs = np.hstack([np.ones((len(X), 1)), (X - self.mu) / self.sd])
        reg = self.alpha * np.eye(Xs.shape[1])
        reg[0, 0] = 0.0  # do not shrink the intercept
        self.w = np.linalg.solve(Xs.T @ Xs + reg, Xs.T @ target)
        resid = target - Xs @ self.w
        self.resid_sd = float(np.std(resid)) if len(resid) > 1 else 0.0
        return self

    def predict_one(self, y: np.ndarray, t: int, ts_hour: int) -> float:
        x = (np.array(self._row(y, t, ts_hour)) - self.mu) / self.sd
        return float(self.w[0] + x @ self.w[1:])


def forecast_series(ts: list[int], values: list[float | None], horizon: int,
                    lower_bound: float | None = 0.0) -> dict:
    """Forecast `horizon` hours after the last timestamp in `ts` (hourly, epoch UTC)."""
    known = sum(v is not None for v in values)
    if known < MIN_HOURS:
        return {"status": "insufficient_data", "hours_available": known,
                "hours_required": MIN_HOURS, "points": [], "metrics": None}

    y = _fill_gaps(values)
    use_daily = len(y) >= DAILY_LAG_HOURS
    model = RidgeAR(use_daily).fit(y, ts)

    # Rolling one-step backtest on the last BACKTEST_HOURS, model refit without them.
    metrics = None
    start = max(model.lags) + 6
    bt = min(BACKTEST_HOURS, len(y) - start)
    if bt >= 3:
        train_end = len(y) - bt
        m = RidgeAR(use_daily).fit(y[:train_end], ts[:train_end])
        pred = np.array([m.predict_one(y, t, ts[t]) for t in range(train_end, len(y))])
        actual = y[train_end:]
        naive = y[train_end - 1:len(y) - 1]
        mae = float(np.mean(np.abs(pred - actual)))
        mae_naive = float(np.mean(np.abs(naive - actual)))
        metrics = {
            "mae": round(mae, 3),
            "mae_persistence": round(mae_naive, 3),
            "skill": round(1 - mae / mae_naive, 3) if mae_naive > 0 else None,
            "backtest_hours": int(bt),
        }

    ext = list(y)
    ts_ext = list(ts)
    points = []
    for step in range(1, horizon + 1):
        t_next = ts_ext[-1] + 3600
        ext_arr = np.array(ext + [0.0])
        yhat = model.predict_one(ext_arr, len(ext), t_next)
        if lower_bound is not None:
            yhat = max(lower_bound, yhat)
        band = 1.96 * model.resid_sd * math.sqrt(step)
        lo = yhat - band if lower_bound is None else max(lower_bound, yhat - band)
        points.append({"ts": t_next, "value": round(yhat, 3),
                       "lower": round(lo, 3), "upper": round(yhat + band, 3)})
        ext.append(yhat)
        ts_ext.append(t_next)

    return {
        "status": "ok",
        "model": "ridge-ar" + ("+daily" if use_daily else ""),
        "features": [f"lag{k}" for k in model.lags] + ["hour_sin", "hour_cos"],
        "hours_trained": int(len(y)),
        "points": points,
        "metrics": metrics,
    }
