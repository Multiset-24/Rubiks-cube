import { describe, expect, it } from 'vitest';
import { applyMoves, FACES, parseMoves, SOLVED } from '../cube/cube';
import { DEFAULT_SCHEME } from '../cube/colors';
import { rgbToLab, type RGB } from './color';
import { hungarian } from './hungarian';
import { classifyScan, guessColor, REFERENCE_RGB } from './classify';

describe('hungarian', () => {
  it('finds the minimum-cost assignment', () => {
    const a = hungarian([
      [4, 1, 3],
      [2, 0, 5],
      [3, 2, 2],
    ]);
    expect(a).toEqual([1, 0, 2]);
  });
});

describe('colour classification', () => {
  it('converts white and black to Lab', () => {
    expect(rgbToLab([255, 255, 255])[0]).toBeCloseTo(100, 0);
    expect(rgbToLab([0, 0, 0])[0]).toBeCloseTo(0, 0);
  });

  it('guesses reference colours', () => {
    expect(guessColor([240, 120, 40])).toBe('orange');
    expect(guessColor([30, 90, 190])).toBe('blue');
  });

  it('recovers a scrambled cube from noisy, unevenly lit samples', () => {
    const state = applyMoves(SOLVED, parseMoves("R U2 F' L D B2 R' U F2 L' D2 B"));
    let seed = 7;
    const rand = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
    const faces: RGB[][] = [];
    for (let f = 0; f < 6; f++) {
      const light = 0.75 + 0.35 * rand(); // each face lit differently
      const samples: RGB[] = [];
      for (let i = 0; i < 9; i++) {
        const name = DEFAULT_SCHEME[state[f * 9 + i] as (typeof FACES)[number]];
        const base = REFERENCE_RGB[name];
        samples.push(base.map((v) => Math.min(255, v * light + (rand() - 0.5) * 30)) as RGB);
      }
      faces.push(samples);
    }
    const result = classifyScan(faces);
    expect(result.facelets).toBe(state);
    expect(result.scheme).toEqual(DEFAULT_SCHEME);
  });
});
