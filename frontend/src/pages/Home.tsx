import { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { api } from '../lib/api';
import { applyMoves, formatMoves, randomScramble, SOLVED } from '../lib/cube/cube';
import { DEFAULT_SCHEME } from '../lib/cube/colors';
import { useCubeSession } from '../lib/session';

export function Home() {
  const navigate = useNavigate();
  const { setCube } = useCubeSession();
  const [busy, setBusy] = useState(false);

  const tryScrambled = async () => {
    setBusy(true);
    try {
      // A random-state scramble from the server; falls back to a local one offline.
      const s = await api.scramble().catch(() => {
        const moves = randomScramble(22);
        return { facelets: applyMoves(SOLVED, moves), scramble: formatMoves(moves) };
      });
      setCube({ facelets: s.facelets, scheme: DEFAULT_SCHEME, source: 'scramble', scramble: s.scramble });
      navigate('/review');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="home">
      <section className="hero">
        <div className="hero__breath" aria-hidden />
        <p className="eyebrow">A calm Rubik’s Cube tutor</p>
        <h1>Solve your cube,<br />one quiet step at a time.</h1>
        <p className="lede">
          Show your cube to the camera. Zen Cube reads it, then teaches you the beginner method: what to look for,
          why each move works, and how to spot the patterns yourself.
        </p>
        <div className="hero__actions">
          <Link to="/scan" className="btn btn--primary btn--lg">Scan my cube</Link>
          <Link to="/review?manual=1" className="btn btn--ghost btn--lg">Enter colours by hand</Link>
          <button type="button" className="btn btn--quiet btn--lg" onClick={tryScrambled} disabled={busy}>
            {busy ? 'Mixing…' : 'Try a scrambled cube'}
          </button>
        </div>
      </section>

      <section className="features" aria-label="How it works">
        <article className="card feature">
          <span className="feature__num">1</span>
          <h3>Scan</h3>
          <p>Hold each face up to your camera. Colours are matched against your cube’s own centres, so lighting matters less.</p>
        </article>
        <article className="card feature">
          <span className="feature__num">2</span>
          <h3>Learn the patterns</h3>
          <p>Nine stages, from the daisy to the last corner. Before each move, a gentle question asks you to read the cube first.</p>
        </article>
        <article className="card feature">
          <span className="feature__num">3</span>
          <h3>Grow</h3>
          <p>Time your solves with WCA scrambles, track averages and streaks, and see how you improve.</p>
        </article>
      </section>

      <section className="modes card">
        <h2>Three ways to solve</h2>
        <div className="modes__grid">
          <div><h4>Learn</h4><p>The layer-by-layer method with explanations and quizzes. About 100–180 moves, every one understood.</p></div>
          <div><h4>Fast</h4><p>Kociemba’s two-phase algorithm finds about 20 moves for any cube in milliseconds.</p></div>
          <div><h4>Optimal</h4><p>IDA* with a corner pattern database: the shortest possible solution for lightly mixed cubes.</p></div>
        </div>
      </section>
    </div>
  );
}
