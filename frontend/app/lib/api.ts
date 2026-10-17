export const API_URL = (process.env.NEXT_PUBLIC_API_URL ?? "http://localhost:8000").replace(/\/$/, "");
export const DEFAULT_DEVICE = process.env.NEXT_PUBLIC_DEVICE_ID ?? "aqms-01";
export const POLL_MS = Number(process.env.NEXT_PUBLIC_POLL_MS ?? 120_000);

export type Param = "co2" | "pm25" | "temp" | "rh";
export const PARAMS: Param[] = ["co2", "pm25", "temp", "rh"];

export interface Reading {
  device_id: string;
  ts: number;
  time: string;
  co2: number | null;
  pm25: number | null;
  temp: number | null;
  rh: number | null;
  flags: [number, number, number, number];
  age_s?: number;
}

export type HourlyRow = { ts: number; time: string } & Record<Param, number | null> &
  Record<`count_${Param}`, number>;

export interface ForecastPoint { ts: number; time: string; value: number; lower: number; upper: number }

export interface ParamForecast {
  status: "ok" | "insufficient_data";
  model?: string;
  features?: string[];
  hours_trained?: number;
  hours_available?: number;
  hours_required?: number;
  points: ForecastPoint[];
  metrics: { mae: number; mae_persistence: number; skill: number | null; backtest_hours: number } | null;
}

export interface ForecastResponse {
  device_id: string;
  horizon: number;
  generated_at: string;
  history: ({ ts: number; time: string } & Partial<Record<Param, number | null>>)[];
  forecast: Partial<Record<Param, ParamForecast>>;
}

export interface Summary {
  device_id: string;
  date: string;
  records: number;
  expected_so_far: number;
  expected_day: number;
  flags: Record<Param, Record<"F_CLEAN" | "F_SENSOR" | "F_RANGE" | "F_OUTLIER", number>>;
}

export interface Device { device_id: string; records: number; last_ts: number; last_time: string }

export interface TorCount { device_1: string; date: string; count: number; target: number | null }

async function get<T>(path: string, params: Record<string, string | number> = {}): Promise<T> {
  const qs = new URLSearchParams(Object.entries(params).map(([k, v]) => [k, String(v)]));
  const res = await fetch(`${API_URL}${path}${qs.size ? `?${qs}` : ""}`, { cache: "no-store" });
  if (!res.ok) throw new Error(`${path} → HTTP ${res.status}`);
  return res.json() as Promise<T>;
}

export const api = {
  devices: () => get<Device[]>("/api/devices"),
  latest: (device_id: string) => get<Reading | Record<string, never>>("/api/readings/latest", { device_id }),
  hourly: (device_id: string, hours = 24) => get<HourlyRow[]>("/api/readings/hourly", { device_id, hours }),
  recent: (device_id: string, hours = 24) => get<Reading[]>("/api/readings/recent", { device_id, hours }),
  summary: (device_id: string) => get<Summary>("/api/summary", { device_id }),
  forecast: (device_id: string, horizon = 6) =>
    get<ForecastResponse>("/api/forecast", { device_id, horizon, param: "all" }),
  tor: () => get<TorCount[]>("/api/tor/summary"),
};
