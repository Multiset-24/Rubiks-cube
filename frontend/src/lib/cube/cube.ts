// Cube model shared by the 3D view, the scanner and the tutor player.
//
// A cube state is the 54-character facelet string used by the API: faces in
// the order U R F D L B, each read row by row while looking at that face, as
// in the usual net. Each character is the face whose centre has that colour.
// Move permutations are generated from 3D sticker geometry (the same method
// as the C++ backend), so client and server can never disagree.

export const FACES = ['U', 'R', 'F', 'D', 'L', 'B'] as const;
export type Face = (typeof FACES)[number];
export const SOLVED = FACES.map((f) => f.repeat(9)).join('');

export type Vec3 = [number, number, number];
export interface StickerGeometry {
  pos: Vec3; // cubie position, each coordinate in {-1, 0, 1}; x right, y up, z front
  normal: Vec3; // direction the sticker faces
}

// 0..5 = face turns U R F D L B, 6..8 = whole-cube rotations x y z.
export interface Move {
  axis: number;
  turns: 1 | 2 | 3; // 1 clockwise, 2 half, 3 counter-clockwise
}

const AXIS_CHARS = 'URFDLBxyz';
export const AXIS_NORMALS: Vec3[] = [
  [0, 1, 0], [1, 0, 0], [0, 0, 1], [0, -1, 0], [-1, 0, 0], [0, 0, -1],
  [1, 0, 0], [0, 1, 0], [0, 0, 1],
];

export function stickerGeometry(index: number): StickerGeometry {
  const face = Math.floor(index / 9);
  const r = Math.floor((index % 9) / 3);
  const c = index % 3;
  switch (face) {
    case 0: return { pos: [c - 1, 1, r - 1], normal: [0, 1, 0] };
    case 1: return { pos: [1, 1 - r, 1 - c], normal: [1, 0, 0] };
    case 2: return { pos: [c - 1, 1 - r, 1], normal: [0, 0, 1] };
    case 3: return { pos: [c - 1, -1, 1 - r], normal: [0, -1, 0] };
    case 4: return { pos: [-1, 1 - r, c - 1], normal: [-1, 0, 0] };
    default: return { pos: [1 - c, 1 - r, -1], normal: [0, 0, -1] };
  }
}

export const GEOMETRY: StickerGeometry[] = Array.from({ length: 54 }, (_, i) => stickerGeometry(i));

function key(p: Vec3, n: Vec3): string {
  return `${p.join(',')}|${n.join(',')}`;
}
const INDEX_BY_KEY = new Map(GEOMETRY.map((g, i) => [key(g.pos, g.normal), i]));

// Clockwise quarter turn looking at the face with outward normal a:
// rotation by -90 degrees about a, v' = -(a x v) + a (a . v).
export function rotateCw(a: Vec3, v: Vec3): Vec3 {
  const cross: Vec3 = [a[1] * v[2] - a[2] * v[1], a[2] * v[0] - a[0] * v[2], a[0] * v[1] - a[1] * v[0]];
  const dot = a[0] * v[0] + a[1] * v[1] + a[2] * v[2];
  return [-cross[0] + a[0] * dot, -cross[1] + a[1] * dot, -cross[2] + a[2] * dot];
}

export function inLayer(axis: number, pos: Vec3): boolean {
  if (axis >= 6) return true;
  const a = AXIS_NORMALS[axis];
  return a[0] * pos[0] + a[1] * pos[1] + a[2] * pos[2] === 1;
}

// perm[j] = i means: after the move, sticker j shows what sticker i showed.
function quarterTurnPerm(axis: number): number[] {
  const perm = Array.from({ length: 54 }, (_, i) => i);
  const a = AXIS_NORMALS[axis];
  GEOMETRY.forEach((g, i) => {
    if (!inLayer(axis, g.pos)) return;
    const j = INDEX_BY_KEY.get(key(rotateCw(a, g.pos), rotateCw(a, g.normal)));
    if (j === undefined) throw new Error('geometry error');
    perm[j] = i;
  });
  return perm;
}

