import type { Stage } from '../lib/api';

interface Props {
  stages: Stage[];
  current: number;
  onJump?: (stageIndex: number) => void;
}

export function StageTimeline({ stages, current, onJump }: Props) {
  return (
    <nav className="timeline" aria-label="Stages">
      {stages.map((s, i) => (
        <button
          key={s.id + i}
          type="button"
          className={`timeline__item ${i < current ? 'is-done' : ''} ${i === current ? 'is-current' : ''} ${s.steps.length === 0 ? 'is-skipped' : ''}`}
          onClick={() => onJump?.(i)}
          title={s.name}
        >
          <span className="timeline__dot" />
          <span className="timeline__name">{s.name}</span>
        </button>
      ))}
    </nav>
  );
}
