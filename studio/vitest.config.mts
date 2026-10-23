import { defineConfig } from 'vitest/config';

// Unit tests cover pure TypeScript modules only. Nothing here starts Electron
// or needs a GPU, so the suite runs headless in CI.
export default defineConfig({
  test: {
    include: ['src/**/*.test.ts'],
    environment: 'node',
  },
});
