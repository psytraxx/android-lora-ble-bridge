import tailwindcss from '@tailwindcss/vite';
import { defineConfig } from 'vite';
import { VitePWA } from 'vite-plugin-pwa';

// Get base path from environment or default to '/' for local dev
const base = process.env.BASE_URL ? `/${process.env.BASE_URL}/` : '/';

export default defineConfig({
  // Use repository name as base for GitHub Pages, fallback to '/' for local dev
  base,
  plugins: [
    tailwindcss(),
    VitePWA({
      registerType: 'autoUpdate',
      includeAssets: ['favicon.ico', 'apple-touch-icon.png', 'mask-icon.svg'],
      manifest: {
        name: 'LoRa Chat',
        short_name: 'LoRa Chat',
        description: 'Long-range messaging via LoRa radio and BLE',
        theme_color: '#1976d2',
        background_color: '#ffffff',
        display: 'standalone',
        scope: base,
        start_url: base,
        orientation: 'portrait',
        icons: [
          {
            src: `${base}pwa-192x192.png`,
            sizes: '192x192',
            type: 'image/png'
          },
          {
            src: `${base}pwa-512x512.png`,
            sizes: '512x512',
            type: 'image/png'
          },
          {
            src: `${base}pwa-512x512.png`,
            sizes: '512x512',
            type: 'image/png',
            purpose: 'any maskable'
          }
        ],
        screenshots: [
          {
            src: `${base}screenshot-wide.png`,
            sizes: '831x756',
            type: 'image/png',
            form_factor: 'wide',
            label: 'LoRa Chat'
          },
          {
            src: `${base}screenshot-narrow.png`,
            sizes: '564x779',
            type: 'image/png',
            form_factor: 'narrow',
            label: 'LoRa Chat'
          }
        ]
      },
      workbox: {
        globPatterns: ['**/*.{js,css,html,ico,png,svg,woff2}'],
        runtimeCaching: [
          {
            urlPattern: /^https:\/\/maps\.googleapis\.com\/.*/i,
            handler: 'CacheFirst',
            options: {
              cacheName: 'google-maps-cache',
              expiration: {
                maxEntries: 10,
                maxAgeSeconds: 60 * 60 * 24 * 30 // 30 days
              }
            }
          }
        ],
        navigateFallback: null
      },
      devOptions: {
        enabled: true
      }
    })
  ],
  build: {
    target: 'es2022',
    sourcemap: true
  }
});