const PERMS: number[][][] = Array.from({ length: 9 }, (_, axis) => {
  const q = quarterTurnPerm(axis);
  const compose = (first: number[], second: number[]) => second.map((i) => first[i]);
  const half = compose(q, q);
  return [q, half, compose(half, q)];
});

export function permutation(m: Move): number[] {
  return PERMS[m.axis][m.turns - 1];
}

export function applyMove(state: string, m: Move): string {
  const p = permutation(m);
  let out = '';
  for (let j = 0; j < 54; j++) out += state[p[j]];
  return out;
}

export function applyMoves(state: string, moves: Move[]): string {
  return moves.reduce(applyMove, state);
}

// Where sticker positions travel under a move (to keep highlighting a piece).
export function trackStickers(indices: number[], m: Move): number[] {
  const p = permutation(m);
  return indices.map((i) => p.indexOf(i));
}

export function moveToString(m: Move): string {
  return AXIS_CHARS[m.axis] + (m.turns === 2 ? '2' : m.turns === 3 ? "'" : '');
}

export function parseMoves(text: string): Move[] {
  const out: Move[] = [];
  const re = /([URFDLBxyzXYZ])(2|3)?('|’)?/g;
  const cleaned = text.replace(/[\s,()]+/g, ' ').trim();
  if (!cleaned) return out;
  for (const token of cleaned.split(' ')) {
    re.lastIndex = 0;
    const m = re.exec(token);
    if (!m || m[0] !== token) throw new Error(`Unknown move "${token}"`);
    let turns = m[2] ? Number(m[2]) : 1;
    if (m[3]) turns = 4 - turns;
    const letter = 'XYZ'.includes(m[1]) ? m[1].toLowerCase() : m[1];
    out.push({ axis: AXIS_CHARS.indexOf(letter), turns: turns as 1 | 2 | 3 });
  }
  return out;
}

export function formatMoves(moves: Move[]): string {
  return moves.map(moveToString).join(' ');
}

export function invertMoves(moves: Move[]): Move[] {
  return [...moves].reverse().map((m) => ({ axis: m.axis, turns: (4 - m.turns) as 1 | 2 | 3 }));
}

export function isSolved(state: string): boolean {
  for (let f = 0; f < 6; f++) {
    const c = state[f * 9 + 4];
    for (let i = 0; i < 9; i++) if (state[f * 9 + i] !== c) return false;
  }
  return true;
}

export function randomScramble(length = 20, rng: () => number = Math.random): Move[] {
  const out: Move[] = [];
  let last = -1;
  while (out.length < length) {
    const axis = Math.floor(rng() * 6);
    // Avoid R R and R L R style redundancy.
    if (axis === last || (out.length >= 2 && axis % 3 === last % 3 && out[out.length - 2].axis === axis)) continue;
    out.push({ axis, turns: (1 + Math.floor(rng() * 3)) as 1 | 2 | 3 });
    last = axis;
  }
  return out;
}

const FACE_WORDS = ['top', 'right', 'front', 'bottom', 'left', 'back'];
const CW_EFFECT = ['front goes left', 'front goes up', 'top goes right', 'front goes right', 'front goes down',
  'top goes left', 'front goes up', 'front goes left', 'top goes right'];
const CCW_EFFECT = ['front goes right', 'front goes down', 'top goes left', 'front goes left', 'front goes up',
  'top goes right', 'front goes down', 'front goes right', 'top goes left'];

export function describeMove(m: Move): string {
  const what = m.axis >= 6
    ? `Turn the whole cube like ${'RUF'[m.axis - 6]}`
    : `Turn the ${FACE_WORDS[m.axis]} face`;
  if (m.turns === 2) return `${what} twice`;
  const dir = m.axis >= 6 ? (m.turns === 1 ? '' : ' backwards') : m.turns === 1 ? ' clockwise' : ' counter-clockwise';
  return `${what}${dir} (${m.turns === 1 ? CW_EFFECT[m.axis] : CCW_EFFECT[m.axis]})`;
}
