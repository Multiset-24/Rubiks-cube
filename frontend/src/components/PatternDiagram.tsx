// Top-face diagrams for the yellow-cross patterns (pattern recognition aid).
type Pattern = 'dot' | 'L' | 'line' | 'cross';

const YELLOW = '#f3c623';
const GREY = '#4a4f57';

const CELLS: Record<Pattern, number[]> = {
  dot: [4],
  L: [1, 3, 4],
  line: [3, 4, 5],
  cross: [1, 3, 4, 5, 7],
};

export function PatternDiagram({ pattern, size = 72 }: { pattern: Pattern; size?: number }) {
  const on = new Set(CELLS[pattern]);
  const cell = size / 3;
  return (
    <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`} role="img" aria-label={`${pattern} pattern`}>
      {Array.from({ length: 9 }, (_, i) => (
        <rect
          key={i}
          x={(i % 3) * cell + 2}
          y={Math.floor(i / 3) * cell + 2}
          width={cell - 4}
          height={cell - 4}
          rx={5}
          fill={on.has(i) || i === 4 ? YELLOW : GREY}
          opacity={on.has(i) ? 1 : 0.5}
        />
      ))}
    </svg>
  );
}
