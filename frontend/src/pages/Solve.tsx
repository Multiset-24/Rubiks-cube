import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Link, useNavigate, useSearchParams } from 'react-router-dom';
import { Breath } from '../components/Breath';
import { Cube3D, type Cube3DHandle } from '../components/Cube3D';
import { MoveChips } from '../components/MoveChips';
import { Quiz } from '../components/Quiz';
import { StageTimeline } from '../components/StageTimeline';
import { api, ApiError, type Method, type Solution, type Step } from '../lib/api';
import { useAuth } from '../lib/auth';
import { applyMoves, describeMove, moveToString, parseMoves, trackStickers } from '../lib/cube/cube';
import { schemeHex } from '../lib/cube/colors';
import { formatDuration } from '../lib/format';
import { saveRun } from '../lib/runs';
import { useCubeSession } from '../lib/session';

interface FlatStep {
  stage: number;
  step: Step;
  numberInStage: number;
}

const SPEEDS = { slow: 560, normal: 360, fast: 200 } as const;
type Speed = keyof typeof SPEEDS;

export default function Solve() {
  const [params] = useSearchParams();
  const method = (params.get('method') as Method) || 'beginner';
  const { cube } = useCubeSession();
  const navigate = useNavigate();
  const [solution, setSolution] = useState<Solution | null>(null);
  const [error, setError] = useState<ApiError | null>(null);

  useEffect(() => {
    if (!cube) return;
    let alive = true;
    setSolution(null);
    setError(null);
    api.solve(cube.facelets, method, cube.scheme).then(
      (s) => alive && setSolution(s),
      (e) => alive && setError(e instanceof ApiError ? e : new ApiError(0, 'UNKNOWN', String(e))),
    );
    return () => {
      alive = false;
    };
  }, [cube, method]);

  if (!cube) {
    return (
      <div className="empty card">
        <h2>Nothing to solve yet</h2>
        <p>Scan or enter your cube first.</p>
        <Link className="btn btn--primary" to="/scan">Scan my cube</Link>
      </div>
    );
  }
  if (error) {
    return (
      <div className="empty card">
        <h2>The solver couldn’t answer</h2>
        <p>{error.message}</p>
        {error.requestId && <p className="muted small">Reference: {error.requestId}</p>}
        <div className="row">
          <button className="btn btn--primary" type="button" onClick={() => navigate(0)}>Try again</button>
          <Link className="btn btn--ghost" to="/review">Back to colours</Link>
        </div>
      </div>
    );
  }
  if (!solution) {
    return <Breath label={method === 'optimal' ? 'Searching for the shortest solution…' : 'Solving…'} />;
  }
  return <Player solution={solution} />;
}

