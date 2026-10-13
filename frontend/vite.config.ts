import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  build: {
    target: 'es2020',
    // three.js alone is ~540 kB minified (137 kB gzip) and only loads on cube pages.
    chunkSizeWarningLimit: 600,
    rollupOptions: {
      output: {
        // three.js is large and changes rarely: its own chunk caches well.
        manualChunks: { three: ['three'] },
      },
    },
  },
  test: {
    environment: 'node',
  },
});
