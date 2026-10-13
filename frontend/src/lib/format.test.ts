import { describe, expect, it } from 'vitest';
import { averageOf, formatMs } from './format';

describe('format', () => {
  it('formats timer values', () => {
    expect(formatMs(9876)).toBe('9.87');
    expect(formatMs(65432)).toBe('1:05.43');
    expect(formatMs(null)).toBe('—');
  });
  it('computes WCA averages', () => {
    expect(averageOf([10, 20], 5)).toBeNull();
    expect(averageOf([9000, 1000, 2000, 3000, 4000], 5)).toBe(3000);
  });
});
