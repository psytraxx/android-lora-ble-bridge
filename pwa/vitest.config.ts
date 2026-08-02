import { defineConfig } from 'vitest/config';

// https://vitest.dev/config/
export default defineConfig({
  test: {
    environment: 'jsdom', // or 'happy-dom'
    globals: true,
    // A concrete origin is required for storage APIs to be available
    environmentOptions: {
      jsdom: { url: 'http://localhost/' }
    },
    setupFiles: ['./src/test-setup.ts']
  }
});
