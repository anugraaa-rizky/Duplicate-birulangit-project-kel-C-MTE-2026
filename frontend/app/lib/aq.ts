import type { Param } from "./api";

export const META: Record<Param, { label: string; short: string; unit: string; decimals: number; flagIndex: number }> = {
  co2: { label: "Karbon dioksida", short: "CO₂", unit: "ppm", decimals: 0, flagIndex: 0 },
  pm25: { label: "Partikulat halus", short: "PM2.5", unit: "µg/m³", decimals: 1, flagIndex: 1 },
  temp: { label: "Suhu udara", short: "Suhu", unit: "°C", decimals: 2, flagIndex: 2 },
  rh: { label: "Kelembapan", short: "RH", unit: "%RH", decimals: 2, flagIndex: 3 },
};

export const FLAG_LABEL: Record<number, string> = {
  0: "Bersih",
  1: "F_SENSOR · gagal baca",
  2: "F_RANGE · di luar rentang",
  3: "F_OUTLIER · pencilan IQR",
};

export type Level = "good" | "warning" | "serious" | "critical";
export interface Status { level: Level; label: string }

/** Kategori ringkas. PM2.5 mengikuti ambang ISPU (Permen LHK 14/2020); CO₂ acuan ventilasi ruang. */
export function statusOf(param: Param, v: number | null): Status | null {
  if (v === null || Number.isNaN(v)) return null;
  switch (param) {
    case "pm25":
      if (v <= 15.5) return { level: "good", label: "Baik" };
      if (v <= 55.4) return { level: "warning", label: "Sedang" };
      if (v <= 150.4) return { level: "serious", label: "Tidak sehat" };
      return { level: "critical", label: v <= 250.4 ? "Sangat tidak sehat" : "Berbahaya" };
    case "co2":
      if (v < 800) return { level: "good", label: "Segar" };
      if (v < 1000) return { level: "warning", label: "Cukup" };
      if (v < 1500) return { level: "serious", label: "Kurang ventilasi" };
      return { level: "critical", label: "Pengap" };
    case "temp":
      return v >= 20 && v <= 28 ? { level: "good", label: "Nyaman" } : { level: "warning", label: v < 20 ? "Sejuk" : "Panas" };
    case "rh":
      return v >= 40 && v <= 70 ? { level: "good", label: "Nyaman" } : { level: "warning", label: v < 40 ? "Kering" : "Lembap" };
  }
}

export function fmt(v: number | null | undefined, decimals: number): string {
  return v === null || v === undefined || Number.isNaN(v) ? "—" : v.toFixed(decimals);
}

export function hhmm(time: string): string {
  return time.slice(11, 16);
}

/** Epoch UTC → "HH:MM" WIB. */
export function wibHHMM(ts: number): string {
  return new Date((ts + 7 * 3600) * 1000).toISOString().slice(11, 16);
}

/** Epoch UTC → "DD/MM HH:MM" WIB. */
export function wibShort(ts: number): string {
  const iso = new Date((ts + 7 * 3600) * 1000).toISOString();
  return `${iso.slice(8, 10)}/${iso.slice(5, 7)} ${iso.slice(11, 16)}`;
}

export const COLORS = {
  s1: "var(--series-1)",
  s2: "var(--series-2)",
  s3: "var(--series-3)",
  raw: "var(--series-raw)",
  critical: "var(--status-critical)",
};
