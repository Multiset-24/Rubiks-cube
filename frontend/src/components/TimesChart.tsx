import { useState } from 'react';
import { formatMs } from '../lib/format';

// Single-series line of recent timer solves (oldest -> newest). One series, so
// no legend: the card title names it. Hovering a point shows its value.
export function TimesChart({ times }: { times: number[] }) {
  const [hover, setHover] = useState<number | null>(null);
  if (times.length < 2) return <p className="muted small">Time a few solves to see your trend.</p>;
  const data = [...times].reverse();
  const w = 320, h = 120, pad = { l: 44, r: 12, t: 10, b: 18 };
  const lo = Math.min(...data), hi = Math.max(...data);
  const span = hi - lo || 1;
  const x = (i: number) => pad.l + (i / (data.length - 1)) * (w - pad.l - pad.r);
  const y = (v: number) => pad.t + (1 - (v - lo) / span) * (h - pad.t - pad.b);
  const path = data.map((v, i) => `${i ? 'L' : 'M'}${x(i).toFixed(1)},${y(v).toFixed(1)}`).join(' ');
  return (
    <figure className="chart">
      <svg
        viewBox={`0 0 ${w} ${h}`}
        role="img"
        aria-label={`Recent solve times from ${formatMs(data[0])} to ${formatMs(data[data.length - 1])}`}
        onMouseLeave={() => setHover(null)}
        onMouseMove={(e) => {
          const r = (e.currentTarget as SVGSVGElement).getBoundingClientRect();
          const px = ((e.clientX - r.left) / r.width) * w;
          const i = Math.round(((px - pad.l) / (w - pad.l - pad.r)) * (data.length - 1));
          setHover(Math.max(0, Math.min(data.length - 1, i)));
        }}
      >
        <line className="chart__grid" x1={pad.l} x2={w - pad.r} y1={y(hi)} y2={y(hi)} />
        <line className="chart__grid" x1={pad.l} x2={w - pad.r} y1={y(lo)} y2={y(lo)} />
        <text className="chart__axis" x={pad.l - 6} y={y(hi) + 4} textAnchor="end">{formatMs(hi)}</text>
        <text className="chart__axis" x={pad.l - 6} y={y(lo) + 4} textAnchor="end">{formatMs(lo)}</text>
        <path className="chart__line" d={path} />
        <circle className="chart__dot" cx={x(data.length - 1)} cy={y(data[data.length - 1])} r={4} />
        {hover !== null && (
          <g>
            <line className="chart__cross" x1={x(hover)} x2={x(hover)} y1={pad.t} y2={h - pad.b} />
            <circle className="chart__dot" cx={x(hover)} cy={y(data[hover])} r={5} />
            <text className="chart__tip" x={Math.min(x(hover) + 8, w - 60)} y={pad.t + 10}>{formatMs(data[hover])}</text>
          </g>
        )}
      </svg>
      <figcaption className="sr-only">
        Recent times, oldest first: {data.map(formatMs).join(', ')}
      </figcaption>
    </figure>
  );
}

// Last 30 days, one cell per day: filled if you practised.
export function ActivityStrip({ days }: { days: string[] }) {
  const set = new Set(days);
  const cells = Array.from({ length: 30 }, (_, i) => {
    const d = new Date(Date.now() - (29 - i) * 86400000);
    const key = d.toISOString().slice(0, 10);
    return { key, on: set.has(key), label: d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' }) };
  });
  return (
    <div className="activity" role="list" aria-label="Practice in the last 30 days">
      {cells.map((c) => (
        <span key={c.key} role="listitem" className={`activity__cell ${c.on ? 'is-on' : ''}`}
          title={`${c.label}: ${c.on ? 'practised' : 'no practice'}`} />
      ))}
    </div>
  );
}
