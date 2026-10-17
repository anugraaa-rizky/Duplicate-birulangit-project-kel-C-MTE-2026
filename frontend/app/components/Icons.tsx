import type { Level } from "../lib/aq";

export function WarnIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 24 24" aria-hidden className="icon">
      <path d="M12 3 2 21h20L12 3Z" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinejoin="round" />
      <path d="M12 10v5M12 18h.01" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" />
    </svg>
  );
}

const GLYPH: Record<Level, string> = {
  good: "M5 12.5l4.2 4.2L19 7",
  warning: "M12 7v6M12 17h.01",
  serious: "M12 7v6M12 17h.01",
  critical: "M7 7l10 10M17 7 7 17",
};

/** Status selalu tampil sebagai ikon + label, tidak hanya warna. */
export function StatusChip({ level, label }: { level: Level; label: string }) {
  return (
    <span className={`status status-${level}`}>
      <svg width="12" height="12" viewBox="0 0 24 24" aria-hidden>
        <path d={GLYPH[level]} fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
      {label}
    </span>
  );
}

export function CloudLogo() {
  return (
    <svg width="34" height="34" viewBox="0 0 48 48" aria-hidden>
      <circle cx="24" cy="24" r="22" className="logo-disc" />
      <path className="logo-cloud" d="M15 31h18a6 6 0 0 0 0-12 8 8 0 0 0-15.2 2.4A5 5 0 0 0 15 31Z" />
    </svg>
  );
}