function Player({ solution }: { solution: Solution }) {
  const { cube } = useCubeSession();
  const { token } = useAuth();
  const cubeRef = useRef<Cube3DHandle>(null);
  const colors = useMemo(() => schemeHex(cube!.scheme), [cube]);

  const flat = useMemo<FlatStep[]>(() => solution.stages.flatMap((stage, s) =>
    stage.steps.map((step, i) => ({ stage: s, step, numberInStage: i + 1 }))), [solution]);
  const parsed = useMemo(() => flat.map((f) => parseMoves(f.step.moves.join(' '))), [flat]);
  // State before each step, so any step can be shown instantly.
  const states = useMemo(() => {
    const out = [solution.facelets];
    parsed.forEach((m) => out.push(applyMoves(out[out.length - 1], m)));
    return out;
  }, [solution, parsed]);

  const total = flat.length;
  const [k, setK] = useState(0);
  const [done, setDone] = useState(0); // moves of step k already applied
  const [active, setActive] = useState(-1);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState<Speed>('normal');
  const [quizMode, setQuizMode] = useState(solution.method === 'beginner');
  const [answers, setAnswers] = useState<Record<number, boolean>>({});
  const [zen, setZen] = useState(false);
  const [showRecognize, setShowRecognize] = useState(false);
  const startedAt = useRef(Date.now());
  const saved = useRef(false);

  const finished = k >= total;
  const cur = flat[Math.min(k, total - 1)];
  const moves = parsed[Math.min(k, total - 1)] ?? [];
  const stage = solution.stages[cur?.stage ?? 0];
  const needsQuiz = !finished && quizMode && !!cur?.step.quiz && answers[k] === undefined;

  // What the cube shows when idle: start of step k plus the moves already done.
  const shown = useMemo(() => {
    if (finished) return { facelets: states[total], focus: [] as number[] };
    let f = states[k];
    let focus = cur.step.focus;
    for (let i = 0; i < done; i++) {
      f = applyMoves(f, [moves[i]]);
      focus = trackStickers(focus, moves[i]);
    }
    return { facelets: f, focus };
  }, [finished, states, total, k, cur, done, moves]);

  // Bumped by every jump and every new play, so a play that was cut short
  // doesn't apply its moves to the step the user moved to.
  const playGen = useRef(0);
  const shownRef = useRef(shown);
  shownRef.current = shown;

  const quiz = Object.values(answers);
  const quizCorrect = quiz.filter(Boolean).length;

  // Save the run once when the cube is solved (to the account, or locally).
  useEffect(() => {
    if (!finished || saved.current) return;
    saved.current = true;
    void saveRun(token, {
      kind: solution.method === 'beginner' ? 'tutor' : solution.method === 'kociemba' ? 'fast' : 'optimal',
      method: solution.method,
      facelets: solution.facelets,
      scramble: cube?.scramble ?? '',
      moveCount: solution.moveCount,
      durationMs: Date.now() - startedAt.current,
      completed: true,
      quizCorrect,
      quizTotal: quiz.length,
      stepsCompleted: total,
      totalSteps: total,
    });
  }, [finished, token, solution, cube, quizCorrect, quiz.length, total]);

  const goTo = useCallback((next: number) => {
    playGen.current++;
    cubeRef.current?.stop();
    setPlaying(false);
    setActive(-1);
    setDone(0);
    setK(Math.max(0, Math.min(total, next)));
  }, [total]);

  // After a jump, the engine has finished the cut-short move on the old
  // state, so put it back on what the current step shows.
  const resync = useCallback(() => {
    cubeRef.current?.setState(shownRef.current.facelets, shownRef.current.focus);
  }, []);

  const playRest = useCallback(async () => {
    if (playing || finished || needsQuiz) return;
    const rest = moves.slice(done);
    if (!rest.length) return goTo(k + 1);
    const gen = ++playGen.current;
    setPlaying(true);
    const from = done;
    await cubeRef.current?.play(rest, {
      msPerTurn: SPEEDS[speed],
      onMove: (i) => {
        if (gen !== playGen.current) return;
        setActive(from + i);
        setDone(from + i);
      },
    });
    if (gen !== playGen.current) return resync();
    setDone(moves.length);
    setActive(-1);
    setPlaying(false);
  }, [playing, finished, needsQuiz, moves, done, goTo, k, speed, resync]);

  const nextMove = useCallback(async () => {
    if (playing || finished || needsQuiz) return;
    if (done >= moves.length) return goTo(k + 1);
    const gen = ++playGen.current;
    setPlaying(true);
    setActive(done);
    await cubeRef.current?.play([moves[done]], { msPerTurn: SPEEDS[speed] });
    if (gen !== playGen.current) return resync();
    setDone(done + 1);
    setActive(-1);
    setPlaying(false);
  }, [playing, finished, needsQuiz, done, moves, goTo, k, speed, resync]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null;
      if (e.ctrlKey || e.metaKey || e.altKey) return;
      if (t?.closest?.('input, select, textarea, [contenteditable="true"]')) return;
      if (e.code === 'Space' || e.code === 'ArrowRight') {
        e.preventDefault();
        void nextMove();
      } else if (e.code === 'ArrowLeft') {
        goTo(k - 1);
      } else if (e.key === 'z') {
        setZen((z) => !z);
      } else if (e.code === 'Escape') {
        setZen(false);
      } else if (e.key === 'Enter') {
        // Enter on a focused button or link clicks it; don't also play the step.
        if (t?.closest?.('button, a, [role="button"], [role="radio"]')) return;
        void playRest();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [nextMove, playRest, goTo, k]);

  const nextMoveText = !finished && done < moves.length ? moves[done] : null;
  const answer = (ok: boolean) => setAnswers((a) => ({ ...a, [k]: ok }));
  const stageStart = flat.findIndex((f) => f.stage === cur?.stage);

  return (
    <div className={`solve ${zen ? 'solve--zen' : ''}`}>
      {!zen && (
        <StageTimeline
          stages={solution.stages}
          current={finished ? solution.stages.length : cur.stage}
          onJump={(s) => {
            const idx = flat.findIndex((f) => f.stage >= s);
            goTo(idx === -1 ? total : idx);
          }}
        />
      )}

      <div className="solve__layout">
        <div className="solve__stage">
          <Cube3D
            ref={cubeRef}
            className="cube cube--main"
            facelets={shown.facelets}
            colors={colors}
            highlight={shown.focus}
            autoRotate={zen && finished}
            label="Your cube, showing the current step"
          />
          {zen && (
            <div className="zen-overlay" onClick={() => void nextMove()}>
              <p className="zen-overlay__step">{finished ? 'Solved' : cur.step.title}</p>
              {needsQuiz ? (
                // Clicks here answer the quiz; they must not reach the overlay.
                <div className="zen-overlay__quiz" onClick={(e) => e.stopPropagation()}>
                  <Quiz key={k} quiz={cur.step.quiz!} onAnswered={answer} />
                </div>
              ) : nextMoveText ? (
                <>
                  <p className="zen-overlay__move">{moveToString(nextMoveText)}</p>
                  <p className="zen-overlay__desc">{describeMove(nextMoveText)}</p>
                </>
              ) : (
                !finished && <p className="zen-overlay__desc">Tap for the next step</p>
              )}
              <button type="button" className="btn btn--quiet zen-overlay__exit" onClick={(e) => { e.stopPropagation(); setZen(false); }}>
                Leave zen mode
              </button>
            </div>
          )}
        </div>

        {!zen && (
          <aside className="solve__panel">
            {solution.fallbackFrom && <p className="notice">{solution.fallbackReason}</p>}
            {finished ? (
              <div className="card done">
                <p className="eyebrow">Done</p>
                <h2>Cube solved</h2>
                <dl className="done__stats">
                  <div><dt>Time</dt><dd>{formatDuration(Date.now() - startedAt.current)}</dd></div>
                  <div><dt>Moves</dt><dd>{solution.moveCount}</dd></div>
                  {quiz.length > 0 && <div><dt>Patterns read</dt><dd>{quizCorrect}/{quiz.length}</dd></div>}
                </dl>
                <p className="muted">{token ? 'Saved to your progress.' : 'Saved on this device. Sign in to sync it.'}</p>
                <div className="row">
                  <Link className="btn btn--primary" to="/scan">Solve another</Link>
                  <Link className="btn btn--ghost" to="/timer">Try the timer</Link>
                </div>
              </div>
            ) : (
              <div className="card step">
                <p className="eyebrow">
                  {stage.name} · step {cur.numberInStage} of {stage.steps.length}
                </p>
                {cur.numberInStage === 1 && <p className="step__goal">{stage.goal}</p>}
                {stage.recognize && (
                  <button type="button" className="link small" onClick={() => setShowRecognize((v) => !v)}>
                    {showRecognize ? 'Hide' : 'How to read this stage'}
                  </button>
                )}
                {showRecognize && <p className="step__recognize">{stage.recognize}</p>}

                <h2 className="step__title">{cur.step.title}</h2>
                {cur.step.observe && (
                  <div className="step__block">
                    <h4>Look for</h4>
                    <p>{cur.step.observe}</p>
                  </div>
                )}

                {needsQuiz ? (
                  <Quiz key={k} quiz={cur.step.quiz!} onAnswered={answer} />
                ) : (
                  <>
                    <div className="step__block">
                      <h4>Do</h4>
                      <p>{cur.step.instruction}</p>
                    </div>
                    {cur.step.algorithm && (
                      <p className="alg">
                        <span className="alg__name">{cur.step.algorithm.name}</span>
                        <code>{cur.step.algorithm.notation}</code>
                        {cur.step.algorithm.repeat > 1 && <span className="alg__repeat">×{cur.step.algorithm.repeat}</span>}
                      </p>
                    )}
                    <MoveChips moves={cur.step.moves} active={active} done={done} />
                    {cur.step.tip && <p className="step__tip">{cur.step.tip}</p>}
                  </>
                )}

                <div className="controls">
                  <button type="button" className="btn btn--quiet" onClick={() => goTo(k - 1)} disabled={k === 0}>Back</button>
                  <button type="button" className="btn btn--ghost" onClick={() => void nextMove()} disabled={playing || needsQuiz}>
                    {done >= moves.length ? 'Next step' : 'One move'}
                  </button>
                  <button type="button" className="btn btn--primary" onClick={() => void playRest()} disabled={playing || needsQuiz}>
                    {done >= moves.length ? 'Next step' : done > 0 ? 'Play the rest' : 'Show me'}
                  </button>
                </div>
                <div className="controls controls--small">
                  <label>
                    Speed
                    <select value={speed} onChange={(e) => setSpeed(e.target.value as Speed)}>
                      <option value="slow">Slow</option>
                      <option value="normal">Normal</option>
                      <option value="fast">Fast</option>
                    </select>
                  </label>
                  {solution.method === 'beginner' && (
                    <label>
                      <input type="checkbox" checked={quizMode} onChange={(e) => setQuizMode(e.target.checked)} /> Pattern quizzes
                    </label>
                  )}
                  <button type="button" className="link" onClick={() => setZen(true)}>Zen mode</button>
                </div>
                <p className="muted small">
                  Step {k + 1} of {total} · {stageStart >= 0 ? `stage ${cur.stage + 1} of ${solution.stages.length}` : ''} ·
                  Space = next move, Enter = play step, ← = back, Z = zen
                </p>
              </div>
            )}
            {!finished && stage.algorithms.length > 0 && (
              <div className="card algs">
                <h4>Algorithms in this stage</h4>
                <ul>
                  {stage.algorithms.map((a) => (
                    <li key={a.name}><span>{a.name}</span><code>{a.notation}</code></li>
                  ))}
                </ul>
              </div>
            )}
          </aside>
        )}
      </div>
    </div>
  );
}
