import { useMemo, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { Cube3D, type Cube3DHandle } from '../components/Cube3D';
import { PatternDiagram } from '../components/PatternDiagram';
import { applyMoves, describeMove, parseMoves, SOLVED } from '../lib/cube/cube';
import { DEFAULT_SCHEME, schemeHex } from '../lib/cube/colors';

const colors = schemeHex(DEFAULT_SCHEME);
const FACE_MOVES = ['U', 'D', 'R', 'L', 'F', 'B'];
const ROTATIONS = ['x', 'y', 'z'];

const ALGORITHMS = [
  { stage: 'First layer corners', name: 'Sexy move', notation: "R U R' U'", why: 'Repeat it with a white corner above its slot; every 1, 3 or 5 repeats the corner drops in.' },
  { stage: 'Second layer', name: 'Right insert', notation: "U R U' R' U' F' U F", why: 'Sends the front-top edge into the front-right slot.' },
  { stage: 'Second layer', name: 'Left insert', notation: "U' L' U L U F U' F'", why: 'Mirror image: sends it into the front-left slot.' },
  { stage: 'Yellow cross', name: 'Yellow cross', notation: "F R U R' U' F'", why: 'Dot → L → line → cross. Same moves every time.' },
  { stage: 'Yellow edges', name: 'Sune + U', notation: "R U R' U R U2 R' U", why: 'With matched edges at back and right, swaps front and left.' },
  { stage: 'Yellow corners', name: 'Corner cycle', notation: "U R U' L' U R' U' L", why: 'Keeps the front-right corner and cycles the other three.' },
  { stage: 'Yellow corners', name: 'Corner twist', notation: "R' D' R D", why: 'Repeat 2 or 4 times per corner until yellow faces up.' },
];

export default function Learn() {
  const ref = useRef<Cube3DHandle>(null);
  const [state, setState] = useState(SOLVED);
  const [last, setLast] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const play = async (notation: string) => {
    if (busy) return;
    setBusy(true);
    setLast(notation);
    const moves = parseMoves(notation);
    await ref.current?.play(moves, { msPerTurn: 380 });
    setState((s) => applyMoves(s, moves));
    setBusy(false);
  };
  const reset = () => {
    ref.current?.stop();
    setState(SOLVED);
    ref.current?.setState(SOLVED, []);
    setLast(null);
  };
  const lastDescription = useMemo(() => {
    if (!last) return 'Tap a move to see it.';
    const moves = parseMoves(last);
    return moves.length === 1 ? describeMove(moves[0]) : `${last} (${moves.length} moves)`;
  }, [last]);

  return (
    <div className="learn">
      <header className="page-head">
        <h1>Learn</h1>
        <p className="muted">The language of the cube, the patterns to look for, and the handful of algorithms the beginner method uses.</p>
      </header>

      <section className="learn__notation">
        <div className="card learn__cube">
          <Cube3D ref={ref} className="cube cube--preview" facelets={state} colors={colors} label="Practice cube" />
          <p className="learn__desc">{lastDescription}</p>
          <button type="button" className="link" onClick={reset}>Reset cube</button>
        </div>
        <div className="card">
          <h2>Notation</h2>
          <p>
            Each letter is a face: <b>U</b>p, <b>D</b>own, <b>R</b>ight, <b>L</b>eft, <b>F</b>ront, <b>B</b>ack.
            A letter alone means a clockwise quarter turn <em>as if you were looking at that face</em>. An
            apostrophe (<b>R’</b>, “R prime”) means counter-clockwise, and <b>2</b> means a half turn.
          </p>
          <div className="movegrid">
            {FACE_MOVES.map((f) => (
              <div key={f} className="movegrid__row">
                {[f, `${f}'`, `${f}2`].map((m) => (
                  <button key={m} type="button" className="chip" onClick={() => void play(m)} disabled={busy}>{m}</button>
                ))}
              </div>
            ))}
          </div>
          <p>
            Lower-case <b>x</b>, <b>y</b>, <b>z</b> turn the <em>whole cube</em> (like R, U and F). The tutor uses <b>y</b> to
            say “turn the cube so that slot faces you”.
          </p>
          <div className="movegrid__row">
            {ROTATIONS.flatMap((r) => [r, `${r}'`]).map((m) => (
              <button key={m} type="button" className="chip chip--rotation" onClick={() => void play(m)} disabled={busy}>{m}</button>
            ))}
          </div>
        </div>
      </section>

      <section className="card">
        <h2>Patterns to recognise</h2>
        <div className="patterns">
          <div><PatternDiagram pattern="dot" /><h4>Dot</h4><p>No yellow edges up. Do the yellow-cross algorithm once from any angle.</p></div>
          <div><PatternDiagram pattern="L" /><h4>L</h4><p>Two edges next to each other. Hold the L at the back-left (9 and 12 o’clock).</p></div>
          <div><PatternDiagram pattern="line" /><h4>Line</h4><p>Two opposite edges. Hold the line left-to-right.</p></div>
          <div><PatternDiagram pattern="cross" /><h4>Cross</h4><p>Done! Move on to matching the edges.</p></div>
        </div>
        <div className="facts">
          <div>
            <h4>White corner above its slot</h4>
            <p>White sticker facing <b>right</b> → R U R’ U’ once. <b>Up</b> → three times. <b>Towards you</b> → five times.</p>
          </div>
          <div>
            <h4>Second-layer edge</h4>
            <p>Line its front colour up with the centre below (an upside-down T). Its top colour tells you left or right.</p>
          </div>
          <div>
            <h4>Yellow corners</h4>
            <p>Yellow pointing <b>right</b> → R’ D’ R D twice. <b>Towards you</b> → four times. Only turn the top between corners.</p>
          </div>
        </div>
      </section>

      <section className="card">
        <h2>The seven algorithms</h2>
        <ul className="alglist">
          {ALGORITHMS.map((a) => (
            <li key={a.name}>
              <div>
                <p className="eyebrow">{a.stage}</p>
                <h4>{a.name}</h4>
                <p className="muted">{a.why}</p>
              </div>
              <code>{a.notation}</code>
              <button type="button" className="btn btn--ghost" onClick={() => void play(a.notation)} disabled={busy}>Try it</button>
            </li>
          ))}
        </ul>
        <p>Ready? <Link to="/scan" className="link">Scan your cube</Link> and the tutor will walk you through each one in context.</p>
      </section>
    </div>
  );
}
