import { useEffect, useMemo, useRef, useState } from 'react';
import { Link, useNavigate, useSearchParams } from 'react-router-dom';
import { CubeNet } from '../components/CubeNet';
import { Cube3D } from '../components/Cube3D';
import { api, ApiError, type Method, type ValidationIssue } from '../lib/api';
import { FACES, type Face } from '../lib/cube/cube';
import { DEFAULT_SCHEME, PALETTE, schemeHex } from '../lib/cube/colors';
import { useCubeSession } from '../lib/session';

const BLANK = FACES.map((f) => '????' + f + '????').join('');

const METHODS: { id: Method; name: string; text: string }[] = [
  { id: 'beginner', name: 'Learn', text: 'Step-by-step tutor with explanations and pattern quizzes.' },
  { id: 'kociemba', name: 'Fast', text: 'About 20 moves for any cube (Kociemba two-phase).' },
  { id: 'optimal', name: 'Optimal', text: 'Shortest possible solution for lightly mixed cubes (IDA*).' },
];

export default function Review() {
  const navigate = useNavigate();
  const [params] = useSearchParams();
  const { cube, setCube } = useCubeSession();
  const manual = params.get('manual') === '1';

  // Manual entry starts from a blank cube with standard centres.
  useEffect(() => {
    if (manual && (!cube || cube.source !== 'manual')) {
      setCube({ facelets: BLANK, scheme: DEFAULT_SCHEME, source: 'manual' });
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [manual]);

  const [paint, setPaint] = useState<Face>('U');
  const [method, setMethod] = useState<Method>('beginner');
  const [issues, setIssues] = useState<ValidationIssue[]>([]);
  const [checking, setChecking] = useState(false);
  const [offline, setOffline] = useState(false);
  const [valid, setValid] = useState(false);
  const seq = useRef(0);

  const facelets = cube?.facelets ?? BLANK;
  const scheme = cube?.scheme ?? DEFAULT_SCHEME;
  const colors = useMemo(() => schemeHex(scheme), [scheme]);
  const unknown = [...facelets].filter((c) => c === '?').length;
  const counts = useMemo(() => {
    const c: Record<string, number> = {};
    for (const ch of facelets) c[ch] = (c[ch] ?? 0) + 1;
    return c;
  }, [facelets]);

  // Validate with the server whenever the cube is complete (debounced, latest wins).
  useEffect(() => {
    setValid(false);
    setIssues([]);
    if (!cube || unknown > 0) return;
    const id = ++seq.current;
    const t = window.setTimeout(async () => {
      setChecking(true);
      try {
        const r = await api.validate(cube.facelets, cube.scheme);
        if (id !== seq.current) return;
        setIssues(r.issues);
        setValid(r.valid);
        setOffline(false);
      } catch (e) {
        if (id === seq.current) setOffline(e instanceof ApiError && e.status === 0);
      } finally {
        if (id === seq.current) setChecking(false);
      }
    }, 350);
    return () => window.clearTimeout(t);
  }, [cube, unknown]);

  if (!cube && !manual) {
    return (
      <div className="empty card">
        <h2>No cube yet</h2>
        <p>Scan your cube or enter its colours to get started.</p>
        <Link className="btn btn--primary" to="/scan">Scan my cube</Link>
      </div>
    );
  }

  const onPaint = (i: number) => {
    if (!cube) return;
    const next = cube.facelets.substring(0, i) + paint + cube.facelets.substring(i + 1);
    setCube({ ...cube, facelets: next, unsure: cube.unsure?.filter((u) => u !== i) });
  };

  const flagged = issues.flatMap((i) => i.facelets);

  return (
    <div className="review">
      <header className="page-head">
        <h1>{manual ? 'Enter your colours' : 'Check the colours'}</h1>
        <p className="muted">
          {manual
            ? 'Pick a colour, then tap stickers to paint them. Hold the cube with white on top and green in front.'
            : 'Compare with your cube and tap any sticker that is wrong. Dotted stickers are ones the camera was unsure about.'}
        </p>
      </header>

      <div className="review__layout">
        <div className="card review__net">
          <div className="palette" role="radiogroup" aria-label="Paint colour">
            {FACES.map((f) => (
              <button
                key={f}
                type="button"
                role="radio"
                aria-checked={paint === f}
                className={`palette__swatch ${paint === f ? 'is-active' : ''}`}
                style={{ background: PALETTE[scheme[f]] }}
                onClick={() => setPaint(f)}
                title={scheme[f]}
              >
                <span className="sr-only">{scheme[f]}</span>
                <span className="palette__count">{counts[f] ?? 0}/9</span>
              </button>
            ))}
          </div>
          <CubeNet facelets={facelets} colors={colors} onPaint={onPaint} flagged={flagged} lowConfidence={cube?.unsure} />
          {unknown > 0 && <p className="muted small">{unknown} stickers left to paint.</p>}
        </div>

        <aside className="review__side">
          <div className="card review__preview">
            <Cube3D className="cube cube--preview" facelets={facelets} colors={colors} highlight={flagged} label="Your cube" />
          </div>

          <div className="card">
            {checking && <p className="muted">Checking your cube…</p>}
            {offline && <p className="warn">Can’t reach the solver right now. It may be waking up; try again in a moment.</p>}
            {valid && <p className="ok">This is a valid cube.</p>}
            {issues.length > 0 && (
              <ul className="issues">
                {issues.map((i, k) => (
                  <li key={k}>
                    <strong>{i.message}</strong>
                    <span>{i.hint}</span>
                  </li>
                ))}
              </ul>
            )}

            <div className="methods" role="radiogroup" aria-label="Solving method">
              {METHODS.map((m) => (
                <button
                  key={m.id}
                  type="button"
                  role="radio"
                  aria-checked={method === m.id}
                  className={`method ${method === m.id ? 'is-active' : ''}`}
                  onClick={() => setMethod(m.id)}
                >
                  <strong>{m.name}</strong>
                  <span>{m.text}</span>
                </button>
              ))}
            </div>
            <button
              type="button"
              className="btn btn--primary btn--lg btn--block"
              disabled={!valid}
              onClick={() => navigate(`/solve?method=${method}`)}
            >
              Begin
            </button>
          </div>
        </aside>
      </div>
    </div>
  );
}
