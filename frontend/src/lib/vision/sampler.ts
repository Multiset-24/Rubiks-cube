import type { RGB } from './color';

// Fraction of the frame's shorter side covered by the 3x3 guide square.
export const GUIDE_FRACTION = 0.62;

// Samples the 9 cells of the centred guide square from the current video frame.
// Uses the raw camera frame (not the mirrored preview), which shows the face as
// someone looking at it would, matching the facelet convention. Each cell's
// colour is the per-channel median of its central area, which ignores glare
// spots, logos and the black plastic between stickers.
export function sampleGrid(video: HTMLVideoElement, canvas: HTMLCanvasElement): RGB[] | null {
  const vw = video.videoWidth;
  const vh = video.videoHeight;
  if (!vw || !vh) return null;
  const scale = Math.min(1, 480 / Math.min(vw, vh));
  const w = Math.round(vw * scale);
  const h = Math.round(vh * scale);
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  if (!ctx) return null;
  ctx.drawImage(video, 0, 0, w, h);
  const side = Math.min(w, h) * GUIDE_FRACTION;
  const x0 = (w - side) / 2;
  const y0 = (h - side) / 2;
  const cell = side / 3;
  const out: RGB[] = [];
  for (let r = 0; r < 3; r++) {
    for (let c = 0; c < 3; c++) {
      const inset = cell * 0.28;
      const x = Math.round(x0 + c * cell + inset);
      const y = Math.round(y0 + r * cell + inset);
      const s = Math.max(2, Math.round(cell - 2 * inset));
      out.push(medianColor(ctx.getImageData(x, y, s, s).data));
    }
  }
  return out;
}

export function medianColor(data: Uint8ClampedArray): RGB {
  const rs: number[] = [];
  const gs: number[] = [];
  const bs: number[] = [];
  for (let i = 0; i < data.length; i += 8) {  // every other pixel is plenty
    rs.push(data[i]);
    gs.push(data[i + 1]);
    bs.push(data[i + 2]);
  }
  const med = (a: number[]) => {
    a.sort((x, y) => x - y);
    return a[Math.floor(a.length / 2)] ?? 0;
  };
  return [med(rs), med(gs), med(bs)];
}
