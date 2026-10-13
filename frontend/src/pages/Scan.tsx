import { useCallback, useEffect, useRef, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { FACES, type Face } from '../lib/cube/cube';
import { PALETTE, type ColorName } from '../lib/cube/colors';
import { classifyScan, guessColor } from '../lib/vision/classify';
import type { RGB } from '../lib/vision/color';
import { GUIDE_FRACTION, sampleGrid } from '../lib/vision/sampler';
import { useCubeSession } from '../lib/session';

// Scan order and how to hold the cube for each face. Holding instructions use
// centre colours, which works the same for a phone's rear camera and a laptop
// webcam (where left/right would be mirrored).
const STEPS: { face: Face; centre: ColorName; top: ColorName; hint: string }[] = [
  { face: 'F', centre: 'green', top: 'white', hint: 'Green centre facing the camera, white centre on top.' },
  { face: 'R', centre: 'red', top: 'white', hint: 'Turn the cube so red faces the camera. White stays on top.' },
  { face: 'B', centre: 'blue', top: 'white', hint: 'Turn again: blue faces the camera, white on top.' },
  { face: 'L', centre: 'orange', top: 'white', hint: 'Once more: orange faces the camera, white on top.' },
  { face: 'U', centre: 'white', top: 'blue', hint: 'Tip the cube: white faces the camera, with the blue side at the top of the picture.' },
  { face: 'D', centre: 'yellow', top: 'green', hint: 'Finally yellow faces the camera, with the green side at the top of the picture.' },
];

type Captures = Partial<Record<Face, RGB[]>>;

export default function Scan() {
  const navigate = useNavigate();
  const { setCube } = useCubeSession();
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(document.createElement('canvas'));
  const [stream, setStream] = useState<MediaStream | null>(null);
  const [facing, setFacing] = useState<'environment' | 'user'>('environment');
  const [mirrored, setMirrored] = useState(false);
  const [aspect, setAspect] = useState(4 / 3);
  const [live, setLive] = useState<RGB[] | null>(null);
  const [captures, setCaptures] = useState<Captures>({});
  const [step, setStep] = useState(0);
  const [error, setError] = useState<string | null>(null);

  const start = useCallback(async (mode: 'environment' | 'user') => {
    setError(null);
    stream?.getTracks().forEach((t) => t.stop());
    try {
      const s = await navigator.mediaDevices.getUserMedia({
        video: { facingMode: { ideal: mode }, width: { ideal: 1280 }, height: { ideal: 720 } },
        audio: false,
      });
      const track = s.getVideoTracks()[0];
      const setting = track.getSettings().facingMode;
      // Laptop webcams usually report no facing mode but face the user.
      setMirrored(setting ? setting === 'user' : mode === 'user' || !/Mobi|Android/i.test(navigator.userAgent));
      setStream(s);
      setFacing(mode);
    } catch (e) {
      const name = (e as DOMException)?.name;
      setError(name === 'NotAllowedError'
        ? 'Camera permission was denied. Allow camera access in your browser settings, or enter the colours by hand.'
        : 'No camera is available here. You can enter the colours by hand instead.');
    }
  }, [stream]);

  useEffect(() => () => stream?.getTracks().forEach((t) => t.stop()), [stream]);

  useEffect(() => {
    const v = videoRef.current;
    if (!v || !stream) return;
    v.srcObject = stream;
    const onMeta = () => setAspect(v.videoWidth / v.videoHeight || 4 / 3);
    v.addEventListener('loadedmetadata', onMeta);
    void v.play().catch(() => undefined);
    const id = window.setInterval(() => {
      const samples = sampleGrid(v, canvasRef.current);
      if (samples) setLive(samples);
    }, 200);
    return () => {
      window.clearInterval(id);
      v.removeEventListener('loadedmetadata', onMeta);
    };
  }, [stream]);

  const current = STEPS[step];
  const allDone = STEPS.every((s) => captures[s.face]);

  const capture = useCallback(() => {
    if (!live || !current) return;
    const next = { ...captures, [current.face]: live };
    setCaptures(next);
    const nextStep = STEPS.findIndex((s) => !next[s.face]);
    setStep(nextStep === -1 ? STEPS.length : nextStep);
  }, [live, current, captures]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.code === 'Space' && stream && current) {
        e.preventDefault();
        capture();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [capture, stream, current]);

  const finish = () => {
    const faces = FACES.map((f) => captures[f]!);
    const result = classifyScan(faces);
    const unsure = result.confidence.flatMap((c, i) => (c < 0.25 ? [i] : []));
    setCube({ facelets: result.facelets, scheme: result.scheme, source: 'scan', unsure });
    stream?.getTracks().forEach((t) => t.stop());
    navigate('/review');
  };

  const centreGuess = live ? guessColor(live[4]) : null;
  const wrongCentre = current && centreGuess && centreGuess !== current.centre;

  return (
    <div className="scan">
      <header className="page-head">
        <h1>Scan your cube</h1>
        <p className="muted">Six faces, one at a time. Fill the square with the face and hold still.</p>
      </header>

      <div className="scan__layout">
        <div className="scan__camera card">
          {!stream ? (
            <div className="scan__start">
              <p>The video is processed in your browser. Only the 54 sticker colours are sent to the server.</p>
              <button type="button" className="btn btn--primary btn--lg" onClick={() => start('environment')}>
                Turn on camera
              </button>
              {error && <p className="error">{error}</p>}
              <Link to="/review?manual=1" className="link">Enter colours by hand instead</Link>
            </div>
          ) : (
            <div className="viewfinder" style={{ aspectRatio: String(aspect) }}>
              <div className={`viewfinder__inner ${mirrored ? 'is-mirrored' : ''}`}>
                <video ref={videoRef} playsInline muted />
                <div
                  className="guide"
                  style={{
                    width: `${(GUIDE_FRACTION * Math.min(1, 1 / aspect)) * 100}%`,
                    height: `${(GUIDE_FRACTION * Math.min(1, aspect)) * 100}%`,
                  }}
                >
                  {Array.from({ length: 9 }, (_, i) => (
                    <div key={i} className="guide__cell">
                      {live && <span className="guide__dot" style={{ background: PALETTE[guessColor(live[i])] }} />}
                    </div>
                  ))}
                </div>
              </div>
              {current && (
                <div className="viewfinder__bar">
                  <span className="pill" style={{ ['--pill' as string]: PALETTE[current.top] }}>{current.top} on top</span>
                  <button type="button" className="btn btn--primary" onClick={capture} disabled={!live}>
                    Capture {current.centre} face
                  </button>
                  <button type="button" className="btn btn--quiet" onClick={() => start(facing === 'user' ? 'environment' : 'user')}>
                    Switch camera
                  </button>
                </div>
              )}
            </div>
          )}
        </div>

        <aside className="scan__side">
          {current ? (
            <div className="card scan__step">
              <p className="eyebrow">Face {step + 1} of 6</p>
              <FaceGuide centre={current.centre} top={current.top} />
              <p>{current.hint}</p>
              {wrongCentre && stream && (
                <p className="warn">That centre looks {centreGuess}. Expected {current.centre}. Check the face, or capture anyway if your cube’s colours differ.</p>
              )}
              <p className="muted small">Even daylight works best; avoid glare. Press Space to capture.</p>
            </div>
          ) : (
            <div className="card scan__step">
              <p className="eyebrow">All six faces captured</p>
              <p>Next you’ll check the colours and fix any the camera got wrong.</p>
              <button type="button" className="btn btn--primary btn--lg" onClick={finish} disabled={!allDone}>
                Review colours
              </button>
            </div>
          )}

          <div className="thumbs">
            {STEPS.map((s, i) => (
              <button
                key={s.face}
                type="button"
                className={`thumb ${i === step ? 'is-current' : ''}`}
                onClick={() => setStep(i)}
                title={captures[s.face] ? `Retake the ${s.centre} face` : `Scan the ${s.centre} face`}
              >
                <span className="thumb__grid">
                  {Array.from({ length: 9 }, (_, k) => (
                    <span
                      key={k}
                      style={{
                        background: captures[s.face]
                          ? PALETTE[guessColor(captures[s.face]![k])]
                          : k === 4 ? PALETTE[s.centre] : 'var(--unknown)',
                      }}
                    />
                  ))}
                </span>
                <span className="thumb__label">{s.centre}</span>
              </button>
            ))}
          </div>
        </aside>
      </div>
    </div>
  );
}

function FaceGuide({ centre, top }: { centre: ColorName; top: ColorName }) {
  return (
    <svg viewBox="0 0 120 132" width="120" height="132" className="faceguide" role="img"
      aria-label={`${centre} centre facing you, ${top} at the top`}>
      <rect x="10" y="2" width="100" height="14" rx="5" fill={PALETTE[top]} stroke="var(--line)" />
      {Array.from({ length: 9 }, (_, i) => (
        <rect key={i} x={10 + (i % 3) * 34} y={22 + Math.floor(i / 3) * 34} width="30" height="30" rx="6"
          fill={i === 4 ? PALETTE[centre] : 'var(--unknown)'} stroke="var(--line)" />
      ))}
    </svg>
  );
}
