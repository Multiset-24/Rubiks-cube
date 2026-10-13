import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { Breath } from '../components/Breath';
import { ActivityStrip, TimesChart } from '../components/TimesChart';
import { api, type RunKind, type RunRecord, type Stats } from '../lib/api';
import { useAuth } from '../lib/auth';
import { formatDate, formatDuration, formatMs } from '../lib/format';
import { deleteLocalRun, localRuns, type LocalRun } from '../lib/runs';

const KIND_LABEL: Record<RunKind, string> = { tutor: 'Tutor', fast: 'Fast solve', optimal: 'Optimal', timer: 'Timer' };

export default function History() {
  const { token, user } = useAuth();
  return token && user ? <AccountHistory token={token} /> : <GuestHistory />;
}

function AccountHistory({ token }: { token: string }) {
  const [stats, setStats] = useState<Stats | null>(null);
  const [runs, setRuns] = useState<RunRecord[]>([]);
  const [cursor, setCursor] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const loadPage = useCallback(async (after: string | null) => {
    const page = await api.listRuns(token, after, 20);
    setRuns((r) => (after ? [...r, ...page.runs] : page.runs));
    setCursor(page.nextCursor);
  }, [token]);

  useEffect(() => {
    setLoading(true);
    Promise.all([api.stats(token).then(setStats), loadPage(null)])
      .catch((e) => setError(e.message))
      .finally(() => setLoading(false));
  }, [token, loadPage]);

  const remove = async (id: number) => {
    await api.deleteRun(token, id);
    setRuns((r) => r.filter((x) => x.id !== id));
    api.stats(token).then(setStats).catch(() => undefined);
  };

  if (loading) return <Breath label="Gathering your progress…" />;
  if (error) return <div className="empty card"><p>{error}</p></div>;

  const tutor = stats?.byKind.tutor;
  return (
    <div className="history">
      <header className="page-head">
        <h1>Your progress</h1>
        <p className="muted">Every tutor session and timed solve, kept in one place.</p>
      </header>

      {stats && (
        <>
          <div className="stats-row">
            <div className="tile"><span>Solves completed</span><strong>{stats.completedRuns}</strong></div>
            <div className="tile"><span>Tutor sessions</span><strong>{tutor?.completed ?? 0}</strong></div>
            <div className="tile"><span>Best single</span><strong>{formatMs(stats.timer.bestMs)}</strong></div>
            <div className="tile"><span>Ao5 · Ao12</span><strong>{formatMs(stats.timer.ao5Ms)} · {formatMs(stats.timer.ao12Ms)}</strong></div>
            <div className="tile"><span>Streak</span><strong>{stats.streak.current} days</strong><em>best {stats.streak.longest}</em></div>
            <div className="tile">
              <span>Patterns read</span>
              <strong>{stats.quiz.accuracy === null ? '—' : `${Math.round(stats.quiz.accuracy * 100)}%`}</strong>
              <em>{stats.quiz.correct}/{stats.quiz.total}</em>
            </div>
          </div>
          <div className="history__charts">
            <div className="card">
              <h3>Recent solve times</h3>
              <TimesChart times={stats.timer.recentMs} />
            </div>
            <div className="card">
              <h3>Last 30 days</h3>
              <ActivityStrip days={stats.activeDays} />
              <p className="muted small">{stats.movesFollowed.toLocaleString()} moves followed with the tutor so far.</p>
            </div>
          </div>
        </>
      )}

      <div className="card">
        <h3>History</h3>
        {runs.length === 0 ? (
          <p className="muted">Nothing yet. <Link to="/scan" className="link">Solve a cube</Link> or <Link to="/timer" className="link">time one</Link>.</p>
        ) : (
          <RunList
            items={runs.map((r) => ({ key: String(r.id), run: r, createdAt: r.createdAt, onDelete: () => remove(r.id) }))}
          />
        )}
        {cursor && (
          <button type="button" className="btn btn--ghost" onClick={() => void loadPage(cursor)}>Load more</button>
        )}
      </div>
    </div>
  );
}

function GuestHistory() {
  const [runs, setRuns] = useState<LocalRun[]>(() => localRuns().reverse());
  return (
    <div className="history">
      <header className="page-head">
        <h1>Your progress</h1>
        <p className="muted">You’re not signed in, so runs are kept on this device only.</p>
      </header>
      <div className="card banner">
        <p>Create a free account to keep your history, averages and streaks across devices. Runs from this device come with you.</p>
        <Link to="/account" className="btn btn--primary">Sign in or register</Link>
      </div>
      <div className="card">
        <h3>On this device</h3>
        {runs.length === 0 ? (
          <p className="muted">Nothing yet. <Link to="/scan" className="link">Solve a cube</Link> to begin.</p>
        ) : (
          <RunList
            items={runs.map((r) => ({
              key: r.clientId,
              run: r,
              createdAt: new Date(r.createdAtMs).toISOString(),
              onDelete: () => {
                deleteLocalRun(r.clientId);
                setRuns((x) => x.filter((y) => y.clientId !== r.clientId));
              },
            }))}
          />
        )}
      </div>
    </div>
  );
}

interface Item {
  key: string;
  run: Partial<RunRecord> & { kind: RunKind };
  createdAt: string;
  onDelete: () => void;
}

function RunList({ items }: { items: Item[] }) {
  return (
    <ul className="runs">
      {items.map(({ key, run, createdAt, onDelete }) => (
        <li key={key} className="run">
          <span className={`badge badge--${run.kind}`}>{KIND_LABEL[run.kind]}</span>
          <span className="run__main">
            {run.kind === 'timer' ? (
              <strong>{formatMs(run.durationMs ?? 0)}</strong>
            ) : (
              <strong>{run.completed ? 'Solved' : 'Unfinished'} · {run.moveCount ?? 0} moves</strong>
            )}
            <span className="muted small">
              {formatDate(createdAt)}
              {run.kind !== 'timer' && run.durationMs ? ` · ${formatDuration(run.durationMs)}` : ''}
              {run.quizTotal ? ` · patterns ${run.quizCorrect}/${run.quizTotal}` : ''}
            </span>
          </span>
          <button type="button" className="link small" onClick={onDelete} aria-label="Delete this run">Delete</button>
        </li>
      ))}
    </ul>
  );
}
