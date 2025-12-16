import { defineConfig } from 'vitest/config';

// https://vitest.dev/config/
export default defineConfig({
  test: {
    environment: 'jsdom', // or 'happy-dom'
    globals: true
  }
});
