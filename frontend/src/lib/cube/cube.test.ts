import { describe, expect, it } from 'vitest';
import {
  applyMoves, formatMoves, GEOMETRY, invertMoves, isSolved, parseMoves, randomScramble, SOLVED, trackStickers,
} from './cube';

describe('cube model', () => {
  it('matches the backend for single moves', () => {
    expect(applyMoves(SOLVED, parseMoves('U'))).toBe('UUUUUUUUUBBBRRRRRRRRRFFFFFFDDDDDDDDDFFFLLLLLLLLLBBBBBB');
    expect(applyMoves(SOLVED, parseMoves('R'))).toBe('UUFUUFUUFRRRRRRRRRFFDFFDFFDDDBDDBDDBLLLLLLLLLUBBUBBUBB');
  });

  it('has order 4 for every move and 6 for the sexy move', () => {
    for (const m of ['U', 'R', 'F', 'D', 'L', 'B', 'x', 'y', 'z']) {
      expect(applyMoves(SOLVED, parseMoves(`${m} ${m} ${m} ${m}`))).toBe(SOLVED);
    }
    const sexy = parseMoves("R U R' U'");
    let s = SOLVED;
    for (let i = 0; i < 6; i++) s = applyMoves(s, sexy);
    expect(s).toBe(SOLVED);
  });

  it('parses, formats and inverts', () => {
    const moves = parseMoves("R U2 F' x y2 z’");
    expect(formatMoves(moves)).toBe("R U2 F' x y2 z'");
    expect(formatMoves(invertMoves(moves))).toBe("z y2 x' F U2 R'");
    expect(() => parseMoves('R Q')).toThrow();
  });

  it('undoes a scramble with its inverse', () => {
    const scramble = randomScramble(25);
    const state = applyMoves(SOLVED, scramble);
    expect(isSolved(state)).toBe(false);
    expect(applyMoves(state, invertMoves(scramble))).toBe(SOLVED);
  });

  it('treats a rotated solved cube as solved', () => {
    expect(isSolved(applyMoves(SOLVED, parseMoves('x y')))).toBe(true);
  });

  it('keeps corner stickers on one cubie', () => {
    const corner = [8, 9, 20]; // URF
    const p = GEOMETRY[corner[0]].pos.join();
    expect(corner.every((i) => GEOMETRY[i].pos.join() === p)).toBe(true);
  });

  it('tracks sticker positions through moves', () => {
    // The U9 sticker (index 8) travels to R3? Under U it goes to index 2... check via state.
    const marked = SOLVED.split('');
    marked[8] = 'X';
    const after = applyMoves(marked.join(''), parseMoves('R'));
    expect(trackStickers([8], parseMoves('R')[0])).toEqual([after.indexOf('X')]);
  });
});
