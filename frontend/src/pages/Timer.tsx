import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Cube3D } from '../components/Cube3D';
import { api } from '../lib/api';
import { useAuth } from '../lib/auth';
import { applyMoves, formatMoves, parseMoves, randomScramble, SOLVED } from '../lib/cube/cube';
import { DEFAULT_SCHEME, schemeHex } from '../lib/cube/colors';
import { averageOf, formatMs } from '../lib/format';
import { saveRun } from '../lib/runs';
import { load, save } from '../lib/storage';

type Phase = 'idle' | 'holding' | 'ready' | 'inspecting' | 'running' | 'stopped';

interface Solve {
  ms: number;
  scramble: string;
  at: number;
}

const HOLD_MS = 350; // hold the space bar this long before the timer arms (like a stackmat)
const SESSION_KEY = 'zen-cube:timer-session';
const colors = schemeHex(DEFAULT_SCHEME);

async function newScramble(): Promise<string> {
  try {
    return (await api.scramble()).scramble; // random-state, WCA style
  } catch {
    return formatMoves(randomScramble(20));
  }
}

export default function Timer() {
  const { token } = useAuth();
  const [scramble, setScramble] = useState<string>('');
  const [phase, setPhase] = useState<Phase>('idle');
  const [elapsed, setElapsed] = useState(0);
  const [inspection, setInspection] = useState(false);
  const [inspectLeft, setInspectLeft] = useState(15);
  const [session, setSession] = useState<Solve[]>(() => load(SESSION_KEY, []));
  const startRef = useRef(0);
  const holdRef = useRef<number | null>(null);
  const rafRef = useRef(0);
  const inspectRef = useRef<number | null>(null);
  const phaseRef = useRef<Phase>('idle');
  phaseRef.current = phase;

  useEffect(() => {
    void newScramble().then(setScramble);
  }, []);
  useEffect(() => save(SESSION_KEY, session.slice(0, 100)), [session]);

  const scrambled = useMemo(() => {
    try {
      return applyMoves(SOLVED, parseMoves(scramble));
    } catch {
      return SOLVED;
    }
  }, [scramble]);

  const tick = useCallback(() => {
    setElapsed(performance.now() - startRef.current);
    rafRef.current = requestAnimationFrame(tick);
  }, []);

  const startSolve = useCallback(() => {
    if (inspectRef.current) window.clearInterval(inspectRef.current);
    startRef.current = performance.now();
    setPhase('running');
    rafRef.current = requestAnimationFrame(tick);
  }, [tick]);

  const stopSolve = useCallback(() => {
    cancelAnimationFrame(rafRef.current);
    const ms = Math.round(performance.now() - startRef.current);
    setElapsed(ms);
    setPhase('stopped');
    const solve = { ms, scramble, at: Date.now() };
    setSession((s) => [solve, ...s]);
    void saveRun(token, { kind: 'timer', durationMs: ms, scramble, completed: true });
    void newScramble().then(setScramble);
  }, [scramble, token]);

  const startInspection = useCallback(() => {
    setPhase('inspecting');
    setInspectLeft(15);
    const began = Date.now();
    inspectRef.current = window.setInterval(() => {
      setInspectLeft(15 - Math.floor((Date.now() - began) / 1000));
    }, 200);
  }, []);

  const press = useCallback(() => {
    const p = phaseRef.current;
    if (p === 'running') return stopSolve();
    if (p === 'idle' || p === 'stopped' || p === 'inspecting') {
      if (inspection && p !== 'inspecting') return startInspection();
      setPhase('holding');
      holdRef.current = window.setTimeout(() => setPhase('ready'), HOLD_MS);
    }
  }, [stopSolve, inspection, startInspection]);

  const release = useCallback(() => {
    const p = phaseRef.current;
    if (holdRef.current) window.clearTimeout(holdRef.current);
    if (p === 'ready') startSolve();
    else if (p === 'holding') setPhase(inspection && inspectRef.current ? 'inspecting' : 'idle');
  }, [startSolve, inspection]);

  useEffect(() => {
    const down = (e: KeyboardEvent) => {
      if (e.code !== 'Space' || e.repeat) {
        if (phaseRef.current === 'running' && !e.repeat) stopSolve();
        return;
      }
      e.preventDefault();
      press();
    };
    const up = (e: KeyboardEvent) => {
      if (e.code === 'Space') {
        e.preventDefault();
        release();
      }
    };
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
      cancelAnimationFrame(rafRef.current);
      if (inspectRef.current) window.clearInterval(inspectRef.current);
    };
  }, [press, release, stopSolve]);

  const times = session.map((s) => s.ms);
  const best = times.length ? Math.min(...times) : null;
  const display = phase === 'inspecting' ? String(Math.max(0, inspectLeft)) : formatMs(elapsed);

  return (
    <div className="timer">
      <header className="page-head">
        <h1>Timer</h1>
        <p className="muted">Hold space (or touch and hold) until the time turns green, release to start, press again to stop.</p>
      </header>

      <div className="card timer__scramble">
        <p className="eyebrow">Scramble · white on top, green in front</p>
        <p className="scramble">{scramble || '…'}</p>
        <button type="button" className="link" onClick={() => void newScramble().then(setScramble)}>New scramble</button>
      </div>

      <div className="timer__layout">
        <button
          type="button"
          className={`timer__face timer__face--${phase}`}
          onPointerDown={(e) => {
            e.preventDefault();
            press();
          }}
          onPointerUp={release}
          aria-label="Timer: hold to arm, release to start, tap to stop"
        >
          <span className="timer__time">{display}</span>
          <span className="timer__hint">
            {phase === 'holding' ? 'keep holding…' : phase === 'ready' ? 'release to start' : phase === 'running' ? 'tap to stop' : phase === 'inspecting' ? 'inspect · hold to start' : 'hold to start'}
          </span>
        </button>
        <div className="card timer__cube">
          <Cube3D className="cube cube--small" facelets={scrambled} colors={colors} label="Scrambled cube preview" />
          <label className="small">
            <input type="checkbox" checked={inspection} onChange={(e) => setInspection(e.target.checked)} /> 15 s inspection
          </label>
        </div>
      </div>

      <div className="stats-row">
        <div className="tile"><span>Best</span><strong>{formatMs(best)}</strong></div>
        <div className="tile"><span>Ao5</span><strong>{formatMs(averageOf(times, 5))}</strong></div>
        <div className="tile"><span>Ao12</span><strong>{formatMs(averageOf(times, 12))}</strong></div>
        <div className="tile"><span>Solves</span><strong>{times.length}</strong></div>
      </div>

      {session.length > 0 && (
        <div className="card">
          <div className="row row--between">
            <h3>This session</h3>
            <button type="button" className="link" onClick={() => setSession([])}>Clear session</button>
          </div>
          <ol className="times">
            {session.slice(0, 50).map((s, i) => (
              <li key={s.at}>
                <span className="muted">{session.length - i}.</span>
                <strong>{formatMs(s.ms)}</strong>
                <span className="muted small scramble--small">{s.scramble}</span>
              </li>
            ))}
          </ol>
          <p className="muted small">{token ? 'Each solve is saved to your progress.' : 'Sign in to keep your times across devices.'}</p>
        </div>
      )}
    </div>
  );
}
