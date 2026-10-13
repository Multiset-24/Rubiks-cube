export function formatMs(ms: number | null | undefined): string {
  if (ms === null || ms === undefined) return '—';
  const totalCs = Math.floor(ms / 10);
  const cs = totalCs % 100;
  const s = Math.floor(totalCs / 100) % 60;
  const m = Math.floor(totalCs / 6000);
  const pad = (n: number) => n.toString().padStart(2, '0');
  return m > 0 ? `${m}:${pad(s)}.${pad(cs)}` : `${s}.${pad(cs)}`;
}

export function formatDuration(ms: number): string {
  const s = Math.round(ms / 1000);
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m ${s % 60}s`;
  return `${Math.floor(m / 60)}h ${m % 60}m`;
}

export function formatDate(iso: string): string {
  const d = new Date(iso);
  return d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' }) +
    ' · ' + d.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' });
}

// WCA average of n: drop the best and worst, mean of the rest.
export function averageOf(times: number[], n: number): number | null {
  if (times.length < n) return null;
  const t = times.slice(0, n).sort((a, b) => a - b);
  const mid = t.slice(1, -1);
  return mid.reduce((a, b) => a + b, 0) / mid.length;
}
