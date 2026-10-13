import { FACES } from '../lib/cube/cube';

// The six faces unfolded:      U
//                            L F R B
//                              D
const LAYOUT: Record<string, [number, number]> = { U: [0, 3], L: [3, 0], F: [3, 3], R: [3, 6], B: [3, 9], D: [6, 3] };

interface Props {
  facelets: string;
  colors: Record<string, string>;
  onPaint?: (index: number) => void;
  flagged?: number[];
  lowConfidence?: number[];
  compact?: boolean;
  names?: Record<string, string>; // facelet letter -> colour name, for screen readers
}

export function CubeNet({ facelets, colors, onPaint, flagged = [], lowConfidence = [], compact, names }: Props) {
  const flaggedSet = new Set(flagged);
  const lowSet = new Set(lowConfidence);
  return (
    <div className={`net ${compact ? 'net--compact' : ''}`} role="group" aria-label="Cube net">
      {FACES.map((face, f) =>
        Array.from({ length: 9 }, (_, i) => {
          const idx = f * 9 + i;
          const [r0, c0] = LAYOUT[face];
          const isCentre = i === 4;
          const letter = facelets[idx];
          const classes = ['net__sticker'];
          const notes = [letter === '?' ? 'not set' : names?.[letter] ?? letter];
          if (flaggedSet.has(idx)) {
            classes.push('net__sticker--flagged');
            notes.push('has a problem');
          }
          if (lowSet.has(idx)) {
            classes.push('net__sticker--unsure');
            notes.push('unsure');
          }
          if (isCentre) classes.push('net__sticker--centre');
          return (
            <button
              key={idx}
              type="button"
              className={classes.join(' ')}
              style={{
                gridRow: r0 + Math.floor(i / 3) + 1,
                gridColumn: c0 + (i % 3) + 1,
                background: colors[letter] ?? 'var(--unknown)',
              }}
              disabled={!onPaint || isCentre}
              onClick={() => onPaint?.(idx)}
              aria-label={`${face} face sticker ${i + 1}${isCentre ? ' (centre)' : ''}: ${notes.join(', ')}`}
            />
          );
        }),
      )}
    </div>
  );
}
