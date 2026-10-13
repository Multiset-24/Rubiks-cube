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
        <p className="eyebrow">Rubik’s Cube tutor</p>
        <h1>Scan your cube.<br />Learn to solve it.</h1>
        <p className="lede">
          Point your camera at each face. Zen Cube reads the colours and walks you through the beginner
          method one step at a time, showing which piece to look at and why each move works.
        </p>
        <div className="hero__actions">
          <Link to="/scan" className="btn btn--primary btn--lg">Scan my cube</Link>
          <Link to="/review?manual=1" className="btn btn--ghost btn--lg">Enter colours by hand</Link>
          <button type="button" className="btn btn--quiet btn--lg" onClick={tryScrambled} disabled={busy}>
            {busy ? 'Loading…' : 'Try a random cube'}
          </button>
        </div>
      </section>

      <section className="features" aria-label="How it works">
        <article className="card feature">
          <span className="feature__num">1</span>
          <h3>Scan</h3>
          <p>Hold each face up to the camera. Stickers are matched against your cube’s own centre colours, which copes better with bad lighting.</p>
        </article>
        <article className="card feature">
          <span className="feature__num">2</span>
          <h3>Learn</h3>
          <p>Nine stages, from the daisy to the last corner. Optional quiz questions ask you to read the cube before the moves are shown.</p>
        </article>
        <article className="card feature">
          <span className="feature__num">3</span>
          <h3>Practise</h3>
          <p>Time solves with WCA-style scrambles and track your best, Ao5, Ao12 and daily streak.</p>
        </article>
      </section>

      <section className="modes card">
        <h2>Three ways to solve</h2>
        <div className="modes__grid">
          <div><h4>Learn</h4><p>Layer-by-layer method with explanations and quizzes. Around 160 moves.</p></div>
          <div><h4>Fast</h4><p>Kociemba’s two-phase algorithm: about 21 moves for any cube.</p></div>
          <div><h4>Optimal</h4><p>IDA* with a corner pattern database. Shortest solution, for cubes up to about 9 moves from solved.</p></div>
        </div>
      </section>
    </div>
  );
}
