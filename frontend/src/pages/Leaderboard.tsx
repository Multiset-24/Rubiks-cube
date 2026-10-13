import { useEffect, useState } from 'react';
import { Breath } from '../components/Breath';
import { api, type LeaderboardEntry } from '../lib/api';
import { useAuth } from '../lib/auth';
import { formatMs } from '../lib/format';

export default function Leaderboard() {
  const { user } = useAuth();
  const [entries, setEntries] = useState<LeaderboardEntry[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    api.leaderboard(25).then((r) => setEntries(r.entries), (e) => setError(e.message));
  }, []);

  return (
    <div className="leaderboard">
      <header className="page-head">
        <h1>Leaderboard</h1>
        <p className="muted">Best single timer solve per person. Times are self-reported.</p>
      </header>
      {error && <div className="empty card"><p>{error}</p></div>}
      {!entries && !error && <Breath />}
      {entries && (
        <div className="card">
          {entries.length === 0 ? (
            <p className="muted">No times yet. Be the first.</p>
          ) : (
            <table className="table">
              <thead>
                <tr><th>#</th><th>Name</th><th className="num">Best</th><th className="num">Solves</th></tr>
              </thead>
              <tbody>
                {entries.map((e) => (
                  <tr key={e.rank} className={user?.displayName === e.displayName ? 'is-me' : ''}>
                    <td>{e.rank}</td>
                    <td>{e.displayName}</td>
                    <td className="num">{formatMs(e.bestMs)}</td>
                    <td className="num">{e.solves}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      )}
    </div>
  );
}
