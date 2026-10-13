import { FACES, type Face } from './cube';

export const COLOR_NAMES = ['white', 'yellow', 'red', 'orange', 'green', 'blue'] as const;
export type ColorName = (typeof COLOR_NAMES)[number];

// Display colours: slightly muted for a calm look, still easy to tell apart.
export const PALETTE: Record<ColorName, string> = {
  white: '#f4f1ea',
  yellow: '#f3c623',
  red: '#c8323c',
  orange: '#f08a24',
  green: '#2f9e64',
  blue: '#2a64c5',
};

export type Scheme = Record<Face, ColorName>;

// Standard Western scheme, held with white on top and green in front.
export const DEFAULT_SCHEME: Scheme = { U: 'white', R: 'red', F: 'green', D: 'yellow', L: 'orange', B: 'blue' };

export function schemeHex(scheme: Scheme): Record<Face, string> {
  const out = {} as Record<Face, string>;
  for (const f of FACES) out[f] = PALETTE[scheme[f]];
  return out;
}

export const UNKNOWN_HEX = '#9aa0a6';
