import { FACES, type Face } from '../cube/cube';
import { COLOR_NAMES, type ColorName, type Scheme } from '../cube/colors';
import { labDistance, rgbToLab, type Lab, type RGB } from './color';
import { hungarian } from './hungarian';

// Typical camera readings of real stickers, used for the live preview and to
// name the centre colours.
export const REFERENCE_RGB: Record<ColorName, RGB> = {
  white: [215, 220, 220],
  yellow: [215, 205, 50],
  red: [185, 35, 45],
  orange: [235, 115, 35],
  green: [35, 160, 85],
  blue: [30, 85, 180],
};
const REFERENCE_LAB = Object.fromEntries(
  COLOR_NAMES.map((n) => [n, rgbToLab(REFERENCE_RGB[n])]),
) as Record<ColorName, Lab>;

// Quick per-sticker guess for the live camera preview.
export function guessColor(rgb: RGB): ColorName {
  const lab = rgbToLab(rgb);
  let best: ColorName = 'white';
  let bestD = Infinity;
  for (const n of COLOR_NAMES) {
    const d = labDistance(lab, REFERENCE_LAB[n]);
    if (d < bestD) {
      bestD = d;
      best = n;
    }
  }
  return best;
}

export interface ScanResult {
  facelets: string; // 54 chars, URFDLB letters
  scheme: Scheme; // colour name of each face's centre
  confidence: number[]; // per sticker, 0..1 (low = worth double-checking)
}

// `faces[f]` holds the 9 samples of face FACES[f] in reading order.
// Each non-centre sticker is matched to one of the six centres it was scanned
// with (same camera, same light), under the constraint that every colour
// appears exactly 9 times. This global assignment fixes most red/orange and
// white/yellow confusions a per-sticker nearest-colour rule would make.
export function classifyScan(faces: RGB[][]): ScanResult {
  if (faces.length !== 6 || faces.some((f) => f.length !== 9)) throw new Error('need 6 faces of 9 samples');
  const labs = faces.map((f) => f.map(rgbToLab));
  const centres = labs.map((f) => f[4]);

  const rows: { face: number; idx: number }[] = [];
  for (let f = 0; f < 6; f++) for (let i = 0; i < 9; i++) if (i !== 4) rows.push({ face: f, idx: i });
  // 8 slots per colour (the 9th is the centre itself).
  const cost = rows.map(({ face, idx }) => {
    const out: number[] = [];
    for (let c = 0; c < 6; c++) {
      const d = labDistance(labs[face][idx], centres[c]);
      for (let k = 0; k < 8; k++) out.push(d);
    }
    return out;
  });
  const assignment = hungarian(cost);

  const letters = new Array<string>(54);
  const confidence = new Array<number>(54).fill(1);
  for (let f = 0; f < 6; f++) letters[f * 9 + 4] = FACES[f];
  rows.forEach(({ face, idx }, r) => {
    const colour = Math.floor(assignment[r] / 8);
    letters[face * 9 + idx] = FACES[colour];
    const ds = centres.map((c) => labDistance(labs[face][idx], c)).sort((a, b) => a - b);
    // Margin between the best and second-best centre, squashed to 0..1.
    confidence[face * 9 + idx] = Math.min(1, (ds[1] - ds[0]) / 25);
  });

  return { facelets: letters.join(''), scheme: nameCentres(centres), confidence };
}

// Gives each centre a distinct colour name (6x6 assignment against references).
export function nameCentres(centres: Lab[]): Scheme {
  const cost = centres.map((c) => COLOR_NAMES.map((n) => labDistance(c, REFERENCE_LAB[n])));
  const a = hungarian(cost);
  const scheme = {} as Scheme;
  FACES.forEach((f: Face, i) => {
    scheme[f] = COLOR_NAMES[a[i]];
  });
  return scheme;
}
