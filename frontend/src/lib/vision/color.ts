export type RGB = [number, number, number];
export type Lab = [number, number, number];

// sRGB (0..255) -> CIELAB (D65). Lab separates lightness from hue, so shadows
// change mostly L while red vs orange or white vs yellow differ in a/b.
export function rgbToLab([r, g, b]: RGB): Lab {
  const lin = (c: number) => {
    const v = c / 255;
    return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
  };
  const R = lin(r), G = lin(g), B = lin(b);
  const x = (R * 0.4124 + G * 0.3576 + B * 0.1805) / 0.95047;
  const y = R * 0.2126 + G * 0.7152 + B * 0.0722;
  const z = (R * 0.0193 + G * 0.1192 + B * 0.9505) / 1.08883;
  const f = (t: number) => (t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116);
  const fx = f(x), fy = f(y), fz = f(z);
  return [116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)];
}

// Colour distance with lightness down-weighted: uneven lighting across the
// cube shifts L far more than hue.
export function labDistance(a: Lab, b: Lab): number {
  const dL = (a[0] - b[0]) * 0.5;
  return Math.sqrt(dL * dL + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2);
}

export function toHex([r, g, b]: RGB): string {
  return '#' + [r, g, b].map((v) => Math.round(Math.max(0, Math.min(255, v))).toString(16).padStart(2, '0')).join('');
}
