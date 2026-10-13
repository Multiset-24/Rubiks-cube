import { describeMove, parseMoves } from '../lib/cube/cube';

interface Props {
  moves: string[];
  active?: number; // index currently animating
  done?: number; // moves before this index are done
  onPick?: (index: number) => void;
}

export function MoveChips({ moves, active = -1, done = 0, onPick }: Props) {
  if (!moves.length) return null;
  return (
    <ol className="chips" aria-label="Moves">
      {moves.map((m, i) => {
        const parsed = parseMoves(m)[0];
        const cls = ['chip', m.match(/^[xyz]/) ? 'chip--rotation' : '', i === active ? 'chip--active' : '', i < done ? 'chip--done' : '']
          .filter(Boolean)
          .join(' ');
        return (
          <li key={i}>
            <button type="button" className={cls} title={parsed ? describeMove(parsed) : m} onClick={() => onPick?.(i)}>
              {m}
            </button>
          </li>
        );
      })}
    </ol>
  );
}
